# Re-derivation from the executable: team-a/spec-tables-audio-radio.md (Team A, 2026-10-01)

Inputs: the spec's NEEDS-EXE units and OPEN notes; index `exe_index/tables-audio-radio.txt`.
**The desk review `adv_tables-audio-radio.md` is absent** — it is not at `review/exe-notes-2026-10-01/`, not under
`review/` at all, and no similarly-named file exists anywhere in the review tree (checked by glob over `review/**`).
This pass therefore works from the spec's own `[OPEN — desk review 2026-09-30 …]` notes and its per-unit
"Review status (2026-09-30)" lines alone; nothing from the desk review's own consolidated exe list was available.
Dumps: bridge job `20261001T123123-team-a-ytgi` (paths below are relative to that job's `results/` folder; the
local mirror is `D:\Crreish-sync\for-team-a\bus-results-team-a\results\20261001T123123-team-a-ytgi\`).
Every dump is a depth-0 function dump or a global cross-reference listing; callees are not in the dumps unless the
index lists them separately. Clean-room: own words, addresses only, no pseudocode, no decompiler auto-names.

Label rules: **CONFIRMED — disassembly** only for what was read in these dumps; HIGH CONFIDENCE / HYPOTHESIS /
OPEN otherwise. Accessor naming used below (from the spec's own §1.2 catalogue): 0x00DABDF0 / 0x00DABE80 are the
"always" / "if present" `u32` readers, 0x00DABC70 / 0x00DABD20 the `s32` pair, 0x00DACCB0 / 0x00DACD40 the `f32`
pair, 0x00DAC480 / 0x00DAC510 the `bool` pair, 0x00DAC300 / 0x00DAC3B0 the `s8` pair. Two further *text* accessors
recur in this group and are not in that catalogue: 0x00DABA10 and 0x00DABA40 (both take a node and a child name
and return a text pointer or NULL; neither is dumped here, so their "always" / "if present" flavour is not stated).

Double-constant caution (project-wide for this spec): a raw "0 in the static image" reading of a double is only
its low dword when the fractional part is zero (1000.0 and 100.0 both have a zero low dword); only the decompiler's
own folded literal (e.g. `* 1000.0`) or both dwords of the constant count as a read of the value.

---

## §1.2 `FUN_00462960` / `FUN_0046FD00` — the Wwise string-to-id path

**Verdict: PARTLY CONFIRMED (wrapper, special cases, hand-off to the SDK); the hash algorithm itself stays OPEN (not in these dumps).**

Evidence:
- `func1/func_0x00462960.txt` (0x00462960, body 0x00462960–0x0046296B, five instructions): saves ESI, loads the
  caller's single stack argument into ESI, calls 0x0046FD00, restores ESI, returns. No other instruction. The
  return value (EAX) is whatever 0x0046FD00 produced. So it is a pure calling-convention shim (stack argument →
  register argument), nothing more. CONFIRMED — disassembly.
- `func1/func_0x0046fd00.txt` (0x0046FD00, body 0x0046FD00–0x0046FD7D): takes the string in ESI. It returns **0**
  (EAX cleared at 0x0046FD18) when the pointer is NULL, when the string compares equal to the literal `none` at
  0x0129B26C under `_stricmp` (so `NONE`, `None` … also give 0), or when the string is empty (inline length loop
  at 0x0046FD43–0x0046FD4F). Otherwise it calls a helper 0x0046FC50 with the string in EDX and a 0x100-byte stack
  buffer in ECX, then passes that buffer to the routine Ghidra labels `AK::SoundEngine::GetIDFromString` at
  0x00F4C350 and returns its result. The decompiler types the buffer as a 128-element wide-character array, which
  is consistent with the wide-string overload of that SDK call and with 0x0046FC50 being a narrow-to-wide
  conversion — that part is HIGH CONFIDENCE, not read (0x0046FC50 is not dumped).
- So: the hash is **the Wwise SDK's own string-to-id function**, not engine code; its algorithm (case folding,
  variant) lives in 0x00F4C350 and was not dumped in this job. The three zero-returning special cases are new.

Spec text changes (§1.2, second paragraph):
- Keep "`FUN_00462960` is a thin pass-through to `FUN_0046FD00`"; append after "(no argument transformation
  visible; a register-forwarding wrapper)": *"— 2026-10-01, disassembly: five instructions, moving the one stack
  argument into ESI and forwarding the result unchanged."*
- Strike the whole `[OPEN — desk review 2026-09-30: only the pass-through … to be settled against the
  executable.]` note; replace with: *"**[2026-10-01, re-derived (job 20261001T123123-team-a-ytgi): CONFIRMED —
  disassembly that `FUN_0046FD00` returns 0 for a NULL pointer, an empty string, or the text `none` compared
  case-insensitively, and otherwise converts the string through a helper (`0x0046FC50`, not dumped; the
  decompiler types its output as a 128-wide-character buffer) and hands it to the Wwise SDK's own
  `AK::SoundEngine::GetIDFromString` (`0x00F4C350`). The hash algorithm is therefore the SDK's, not engine code;
  it remains OPEN until `0x00F4C350` and `0x0046FC50` are read. Consequence for every Wwise-hashed field in this
  document: a `Name`/`wwise_id`/`Wwise_switch` text that is empty or literally `none` (any case) stores id 0.]**"*
- New Review status: **Review status (2026-10-01): CONFIRMED — disassembly for the wrapper and for the three
  zero-returning inputs of `FUN_0046FD00` (NULL / empty / `none`); the id algorithm is the Wwise SDK's
  `GetIDFromString` (`0x00F4C350`), not re-derived — not cleared: algorithm OPEN — re-derived from the executable
  (job 20261001T123123-team-a-ytgi).**

Next dump: func 0x00F4C350 (the SDK string-to-id body), func 0x0046FC50 (the conversion helper — settles whether
any case folding happens before the SDK call).

---

## §1.3 item 1 `FUN_00464730` — the `wwise_id` digit-prefix parser

**Verdict: CONFIRMED (grammar as stated) + the OPEN accumulator question SETTLED: 32-bit modular wrap-around, no saturation.**

Evidence (`func1/func_0x00464730.txt`, 0x00464730, body 0x00464730–0x00464766, no callees, single caller
0x00464BEB inside the bank loader):
- The string arrives in EDX; the result accumulates in EAX, which starts at 0.
- NULL pointer → 0 (0x00464732). First byte `-` (0x2D) → 0 (0x00464738, jumps straight to the zeroing exit).
- Digit loop (0x00464742–0x00464757): while the byte is in `0`…`9` (signed byte compares, so any byte ≥ 0x80 also
  counts as a non-digit and stops the scan), the accumulator is multiplied by ten and the digit added, using two
  address-arithmetic instructions on the full 32-bit register (0x0046474A, 0x0046474E). **There is no overflow
  test, no 64-bit extension and no clamp**: more than ten digits, or a value above 4,294,967,295, simply wraps
  modulo 2³². The decompiler types the result as a signed `int`, but the bit pattern is the same either way and
  the caller stores it as a dword at record `+0x40`.
- Stop character test (0x00464759–0x00464762): the value is kept only if the byte that ended the scan is `\0` or
  `.`; anything else zeroes the result. Matches the spec's grammar exactly, including the desk-review
  clarifications (leading space or `+` → 0; empty → 0; `12.5` → 12).

Spec text changes (§1.3 item 1):
- Strike the `[OPEN — desk review 2026-09-30: the accumulator's width … (FUN_00464730).]` note; replace with:
  *"**[2026-10-01, re-derived (job 20261001T123123-team-a-ytgi): CONFIRMED — disassembly: the accumulator is the
  full 32-bit register, multiplied by ten and added to with no overflow check, so an id of more than ten digits
  (or above 2³²−1) wraps modulo 2³² — no saturation, no error. The stop-character rule and the leading-`-` rule
  are as stated; byte values ≥ 0x80 are treated as non-digits because the compares are signed.]**"*
- New Review status: **Review status (2026-10-01): CONFIRMED — disassembly (full body re-read: grammar,
  32-bit modular accumulator) — cleared for implementation — re-derived from the executable (job
  20261001T123123-team-a-ytgi).**

---

## §1.3 item 2 `FUN_00D9E7E0` — the case-sensitive CRC-32 entry point (with §9's call site)

**Verdict: CONFIRMED; the seed / final-XOR / length-limit questions are SETTLED (seed 0, no XOR, no limit at the `foley_touch` site); the "why it differs" question is answered to the extent the dumps allow.**

Evidence:
- `func8/func_0x00d9e7e0.txt` (0x00D9E7E0, body 0x00D9E7E0–0x00D9E846, **no callees at all**): the calling shape
  is three stack arguments — string pointer, initial CRC value (seed), maximum byte count — plus a pointer in ECX
  through which the result is written (the function also returns that pointer in EAX; the caller re-reads the
  dword). Behaviour read instruction by instruction:
  - NULL string → the output dword is set to **0** regardless of the seed (0x00D9E7EB stores the zero pointer
    value itself).
  - Empty string → the output is the **seed unchanged** (0x00D9E7FB → 0x00D9E83A path).
  - Otherwise, for each byte while the running 1-based byte count is ≤ the maximum (0x00D9E810: an unsigned
    "above" test, so a maximum of 0xFFFFFFFF means "no limit"): the byte is XOR-ed into the low byte of the
    running value, that low byte indexes the 256-entry dword table at 0x01320DA0, and the running value is shifted
    right by 8 and XOR-ed with the table entry (0x00D9E814–0x00D9E824). This is the standard reflected,
    table-driven CRC-32 step. **No instruction folds case, no final XOR or complement is applied, and nothing is
    pre-/post-processed** — the raw running value is stored.
- The dump's globals section lists **eight distinct functions** that index the table 0x01320DA0: 0x00D9E6C0,
  0x00D9E700, 0x00D9E740, 0x00D9E790, 0x00D9E7E0, 0x00D9E850, 0x00D9E8B0, 0x00D9E920. So 0x00D9E7E0 is a
  genuinely separate, self-contained entry point into the same table (it calls nothing, so it cannot be "a wrapper
  that bypasses a lower-casing step" — there is no wrapper layer). Whether 0x00D9E740/0x00D9E8B0 lower-case their
  input is not re-read here (neither is in this job); the spec's statement that they do rests on
  `spec-tables-weapons-combat.md` §1.4 as before.
- Call site, `func2/func_0x00561ab0.txt` (0x00561B23–0x00561B31): the three pushes are `-1` (maximum byte count
  0xFFFFFFFF = no length limit), `0` (seed 0), and the `Name` text pointer; ECX points at a stack slot; the result
  dword is read back from it immediately. CONFIRMED — disassembly: **seed 0, no length limit, no final XOR, no
  case folding** for `foley_touch.xtbl`'s row-name key.

Spec text changes (§1.3 item 2):
- Strike "Whether the lower-casing happens in a wrapper this particular call bypasses, or `FUN_00D9E7E0` is a
  genuinely separate case-sensitive entry point into the same table, was not resolved further." Replace with:
  *"`FUN_00D9E7E0` is a genuinely separate entry point: its body calls nothing, so there is no wrapper layer to
  bypass; the table at `0x01320DA0` is indexed by eight distinct functions (`0x00D9E6C0`, `0x00D9E700`,
  `0x00D9E740`, `0x00D9E790`, `0x00D9E7E0`, `0x00D9E850`, `0x00D9E8B0`, `0x00D9E920`), of which this is one
  (2026-10-01, disassembly)."*
- Strike the `[OPEN — desk review 2026-09-30: the seed passed at the foley_touch call site … to be settled
  against the executable.]` note; replace with: *"**[2026-10-01, re-derived (job 20261001T123123-team-a-ytgi):
  CONFIRMED — disassembly. Calling shape: (string, seed, maximum byte count) on the stack, result written through
  a pointer passed in ECX. NULL string → 0 whatever the seed; empty string → the seed unchanged; otherwise the
  reflected table-driven CRC-32 step over at most "maximum" bytes, with no case folding and no final XOR. At the
  `foley_touch` call site (`0x00561B23`–`0x00561B31`) the seed is 0 and the maximum is 0xFFFFFFFF (no length
  limit).]**"*
- New Review status: **Review status (2026-10-01): CONFIRMED — disassembly (body of `FUN_00D9E7E0` and the
  `FUN_00561AB0` call site: seed 0, no limit, no final XOR, case-sensitive; separate entry point, not a wrapper)
  — cleared for implementation — re-derived from the executable (job 20261001T123123-team-a-ytgi).**

---

## §2 `audio_banks.xtbl` — record tiling, `+0x54`/`+0x5b` writers, free list, DLC coverage of the `wep_` pass

**Verdict: CORRECTED in four places (the `voice` row, the `+0x48` wording, the `+0x54`/`+0x56` sentence, the
"Unknown" fallback), CONFIRMED for the hard-fail rule and most of the table, tiling largely SETTLED (`+0x5A` is
the DLC index; `+0x54`/`+0x56` are two `u16`s); free-list capacity and `+0x5b` bit 6 still OPEN (their writers
are not in this job); DLC coverage of `FUN_00561C00` SETTLED (base file only).**

Evidence — `func1/func_0x00464a70.txt` (the loader, 0x00464A70, body to 0x00464F2E):
- Filename: for a DLC index > 0 (signed byte test at 0x00464A8B/0x00464A8E) it formats `dlc%d_%s` with
  `audio_banks.xtbl`; otherwise the plain name. Existence check 0x00DA90D0, then the opener 0x00DAC9A0 with
  (name, 0, 1). No document or no first `NewEntity` child → return 0.
- Per row, five stack flags are reset (streaming := 1, boot-load := 0, Init := 0, ram_bank := 0, cacheable := 0)
  before anything is read.
- `Name` via text accessor 0x00DABA10; **NULL → the function returns −6 (0xFFFFFFFA) at once** (0x00464F1B),
  without closing the document and without visiting the remaining rows. Inline case-sensitive byte compare with
  `Init` (0x00464BA0–0x00464BC7): equal → Init flag and boot-load flag both set.
- `wwise_id` via 0x00DABA10; **NULL → the same −6 return** (0x00464BE3). Non-NULL → parsed by 0x00464730 (§1.3
  item 1).
- `streaming` via 0x00DABA40; inline case-sensitive compare with `False` → streaming flag := 0.
- `load_at_boot` via 0x00DABA10, **only when the DLC index is ≤ 0** (0x00464C3D); inline case-sensitive compare
  with `True` → boot-load flag := 1.
- `ram_bank_pc`, `cacheable_pc` via 0x00DABA40; inline case-sensitive compare with `True` → their flags.
- `ram_size_pc`: a dword local is zeroed, then the "if present" `u32` reader 0x00DABE80 fills it.
- `voice` via 0x00DABA40; `_stricmp` against `True` (a real library call, 0x00464D49) → voice flag := 1.
- Record allocation: 0x004648C0 with the `Name` text in ECX and (wwise_id, streaming flag, voice flag) on the
  stack. **A NULL record (free list exhausted) does not abort the load — the loop goes on to the next row**
  (0x00464D74 → 0x00464EF5).
- Post-allocation writes, all on the record returned:
  - base-game row (DLC index ≤ 0) **and** boot-load flag: `+0x54` (u16) := **0x8004** (0x00464D94), then
    0x004673C0 is called with argument 1, EAX = the record and EDI = the registry address 0x0320E520 (not dumped);
  - ram_bank flag → `+0x5b` |= 0x08 (bit 3); cacheable flag → `+0x5b` |= 0x04 (bit 2);
  - **voice flag → `+0x5b` |= 0x10 (bit 4)** (0x00464DB7) — the spec's "transient local only, not stored" is wrong;
  - `+0x50` := 0xFFFFFFFF;
  - ram_size ≠ 0 **and** boot-load flag → 0x00470160 with EDI = the record, EAX = ram_size (the pool-creation
    call the spec names; its body is not in this job, so the `CreatePool`/`SetPoolName` identification is as
    previously read, not re-derived);
  - base-game row **and** Init flag: `+0x54` := 0x8004 again, **`+0x5b` bit 0 := 1** (0x00464DE6–0x00464DF9: the
    bit is set equal to the Init flag's bit 0 via an XOR-mask sequence), and 0x004673C0(1) again;
  - **`+0x5a` (byte) := the DLC index argument** (0x00464E15), unconditionally — 0 for the base file, N for
    `dlcN_audio_banks.xtbl`;
  - **if `+0x44` == 1** (streaming flag set — the default): copy the record's name (bounded, 0x41) into a stack
    buffer; if `+0x5b` bit 5 (0x20) is set, append `_` (0x0129EE18) and the platform-token global 0x0317283C;
    append `_media`; look the name up with 0x0046E320 (ECX = buffer) and, if that returns nothing, build a path
    with 0x004651E0 and open it through the function pointer at 0x03172888; then read **0x2C = 44 bytes** through
    the function pointer at 0x03172890. **A read that returns anything other than 44 makes the whole function
    return −4 (0xFFFFFFFC)** (0x00464F25). On success **`+0x56` (u16) := the u16 at offset 0x18 of those 44
    bytes** (0x00464EC8–0x00464ECD), and — for a base-game row whose `+0x54` has bit 15 set — that u16 is added to
    the running global 0x03171A8C (0x00464EE6). The file handle is then released (0x00DAA440).
- Next row via the sibling walker; at the end the document is closed and 0 is returned.

Evidence — `func1/func_0x004648c0.txt` (the allocator/filler, 0x004648C0, body to 0x00464A64, single caller
0x00464D68):
- The registry is a structure whose address is the dword at 0x0320E520. **The free count is the u16 at registry
  `+0x1E`**; if it is 0 the function formats `Bump AUDIOLIB_MAX_SOUNDBANKS!\n` into a stack buffer and passes it to
  the log callback pointer at 0x031728BC when that is non-NULL, then returns NULL. Otherwise 0x00467AA0 (registry)
  returns a slot index, 0x00467A20 is called with that index (ECX) and the address 0x0320E524 (EAX) — the pop /
  bookkeeping step — the byte at (registry `+0x08` pointer)[index] is cleared, and the record is **registry `+0x00`
  base + index × 0x5C**. So: fixed slots, stride 0x5C, free count at registry `+0x1E`, record base at registry
  `+0x00`, a per-slot byte array at registry `+0x08`. The initial free count (the capacity) is not in this
  function.
- `Name` non-NULL: bounded copy 0x00DA7930(record, name, 0x40) into `+0x00`; if the voice flag is set, 0x00464820
  is called with ESI = the record (not dumped; it references `_`, the platform token and `bnk_pc`); then the Wwise
  hash of the name (0x0046FD00, §1.2) becomes the registration key. `Name` NULL: the literal `Unknown` is copied
  and the numeric wwise_id is the key. **The `Unknown` branch is unreachable from this table's loader**, which
  returns −6 before calling the allocator when `Name` is absent (and 0x004648C0 has no other caller).
- The key and the address of the record pointer are passed to 0x004675F0 with ECX = the map object at 0x0321B4B0
  — registration into the name-hash → record map that §17's lookup 0x004647C0 reads (see §17 below).
- `+0x40` := wwise_id; `+0x44` := 0, then := 1 if the streaming flag is set. If set and `+0x5b` bit 5 (0x20) is
  **clear** on the fresh record: the name is copied (bounded 0x400) and `_media.` then `bnk_pc` appended
  (0x004649B4–0x004649DC); if bit 5 is set, 0x00465180 builds the variant with the platform token instead. Then
  0x00DAB0C0 is called with (buffer, the literal at 0x0129A85C, 0, 0, −1) — the dump shows that literal's bytes as
  `rb`, i.e. an open-for-binary-read mode string, so 0x00DAB0C0 is an open-by-name. If the open succeeds,
  **`+0x48` := the result of 0x00DA7D20(handle), and the handle is released at once by 0x00DAA440** — `+0x48` is
  not the handle. (0x00DA7D20 is not dumped; a length query is the natural reading — HIGH CONFIDENCE, not read.)
- **`+0x54` (u16) := 1** on every successful allocation (0x00464A14–0x00464A19), before the loader overrides it
  with 0x8004 for boot-load rows.
- Nothing in either function writes `+0x45`–`+0x47`, `+0x4C`–`+0x4F`, `+0x58`–`+0x59`, or `+0x5b` bits 1, 5, 6, 7.
  Neither function clears the record before filling it; whether a slot is zero when handed out depends on the
  registry's initialisation and release paths, which are not in this job.

Evidence — `glob/xref_0x0320e520.txt`: 32 references in 11 functions; **the only write is at 0x00466F56 inside
0x00466ED0** (the registry constructor/initialiser). Readers include the bank state machine 0x00465CC0 (14
references), 0x00464F30, 0x00465410, 0x004661C0, 0x004662D0, 0x00466850 (the loader's caller), 0x00466940,
0x00466C20. The capacity constant must therefore be in 0x00466ED0 (or a value it is handed) — not dumped.

Evidence — `func2/func_0x00561c00.txt` (the `wep_` pass, 0x00561C00, body to 0x00561C8D, single caller
0x00B85554 in 0x00B85400): the filename literal `audio_banks.xtbl` is pushed **directly** (0x00561C05) — no
`dlc%d_` formatting, no loop over DLC indices. It walks `NewEntity` rows, reads `Name` via 0x00DABA10, and for a
NULL-safe `_strnicmp(name, "wep_", 4) == 0` hashes the name through 0x00462960 and calls 0x004641F0 with ECX =
the object at 0x013C85D8. **It never opens the `dlcN_` copies.** CONFIRMED — disassembly. 0x004641F0 (the
find-or-insert with the capacity) is not dumped; the object 0x013C85D8 is also referenced from 0x00553B40,
0x00561C90 and two static-initialiser sites (0x00FEE1B0, 0x01016DE0).

Evidence — `func1/func_0x004647c0.txt` (the name → bank-record lookup §17 uses, 0x004647C0): hashes the queried
name with 0x0046FD00, reduces the hash modulo 0x223 (= 547; the multiply-shift sequence at 0x004647CB–0x004647DD
is a division by 547), asks 0x00467B90 on the map object 0x0321B4B0 for a u16 slot; 0xFFFF → returns NULL,
otherwise returns the dword at 0x0321C5D0[slot] — the record pointer stored at registration. So the registry's
by-name lookup is **by Wwise id of the name**, 547 buckets. CONFIRMED — disassembly for this function; the map
internals (0x004675F0 / 0x00467B90) are not dumped.

Spec text changes:
- §2.1, `FUN_00561C00` bullet: after "registers the id into a small fixed hash/refcount structure via
  `FUN_004641F0`" insert *"(the structure is the object at `0x013C85D8`)"*; append to the bullet: *"**2026-10-01,
  disassembly: this pass opens only the plain `audio_banks.xtbl` — the literal is pushed directly, there is no
  `dlc%d_` formatting — so the 48 DLC rows (§19) are never seen by the `wep_` cache.**"*
- §2.2: append *"**(2026-10-01, disassembly: the loader returns −6 (`0xFFFFFFFA`) in both cases, leaving the
  document open; a companion-file header read that returns fewer than 44 bytes likewise aborts the whole load with
  −4 (`0xFFFFFFFC`). By contrast, exhausting the record free list does not abort — the row is skipped and the walk
  continues.)**"*
- §2.3 table, `Name` row: strike `"Unknown" if literally absent (dead in practice)` → *`"Unknown" fallback inside
  FUN_004648C0 — unreachable from this loader, which returns −6 before allocating when Name is absent (2026-10-01,
  disassembly)`*. Also append to the Notes cell: *"Registered (Wwise id of `Name` → record pointer) in the map at
  `0x0321B4B0` via `0x004675F0`, 547 buckets; `FUN_004647C0` (§17) reads it back."*
- §2.3 table, `streaming` row: strike "storing the resulting handle at `+0x48`" → *"storing at `+0x48` the value
  0x00DA7D20 returns for the opened file (the handle itself is released immediately afterwards by 0x00DAA440;
  0x00DA7D20 is not dumped — a length query is the natural reading, HIGH CONFIDENCE). The companion name is
  `<Name>` + `_media.` + `bnk_pc`, or — only if `+0x5b` bit 5 is already set on the freshly allocated record —
  the variant built by 0x00465180 with `_` + the platform token (2026-10-01, disassembly)."*
- §2.3 table, `voice` row: strike "a transient local only — not stored in the record" → *"**`+0x5b` bit 4
  (`0x10`)** — set when `voice` is exactly `True` under `_stricmp`; the flag is also passed to the allocator,
  which then runs 0x00464820 on the record before hashing its name (not dumped) (2026-10-01, disassembly —
  CORRECTED)."*
- §2.3, the sentence beginning "Fields `+0x50` (unconditionally set to `0xFFFFFFFF` …) and `+0x54`/`+0x56`
  (written only for the boot-load path, from a direct 44-byte header read …)": strike the `+0x54`/`+0x56` clause
  and replace with: *"`+0x54` and `+0x56` are two separate `u16`s. `+0x54` is set to 1 by the allocator on every
  record and overwritten with **`0x8004`** by the loader for base-game rows that are `Init` or `load_at_boot =
  True` (bit 15 is therefore the loader's own boot-load mark, §17). `+0x56` is written from offset 0x18 of a
  44-byte header read of the resolved `_media.bnk_pc` companion, for **every row whose streaming flag is set**
  (the default), not only boot-load rows; for base-game boot-load rows that same u16 is also summed into the global
  `0x03171A8C`. (2026-10-01, disassembly — CORRECTED.)"*
- §2.3, strike the `[OPEN — desk review 2026-09-30: the record table above does not tile 0x5c …]` note; replace
  with: *"**[2026-10-01, re-derived (job 20261001T123123-team-a-ytgi): tiling — `+0x00`–`+0x3F` Name (bounded
  copy, limit 0x40; the loader later re-copies it with limit 0x41, consistent with a terminator inside the 0x40
  bytes — HIGH CONFIDENCE, 0x00DA7930 not dumped); `+0x40` u32 wwise_id; `+0x44` byte streaming flag;
  `+0x45`–`+0x47` not written by the loader or allocator; `+0x48` dword from 0x00DA7D20 (streaming rows only);
  `+0x4C`–`+0x4F` not written; `+0x50` dword 0xFFFFFFFF; `+0x54` u16 (1 / 0x8004); `+0x56` u16 (companion header
  offset 0x18); `+0x58`–`+0x59` not written; **`+0x5A` byte = DLC index (0 base, N for `dlcN_`)**; `+0x5B` flag
  byte: bit 0 Init, bit 2 cacheable_pc, bit 3 ram_bank_pc, bit 4 voice, bit 5 read (platform-suffixed companion
  name) but never written here, bits 1/6/7 untouched here. The "boot-load" flag is a loader-local; its persistent
  traces are `+0x54` = 0x8004, the two 0x004673C0(1) calls, and (for `Init`) `+0x5B` bit 0. Nothing here zeroes a
  record before filling it. Still OPEN: the free-list capacity (free count = u16 at registry `+0x1E`; the registry
  pointer 0x0320E520 is written only by 0x00466ED0, not dumped), the writer of `+0x5B` bit 6 (neither function
  here; candidates 0x004673C0 and the state machine 0x00465CC0), and whether 0x00466ED0 zeroes the slots.]**"*
- Strike "The exact capacity constant was not resolved (**OPEN**)" → keep OPEN but refine: *"The capacity is the
  initial value of the u16 free count at registry `+0x1E`, set by 0x00466ED0 (the only writer of `0x0320E520`),
  not dumped — OPEN."*
- New Review status: **Review status (2026-10-01): CORRECTED (`voice` → `+0x5B` bit 4; `+0x48` is not the handle;
  `+0x54`/`+0x56` are two u16s with the writers above; `Unknown` fallback unreachable) and CONFIRMED — disassembly
  (hard-fail −6/−4 returns, `+0x5A` = DLC index, `+0x5B` bits 0/2/3/4, DLC rows never seen by `FUN_00561C00`,
  by-name lookup via the 547-bucket map at `0x0321B4B0`) — not cleared: free-list capacity (0x00466ED0) and the
  `+0x5B` bit-6 writer (0x004673C0 / 0x00465CC0) remain OPEN — re-derived from the executable (job
  20261001T123123-team-a-ytgi).**

Team B relevance: the `voice` → `+0x5B` bit 4 correction and the `+0x5A` DLC-index byte change the record a
reimplementation must produce; the `wep_` cache ignoring DLC rows changes which banks are preloaded.

Next dump: func 0x00466ED0 (registry initialiser: capacity, zeroing), func 0x004673C0 (boot-load request —
likely writer of the state bits incl. `+0x5B` bit 6), func 0x004641F0 (the `wep_` cache's capacity),
func 0x00DA7D20 (what `+0x48` holds), func 0x00464820 (the `voice` name adjustment), func 0x00DA7930
(terminator rule of the bounded copy).

---

## §9 `foley_touch.xtbl` — hash seed and the consumer of `0x013C85D0`

**Verdict: CONFIRMED (seed 0, no limit, no XOR — see §1.3 item 2) + CORRECTED (`Frequency` uses the "always"
reader) + consumer IDENTIFIED by address but not read (still OPEN for its matching rule).**

Evidence (`func2/func_0x00561ab0.txt`, 0x00561AB0, body to 0x00561BA3, single caller 0x00554DF6 in 0x00554C90):
- Opens `foley_touch.xtbl` (0x00DAC9A0 with (name, 0, 1)); first `FoleyTouch` child; the row count from
  0x00DC5150 is stored as a **u16** at 0x013C85D4 (0x00561AE8); the array of count × 0x0C bytes is obtained
  through the allocator at vtable slot `+0x38` of the object passed in (arguments size, 4, 0, 0) and stored at
  0x013C85D0. Whether that allocator zeroes is not visible.
- Per row: `Name` text via 0x00DABA40 → 0x00D9E7E0 with (text, seed 0, limit 0xFFFFFFFF) → `+0x00`. First
  `TouchFoleySet` child via 0x00DC4FF0 (not iterated). `Wwise_switch` text via 0x00DABA10 → 0x00462960 → `+0x08`.
  **`Frequency` via 0x00DABDF0 — the "always" `u32` reader — into `+0x04`** (0x00561B70), not the "if present"
  0x00DABE80 the spec's table states. Stride 0x0C (0x00561B8A). Document closed at the end.
- `glob/xref_0x013c85d0.txt`: six references in four functions — the loader's own write and two reads, and reads
  in **0x00561A70**, **0x00561A90** and **0x00561BB0**; 0x00561BB0 also reads the count 0x013C85D4 (seen in the
  loader dump's globals section at 0x00561BC5). These three are the consumers; none is dumped, so the matching rule
  (which of them is the analogue of §8's `FUN_00561370`) is not read.

Spec text changes (§9):
- Table row `Frequency`: strike `u32`, "if present" → *`u32`, **"always"** (`FUN_00DABDF0`), written
  unconditionally into `+0x04` (an absent element leaves whatever the "always" reader produces for a missing child —
  `spec-tables-weapons-combat.md` §1.3's caveat) (2026-10-01, disassembly — CORRECTED)*.
- Table row name id: append *"seed 0, no length limit, no final XOR (2026-10-01, disassembly, §1.3 item 2)"*.
- Strike the `[OPEN — desk review 2026-09-30: the seed passed to FUN_00D9E7E0 here … readers of 0x013C85D0.]`
  note; replace with: *"**[2026-10-01, re-derived (job 20261001T123123-team-a-ytgi): seed 0, no length limit,
  no final XOR, case-sensitive — CONFIRMED — disassembly. The count is a u16 at `0x013C85D4`; the array is
  allocated count × 12 through the allocator object handed to the loader. Consumers of `0x013C85D0`: exactly
  three other functions read it — `0x00561A70`, `0x00561A90` and `0x00561BB0` (the last also reads the count) —
  identified from the cross-reference listing but not read; the matching rule stays OPEN.]**"*
- New Review status: **Review status (2026-10-01): CONFIRMED — disassembly (seed 0 / no limit / no XOR;
  record layout; u16 count) and CORRECTED (`Frequency` is the "always" reader) — not cleared: the consumer
  (`0x00561BB0` / `0x00561A70` / `0x00561A90`) is identified but unread — re-derived from the executable (job
  20261001T123123-team-a-ytgi).**

Next dump: func 0x00561BB0, func 0x00561A70, func 0x00561A90.

---

## §3 `audio_constants.xtbl` — `PlayTimers` type, squaring, and the `med_health` precision

**Verdict: CORRECTED (the twelve `PlayTimers` are `u32`, not `f32`, and the two distance squarings are integer
multiplies) + CONFIRMED for every other element and destination + the precision OPEN SETTLED (double multiply).**

Evidence (`func1/func_0x00553580.txt`, 0x00553580, body to 0x00553876, single caller 0x00554DD1 in
0x00554C90; the whole body is straight-line, no NULL checks on any node):
- `AudioConstants` → `PlayTimers`: **all twelve** children are read with 0x00DABDF0 — the "always" **`u32`**
  reader — into twelve consecutive dword globals 0x013BB440 … 0x013BB46C (in the spec's order: `BrassCollision`
  0x013BB440, `GlassShatter` 0x013BB444, `BulletImpactHuman` 0x013BB448, `BulletImpactWall` 0x013BB44C,
  `ObjectDebris` 0x013BB450, `VehicleImpactCollision` 0x013BB454, `VehicleImpactDistance` 0x013BB458,
  `VehicleScrapeCollision` 0x013BB45C, `VehicleScrapeDistance` 0x013BB460, `RagdollBoneImpactCollision`
  0x013BB464, `SmallDeformation` 0x013BB468, `LargeDeformation` 0x013BB46C). The twelve-name count is confirmed.
  `VehicleImpactDistance` and `VehicleScrapeDistance` are squared in place with an **integer multiply**
  (0x00553629, 0x00553659) — not a float operation; a consumer in 0x00558C60 even loads 0x013BB458 with an
  integer-to-float instruction (seen in the globals section). So these are integers end to end.
- `OnFootSettings` → `FootstepRange`: "always" `f32` (0x00DACCB0) into a stack local, widened to double, squared
  in double, narrowed back to float and stored at 0x013BB470 (0x005536B0–0x005536CB).
- `DrivingSettings` → `AmbientSpawnAcquireRadio` `f32` → 0x013BB474; child `Alarm` → `Percentage` `f32` →
  0x013BB49C, `TimeMin`/`TimeMax` each read by the "always" `u32` reader into a dword local and the low 16 bits
  stored at 0x013BB4A0 / 0x013BB4A2; child `Passby_whoosh` → `min_distance_on_foot` 0x013BB4A4,
  `min_distance_driving` 0x013BB4A8, `min_speed` 0x013BB4AC (all "always" `f32`).
- `Wind` (literal at 0x01119394, confirmed `Wind`) → `high_altitude` → `min_speed` 0x013BB4B0, `max_speed`
  0x013BB4B4, `wind_change_rate` 0x013BB4B8, `min_altitude` 0x013BB4BC, `max_altitude` 0x013BB4C0;
  `player_falling` → `min_speed` 0x013BB4C4, `max_speed` 0x013BB4C8, `parachute_speed` 0x013BB4CC (all
  "always" `f32`).
- `Player_Health` → `med_health`: "always" `s32` (0x00DABC70) into a dword local; then (0x0055384A–0x00553861)
  the integer is converted **to single precision first**, widened to double, multiplied in **double** by the
  qword at 0x01117DC8, and narrowed to single for the store at 0x013BB4D0. The decompiler folds the constant as
  `0.01`; the dump's low dword is 0x40000000, which together with the spec's previously-read value
  0.009999999776482582 is exactly the pattern of a single-precision 0.01 widened to double (low 29 bits zero) —
  consistent, not a contradiction. So: `stored = float( double(float(int)) × 0.009999999776482582 )`.

Spec text changes (§3):
- First bullet: strike "(~~11~~ **12** `f32` — … all the shared "always" `u32`/`f32` family,
  `FUN_00DABDF0`/`FUN_00DACCB0`" → *"(**12 `u32`** — every one read by the shared "always" `u32` reader
  `FUN_00DABDF0` into the dword globals `0x013BB440`–`0x013BB46C`; 2026-10-01, disassembly — CORRECTED: these are
  integers, not floats)"*. In the same bullet strike "(squared in place after load — `distance²` …)" → *"(squared
  in place after load by an **integer** multiply — `distance²` as an integer, presumably compared against a
  squared position-delta to avoid a `sqrt`)"* for both distance fields.
- `OnFootSettings` bullet: append *"(squared in double precision, stored as `f32` at `0x013BB470`)"*.
- `DrivingSettings` bullet: append the destinations *"(`0x013BB474`; `Alarm` → `0x013BB49C`, `u16` at
  `0x013BB4A0`/`0x013BB4A2`; `Passby_whoosh` → `0x013BB4A4`/`0x013BB4A8`/`0x013BB4AC`)"*; `Wind` bullet: append
  *"(`0x013BB4B0`–`0x013BB4C0`; `player_falling` `0x013BB4C4`–`0x013BB4CC`)"*.
- `Player_Health` bullet: strike the `[OPEN — desk review 2026-09-30: whether the × 0.01 multiply is done in
  single or double precision …]` note; replace with: *"**[2026-10-01, re-derived (job
  20261001T123123-team-a-ytgi): CONFIRMED — disassembly: the integer is converted to single precision, widened to
  double, multiplied in double by the constant at `0x01117DC8` (0.009999999776482582, a single-precision 0.01
  widened), and narrowed to `f32` for the store at `0x013BB4D0`.]**"*
- New Review status: **Review status (2026-10-01): CORRECTED (`PlayTimers` are twelve `u32`, integer squaring)
  and CONFIRMED — disassembly for every element name, reader and destination, and for the `med_health` precision
  — cleared for implementation — re-derived from the executable (job 20261001T123123-team-a-ytgi).**

Team B relevance: a reimplementation that stores `PlayTimers` as floats (and squares the two distances in float)
will diverge from the engine's integer values; the integer type matters for any consumer comparison.

---

## §4 `audio_settings.xtbl` — where the defaults are primed, the `Health_adjust_rate` fallback, and the Doppler default

**Verdict: CORRECTED (`Doppler_multiplier`'s coded default is 10.0, not 11.0) + priming location SETTLED (inside
`FUN_00467F40` itself) + `Health_adjust_rate` fallback SETTLED (0.0, zero-filled, no other writer).**

Evidence (`func1/func_0x00467f40.txt`, 0x00467F40, body to 0x00467FB8, single caller 0x0046801F in 0x00467FC0;
the `<global_settings>` node arrives in EDI):
- First child `general_settings` of that node (0x00DC4FF0). **Only if it exists**: the float at 0x012A2DE0
  (raw 0x43ABC000 = **343.5**) is stored to 0x03172974 (0x00467F55/0x00467F61), then the "if present" `f32`
  reader 0x00DACD40 is run for `Speed_of_sound` → 0x03172974 and for `Health_adjust_rate` → 0x0317297C. No
  priming of 0x0317297C anywhere in the function.
- Then, **unconditionally**, the float at 0x01118D20 (raw 0x41200000 = **10.0**, a constant shared by 139
  functions) is stored to 0x03172978 (0x00467F84/0x00467F90); then, only if a `Doppler_settings` child exists,
  the "if present" reader runs for `Doppler_multiplier` → 0x03172978. Return 0.
- Globals section: 0x03172974 and 0x03172978 are referenced **only by this function** (2 uses each) — so the
  priming is here and nowhere else, and neither global is read by any other function by name. 0x0317297C is
  referenced only here and by one reader, 0x0045F690 (a float multiply) — **no other writer exists**, and the
  global sits in the zero-filled (not file-backed) data block, so its fallback when `Health_adjust_rate` is absent
  is **0.0**.
- Consequence of the 10.0 reading: the shipped `Doppler_multiplier` (11.0, §4.1) **differs** from the coded
  default, so the real data does exercise the read — the desk review's worry was based on the wrong default.
- Consequence of the conditional priming: a `global_settings` block **without** a `general_settings` child leaves
  0x03172974 at its zero-fill value — speed of sound 0 — since nothing else writes it.

Spec text changes (§4):
- `general_settings` bullet: strike "(default primed from a global constant, value **343.5** …)" → *"(default
  **343.5** — the float at `0x012A2DE0`, written to `0x03172974` by `FUN_00467F40` itself, immediately before the
  read and **only when a `general_settings` child exists**; nothing else writes that global, so a block without
  `general_settings` leaves speed of sound at 0.0; 2026-10-01, disassembly)"*; strike "(no explicit
  default-priming instruction found in this reader; whatever the `.data` section already holds is the fallback if
  absent)" → *"(no priming anywhere: `0x0317297C` is in the zero-filled data block and has no writer other than
  this reader, so the fallback is **0.0**; its one consumer is `0x0045F690`; 2026-10-01, disassembly)"*.
- `Doppler_settings` bullet: strike "(default primed from a global constant, value **11.0**)" → *"(default
  **10.0** — the float at `0x01118D20`, written to `0x03172978` unconditionally by `FUN_00467F40` before the
  `Doppler_settings` lookup; 2026-10-01, disassembly — CORRECTED from 11.0)"*.
- Strike the whole `[OPEN — desk review 2026-09-30: where the 343.5 and 11.0 defaults are primed …]` note;
  replace with: *"**[2026-10-01, re-derived (job 20261001T123123-team-a-ytgi): both defaults are primed inside
  `FUN_00467F40` (343.5 conditionally on `general_settings` being present; 10.0 unconditionally);
  `Health_adjust_rate` falls back to 0.0; because the coded Doppler default is 10.0 and the shipped value is 11.0,
  the real data does show the element being read. CONFIRMED — disassembly.]**"*
- §4.1: the sentence "so the retail game runs on the coded default of 343.5 m/s for this field" stands.
- New Review status: **Review status (2026-10-01): CORRECTED (Doppler default 10.0) and CONFIRMED — disassembly
  (priming sites, 343.5, `Health_adjust_rate` fallback 0.0) — cleared for implementation — re-derived from the
  executable (job 20261001T123123-team-a-ytgi).**

Team B relevance: the Doppler default (10.0) and the zero fallbacks matter only for modded/absent elements; the
shipped file supplies 11.0 and 0.5 explicitly.

---

## §5 `audio_line_tags.xtbl` — the 119 cap, the absent-`wwise_id` rule, and the consumers of `0x01504484`

**Verdict: CONFIRMED (119 stored ids; count test after the store; absent element still advances the count) +
CORRECTED (`wwise_id` uses the "always" reader; the array is zeroed before the walk) + a new, stronger finding:
no function other than the loader references the array or its count.**

Evidence (`func4/func_0x00709d90.txt`, 0x00709D90, body to 0x00709EA6, single caller 0x0070B9BF in 0x0070B980):
- One byte argument: when it is 0 the 0x1E0-byte array is allocated through the allocator object 0x01516F88
  (0x00DAD460 with (0x1E0, 0, 0, 0)) and stored at 0x01504484; otherwise the existing pointer is reused (a
  reload path). NULL → return 0.
- The count 0x01504480 is set to 0 and **the whole 0x1E0-byte array is cleared with `memset`** (0x00709DE2)
  before the file is opened — so every slot starts at 0 on every load.
- Open `audio_line_tags.xtbl`; NULL → return 0. First `Audio_line` child.
- Per row: `Name` is copied by the bounded copy 0x00DABA70 into a 0x20-byte stack buffer that nothing reads again
  (confirmed). **`wwise_id` is read by 0x00DABDF0 — the "always" `u32` reader — directly into the current slot**
  (0x00709E49), not the "if present" 0x00DABE80 the spec states.
- Then (0x00709E4E–0x00709E62): the count is incremented, the slot pointer advanced by 4, and **if the new count is
  ≥ 0x77 (119) the loop exits** (signed compare, irrelevant here). The increment is unconditional — a row whose
  `wwise_id` is absent still consumes a slot and advances the count. The test runs **after** the store, so slots 0
  … 118 are written: **exactly 119 ids**, the 120th slot stays at its cleared 0. The 4,507 figure stands.
- Document closed; return 1.
- `glob/xref_0x01504484.txt`: **3 uses, all inside 0x00709D90** (one write, two reads). The loader dump's globals
  section shows the same for the count 0x01504480 (3 uses, all in the loader). So **no other function in the
  image refers to the array pointer or the count by name**. A consumer would have to be handed the pointer some
  other way; nothing in these dumps shows one. CONFIRMED — disassembly (cross-reference listing).

Spec text changes (§5):
- First paragraph: strike "`wwise_id` (`u32`, "if present", `FUN_00DABE80`)" → *"`wwise_id` (`u32`, **"always"**,
  `FUN_00DABDF0` — written straight into the slot; 2026-10-01, disassembly — CORRECTED)"*; after "(allocated once,
  `0x1E0` = 480 bytes = **120** `u32` slots)" insert *"and cleared to zero with `memset` on every load"*.
- Strike the `[OPEN — desk review 2026-09-30: whether the count test runs before or after the store …]` note;
  replace with: *"**[2026-10-01, re-derived (job 20261001T123123-team-a-ytgi): CONFIRMED — disassembly: the
  count is incremented after the store and the loop exits when it reaches 119, so slots 0–118 are written (119
  ids) and slot 119 stays 0; the increment is unconditional, so a row with no `wwise_id` still consumes a slot
  (its value is whatever the "always" reader leaves for a missing child). Further: the cross-reference listing
  for `0x01504484` and for the count `0x01504480` shows **no reference outside this loader** — no function in the
  image reads the array by name.]**"*
- §5.1, after "their `wwise_id` is never read at all once the count check fails": append *"(2026-10-01: and the
  119 that are stored are referenced by no other function by name — see the OPEN note above)"*.
- New Review status: **Review status (2026-10-01): CONFIRMED — disassembly (119-id cap, test-after-store,
  unconditional count advance, array zeroed per load) and CORRECTED (`wwise_id` is the "always" reader); the
  cross-reference listing shows no consumer of the array or count outside the loader — cleared for
  implementation — re-derived from the executable (job 20261001T123123-team-a-ytgi).**

---

## §8 `foley_collision.xtbl` — absent-`Frequency` value, absent `Name`/`CollisionFoleySet`, conversion precision

**Verdict: CORRECTED (`Frequency` is the "always" reader, so the "value left when absent" question is reframed)
+ CONFIRMED (record layout, stride, first-child-only, mph→m/s in double precision) + absent-`Name` SETTLED
(key 0, via §1.2) + absent-`CollisionFoleySet` still OPEN (accessor NULL behaviour not dumped).**

Evidence (`func2/func_0x00561450.txt`, 0x00561450, body to 0x0056159D, single caller 0x00554DE7 in 0x00554C90):
- Open; first `FoleyCollision`; the count from 0x00DC5150 is stored as a **u16** at 0x013C85B4; the array of
  count × 0x14 bytes comes from the allocator at vtable slot `+0x38` of the object passed in (arguments size, 4,
  0, 0) and is stored at 0x013C85B0. Whether that allocator zeroes is not visible.
- Per row: `Name` text via 0x00DABA40 → 0x00462960 → `+0x00`. A missing `Name` gives a NULL text pointer, and
  0x0046FD00 returns 0 for NULL (§1.2) — so **an absent `Name` stores key 0** (the row is not skipped).
- First `CollisionFoleySet` child via 0x00DC4FF0 (not iterated — confirmed). `Wwise_switch` text via 0x00DABA10 →
  0x00462960 → `+0x10`. `MinimumSpeed` via the "always" `f32` reader 0x00DACCB0 → `+0x04`, then the stored float
  is widened to double, multiplied by the qword at 0x01119DF8 (the decompiler folds it as `0.44704`; the low dword
  0xA0000000 is consistent with a single-precision constant widened), narrowed back to float and stored
  (0x0056150D–0x00561527). `MaximumSpeed` identically → `+0x08`. **`Frequency` via 0x00DABDF0 — the "always"
  `u32` reader** → `+0x0C` (0x00561563), not the "if present" 0x00DABE80 the spec states. Stride 0x14. Document
  closed at the end.
- The literal for `MinimumSpeed` is rendered `?MinimumSpeed` by the dump's label printer at 0x01119E00, but the
  dword at that address is the bytes `Mini` and the decompiler passes `MinimumSpeed` — a display artefact, not a
  different element name.
- When `CollisionFoleySet` is absent, 0x00DC4FF0 returns NULL and the three accessors are handed a NULL node;
  their behaviour on NULL is not in these dumps (`+0x10` becomes 0 only if 0x00DABA10 returns NULL for a NULL
  node — not read).
- The count/array are read by 0x00561370 (the spec's resolver), 0x005613B0 and 0x00561410 (globals section).

Spec text changes (§8):
- Table row `Frequency`: strike `u32`, "if present" (`FUN_00DABE80`) → *`u32`, **"always"** (`FUN_00DABDF0`),
  written unconditionally (2026-10-01, disassembly — CORRECTED)*.
- Table rows `MinimumSpeed`/`MaximumSpeed`: append *"— the multiply is done in double precision on the stored
  float and the result narrowed back to `f32` (2026-10-01, disassembly)"*.
- Strike the `[OPEN — desk review 2026-09-30: the value left at +0x0C when the "if present" Frequency is absent
  …]` note; replace with: *"**[2026-10-01, re-derived (job 20261001T123123-team-a-ytgi): `Frequency` is read by
  the "always" reader, so an absent element gives that reader's missing-child result rather than an untouched
  slot; an absent `Name` stores key 0 (the Wwise id function returns 0 for NULL, §1.2) and the row is kept; the
  ×0.44704 conversion is a double-precision multiply narrowed to `f32`. CONFIRMED — disassembly. Still OPEN: the
  three accessors' behaviour when `CollisionFoleySet` itself is absent (NULL node), and whether the allocator
  zeroes the array.]**"*
- New Review status: **Review status (2026-10-01): CORRECTED (`Frequency` "always") and CONFIRMED — disassembly
  (layout, stride, first-child-only, double-precision conversion, absent `Name` → key 0) — cleared for
  implementation except the absent-`CollisionFoleySet` accessor behaviour (OPEN) — re-derived from the executable
  (job 20261001T123123-team-a-ytgi).**

Next dump: func 0x00DABA10, func 0x00DABA40 (the two text accessors — NULL-node behaviour and "always"/"if
present" flavour; they decide the absent-child cases in §8, §9, §2 and §10).

---

## §6 `audio_personas.xtbl` — `FUN_00EA48B0` case sensitivity, full vs truncated `Name`, `+0x2C` width, dedup path

**Verdict: CONFIRMED (case-sensitive substring search; searches the full row text, not the 0x20 copy; `+0x2C` is
a `u16`; dedup by `wwise_id` is a linear scan) + CORRECTED (`wwise_id` uses the "always" reader; the 300-cap
rejection happens in the fill helper and the loader keeps walking; the DLC-vs-base selection is a sign test) +
absent-`wwise_id` collapse still OPEN (depends on the "always" reader's missing-child result).**

Evidence — `func8/func_0x00ea48b0.txt` (0x00EA48B0, body 0x00EA48B0–0x00EA5AE4, no callees, 30+ callers across
the image): takes (haystack, needle). Empty needle → returns the haystack. One-character needle → a
character-scan with the usual four-bytes-at-a-time trick. Otherwise a plain substring search: the first needle
byte is scanned for, the second compared, then the remainder byte by byte (0x00EA48C8–0x00EA491B). **Every compare
is an exact byte compare**; there is no case folding, no table lookup, no locale call. This is the C runtime's
`strstr`. **Case-sensitive — CONFIRMED — disassembly.** So `_wm` or `young` in a persona name would not match.

Evidence — `func4/func_0x00709f40.txt` (the record fill, 0x00709F40, body to 0x0070A0F2, single caller
0x0070B1E8):
- Takes (row `Name` text pointer, parsed `wwise_id`). If the count at 0x01504488 is ≥ 300 (unsigned compare at
  0x00709F50) it returns NULL without touching anything.
- Record = base 0x01504094 + count × 0x30. Before the copy it writes: **`+0x2C` (u16) := 0** (a word store at
  0x00709F71), `+0x00` byte := 0, `+0x20` := 0, `+0x24` := 0xFF, `+0x26` (u16) := 0xFFFF, `+0x28`/`+0x29`/`+0x2A`
  := 0. **Not written: `+0x25`, `+0x2B`, `+0x2E`, `+0x2F`** (padding or consumer-side).
- Bounded copy 0x00DA7930(record, name, 0x20) into `+0x00`; `+0x20` := wwise_id.
- The ethnicity/gender search runs on **the row's full `Name` text** (the caller's pointer, pushed at
  0x00709F99), not the 0x20-byte copy: first `strstr(name, "_")` (literal 0x0129EE18); if an underscore exists,
  the eight suffix literals are searched **from that first underscore onward**, in the order `_WM` (0x0113FA14),
  `_WF`, `_BM`, `_BF`, `_HM`, `_HF`, `_AM`, `_AF` (0x0113F9F8) — the dump shows each literal's bytes, e.g.
  0x004D575F = `_WM`. Since every token begins with `_`, starting at the first underscore is equivalent to
  searching the whole name. Outcomes: `_WM` → `+0x29` 1, `+0x28` 1; `_WF` → 1, 2; `_BM` → 2, 1; `_BF` → 2, 2;
  `_HM` → 3, 1; `_HF` → 3, 2; `_AM` → 4, 1; `_AF` → 4, 2; first match wins. Confirms the spec's table.
- Age: `strstr(full name, "Young")` → `+0x2A` 1; else `Middle` → 2; else `Elderly` → 3; first match wins. The
  count 0x01504488 is incremented on every path that returns a record.

Evidence — `func4/func_0x0070b090.txt` (the loader, 0x0070B090, body to 0x0070B235, single caller 0x0070B9B3 in
0x0070B980):
- The argument is a signed int; the test is **`≥ 0` → `dlc%d_audio_personas.xtbl`, negative → plain
  `audio_personas.xtbl`** (0x0070B0A4 sets the flag with a signed "greater-or-equal"; the plain branch formats
  `%s`). So the base-game call passes a negative index; this differs from the banks loader's `> 0` rule (§2) —
  a 0 would select `dlc0_`. The caller's actual argument values are not in this job.
- Existence check 0x00DA90D0 → missing file returns 1 (not an error). Open; the `Audio_Persona` child count must be
  non-zero or the function returns 0.
- Base-game path only: allocate 0x3840 bytes (= 300 × 0x30) → 0x01504094; allocate a second 0x1130-byte block
  → 0x01504088 and initialise it with 0x0070ACB0 (that block is read by about thirty other functions — a
  separate per-persona structure, not traced here); 0x01504090 := 0. **The count 0x01504488 is not reset here**;
  the globals section shows it is zeroed by the caller 0x0070B980 (0x0070B99F).
- Per row: `Name` text via 0x00DABA10; **`wwise_id` via 0x00DABDF0 — the "always" `u32` reader** into a stack
  dword (0x0070B1B8), not the "if present" 0x00DABE80 the spec states. Then a linear scan over records 0 …
  count−1 at stride 0x30 comparing each `+0x20` with the parsed id; a hit skips to the next row (0x0070B1DC →
  0x0070B1F0). Otherwise 0x00709F40(name, id) — **its NULL return at the 300 cap is ignored; the loader goes on
  to the next row and returns 1 at the end** (it does not "fail, returning null").
- After the walk: close; on the base-game path 0x0150448C := the count (a base-row-count marker). Return 1.
- Absent `wwise_id`: the "always" reader's missing-child result is what gets compared and stored; if that
  result is 0 every such row after the first would collapse onto the first id-0 record. Whether it is 0 is not in
  these dumps (0x00DABDF0 not dumped) — OPEN, narrowed to that one callee.
- A note on the accessor mapping: the "always"/"if present" labels for 0x00DABDF0/0x00DABE80 are the spec's
  own §1.2 catalogue, and the calling pattern seen in this job agrees with it (callers of 0x00DABE80 prime the
  destination first — e.g. `ram_size_pc` in §2; callers of 0x00DABDF0 never do). The CORRECTED verdicts on
  `wwise_id`/`Frequency` here, in §5, §8 and §9 rest on that mapping.

Spec text changes (§6):
- First paragraph: strike "(the loader fails, returning null, once the count exceeds 299)" → *"(the fill helper
  `FUN_00709F40` returns NULL once the count reaches 300; the loader ignores that and keeps walking the remaining
  rows, returning 1 — 2026-10-01, disassembly — CORRECTED)"*; strike "(DLC-aware: `dlc%d_audio_personas.xtbl` or
  plain)" → *"(DLC-aware: a **non-negative** index argument selects `dlc%d_audio_personas.xtbl`, a negative one
  the plain file — a sign test, unlike the banks loader's `> 0`; 2026-10-01, disassembly)"*.
- Second paragraph: strike `an "if present" wwise_id (u32)` → *`wwise_id` (`u32`, **"always"**, `FUN_00DABDF0`;
  2026-10-01, disassembly — CORRECTED)*.
- Table: row `+0x2C` → *`+0x2C` | `u16`, zeroed by the fill (not read from any element) | — | `0`*; add row
  *`+0x25`, `+0x2B`, `+0x2E`–`+0x2F` | not written by the fill helper (padding or consumer-side; 2026-10-01,
  disassembly)*.
- Strike "(via a helper, `FUN_00EA48B0`, behaving as a plain substring search — its own body was not
  independently decompiled, only its call pattern)" → *"(via `FUN_00EA48B0`, the C runtime's `strstr`: byte-exact,
  **case-sensitive**, body read 2026-10-01)"*; after "searched only after first confirming the name contains an
  underscore at all" append *"— the search runs on the row's full `Name` text, not the 0x20-byte copy, starting at
  the first underscore (equivalent to the whole name since each token begins with `_`)"*.
- Strike the `[OPEN — desk review 2026-09-30: the CONFIRMED label covers FUN_00709F40's body only …]` note;
  replace with: *"**[2026-10-01, re-derived (job 20261001T123123-team-a-ytgi): CONFIRMED — disassembly:
  `FUN_00EA48B0` is case-sensitive and is given the full `Name` text; `+0x2C` is a `u16`; `+0x25`, `+0x2B`,
  `+0x2E`–`+0x2F` are never written; dedup is a linear scan on `+0x20`. Still OPEN: what the "always" `u32`
  reader leaves for a missing `wwise_id` (callee `0x00DABDF0` not dumped) — if 0, all such rows after the first
  collapse onto one record.]**"*
- New Review status: **Review status (2026-10-01): CONFIRMED — disassembly (`strstr` case-sensitive, full-name
  search, `+0x2C` u16, linear dedup, cap behaviour) and CORRECTED (`wwise_id` "always"; cap handled in the fill
  helper; sign-test DLC selection) — not cleared: the absent-`wwise_id` value depends on `0x00DABDF0` (OPEN) —
  re-derived from the executable (job 20261001T123123-team-a-ytgi).**

Next dump: func 0x00DABDF0 (the "always" `u32` reader — missing-child result; also settles the §5/§8/§9
absent-element cases), func 0x00DABE80 (its "if present" twin, to confirm the catalogue mapping),
func 0x0070B980 (the caller: the actual index values passed for base and DLC loads).

---

## §7 `persona_radio_prefs.xtbl` — the `Radio_Station` matching rule and `FUN_00709EB0`

**Verdict: CORRECTED (the match is against each station's `+0x140` Wwise id — the hash of its `wwise_id`
element, §11.2 — not against `Genre`; the stored byte is the station index **plus one**) + CONFIRMED (persona
lookup is a case-insensitive linear name scan; unresolved station → 0xFF; unresolved persona → row skipped).**

Evidence — `func2/func_0x0055dd50.txt` (0x0055DD50, body to 0x0055DD98, five callers incl. 0x0070A165): the text
is hashed by 0x00462960 (§1.2); a 0 hash returns −1 at once. Otherwise, with the station count at 0x013C58DC
(signed, must be > 0) and the station array at 0x013C58CC, it compares the dword at **station `+0x140`** of
stations 0, 1, 2 … (stride 0x690) with the hash; on a hit at index *i* it returns **the byte `i + 1`**
(0x0055DD93–0x0055DD95); no hit → **−1 (0xFF)**. No normalisation, no lookup table, no `Genre` involvement.
CONFIRMED — disassembly.
- Consequence: the `Radio_Station` token must be, after Wwise hashing, equal to the hash the station loader
  stored at `+0x140` from the station's own `wwise_id` element (§11.2). The spec's observed correspondence with
  `Genre` stems is incidental. Whether the Wwise hash is case-insensitive is the §1.2 OPEN (SDK function).
- The scan starts at station 0 (the mix-tape slot); whatever §11's station-0 initialisation leaves at its
  `+0x140` is compared too.

Evidence — `func4/func_0x00709eb0.txt` (0x00709EB0, body to 0x00709EF0, single caller 0x0070A156): linear scan
over the persona records (count 0x01504488, base 0x01504094, stride 0x30), **`_stricmp`** of the record's
`+0x00` name against the query; returns the first record that matches, else NULL (also NULL when the base
pointer is NULL). Case-insensitive, exact-length match against the stored (0x20-bounded) copy. CONFIRMED.

Evidence — `func4/func_0x0070a100.txt` (0x0070A100, body to 0x0070A192, single caller 0x0070B9B8): `Name` via
0x00DABA10, `Radio_Station` via 0x00DABA40; if either is NULL the row is skipped; 0x00709EB0 on the name — NULL
→ **row skipped, nothing written**; else the byte from 0x0055DD50 is stored at persona `+0x24`. So an
unresolvable `Radio_Station` stores 0xFF, indistinguishable from the fill's default 0xFF.

Spec text changes (§7):
- First paragraph: strike "and `Radio_Station`'s text to a station index (`FUN_0055DD50`, not independently
  decompiled — its return value is stored as a single byte)" → *"and `Radio_Station`'s text to a station index
  (`FUN_0055DD50`: the text is Wwise-hashed (`FUN_00462960`) and compared with each station's `+0x140` id — the
  hash of that station's own `wwise_id` element, §11.2 — in index order from station 0; the byte stored is
  **index + 1**, or 0xFF when nothing matches or the hash is 0; 2026-10-01, disassembly — CORRECTED)"*; strike
  "(default `0xFF`, presumably "no station preference")" → *"(default 0xFF; an unresolved `Radio_Station` also
  stores 0xFF, and a `Name` matching no persona (case-insensitive `_stricmp` over the records, `FUN_00709EB0`)
  skips the row)"*.
- Second paragraph: strike from "**Radio_Station's exact matching rule against `radio_stations.xtbl` was not
  traced**" through "…(§11 — stations have no `Name` element in this loader's own reads)." → *"**The matching
  rule is a Wwise-id equality with the station's `wwise_id` element** (2026-10-01, disassembly of
  `FUN_0055DD50`); the visible correspondence with `Genre` stems is incidental. The real-data check that each
  `Radio_Station` value equals a station's `wwise_id` text is recorded in §7.1."*
- Strike both bracketed notes at the end of that paragraph (`[HIGH CONFIDENCE — the correspondence pattern …]`
  and `[OPEN — desk review 2026-09-30: the listed pairs are not a uniform stem match …]`); replace with:
  *"**[2026-10-01, re-derived (job 20261001T123123-team-a-ytgi): CONFIRMED — disassembly for the rule, the
  index-plus-one encoding, the 0xFF outcomes and the row-skip on an unknown persona.]**"*
- §20 item 5: mark resolved by this note.
- New Review status: **Review status (2026-10-01): CORRECTED (`Radio_Station` resolves by Wwise id against
  station `+0x140`, stored as index + 1) and CONFIRMED — disassembly (`FUN_00709EB0` case-insensitive scan; 0xFF
  and skip behaviours) — cleared for implementation — re-derived from the executable (job
  20261001T123123-team-a-ytgi).**

Team B relevance: the stored byte is index + 1 (1 = station 0, the mix tape), and resolution is by the station's
`wwise_id` text, not `Genre` — a reimplementation that resolves by `Genre` stem will mis-assign.

---

## §10 `foley_engine.xtbl` — pointer-array capacity and the vehicle `+0x7AC` index-vs-hash question

**Verdict: CONFIRMED (per-row record, fields, seed 0 / no limit, no bound check, indices run on across DLC loads)
+ capacity narrowed to HIGH CONFIDENCE 128 slots (address-layout inference, not read) + the vehicle-side
consumer still OPEN, but the pointer array's readers are now identified and all take an **index** bounds-checked
against the counter.**

Evidence — `func2/func_0x005591b0.txt` (the loader, 0x005591B0, body to 0x005592EB, single caller 0x00554DD8):
- Signed int argument; **`≥ 0` → `dlc%d_foley_engine.xtbl`** with the DLC allocation pool 0x01495500 (its
  virtual "begin" is called and, when it answers true, 0x01495504/0x01495508 are reset), **negative → plain
  `foley_engine.xtbl`** with the main pool 0x01495410 — the same sign convention as the personas loader (§6).
- Existence check, open, then for each `Engine` row: 0x005590C0 is called with the pool in ECX and the **u16
  counter at 0x013BBAA0** (sign-extended) as the index; the counter is then incremented as a word (0x0055927F).
  **There is no compare against any limit anywhere in the loop** — confirmed. The counter is never reset here,
  so indices continue across the base load and each DLC load.
- After the walk: base-game path → the u16 at 0x013BBAA4 := the counter (a base-row-count marker); DLC path →
  0x01495504 := 1 and 0x01495508 := the dword at 0x01495530 (pool restore).

Evidence — `func1/func_0x005590c0.txt` (the row fill, 0x005590C0, body to 0x005591A9, single caller
0x0055927A; the row node arrives in EDI, the pool in ECX, the index on the stack):
- Record pointer = dword at 0x013BB8A0[index]; **if NULL, a 0x10-byte block is allocated through the pool's
  vtable slot `+0x38` (0x10, 4, 0, 0) and stored in that slot; if already non-NULL the existing block is reused**
  (a reload path). The 16 bytes are then zeroed.
- `Name` via 0x00DABA10. `+0x08` := 0, then `Vehicle_Model` via 0x00DABA40 → if non-NULL, 0x00462960 → `+0x08`.
  `+0x00` := 0x00D9E8B0(`Name`, seed 0, limit 0xFFFFFFFF) — seed and no-limit confirmed at 0x0055913D–0x00559146
  (the lower-casing itself is that callee's business, not in this job). `+0x04` := 0x00462960 of `Veh_%s`
  formatted with `Name` into a 0x80-byte stack buffer. `+0x0C` byte := 0 then the "if present" bool reader
  0x00DAC510 for `NPC_Only`. `+0x0D` byte := 0xFF then the "if present" `s8` reader 0x00DAC3B0 for
  `dlc_framework_id`. `+0x0E`–`+0x0F`: zero from the block clear, never written. All as the spec's table.
- Capacity: nothing in either function bounds the index. The pointer array begins at 0x013BB8A0 and the counter
  sits at 0x013BBAA0, exactly 0x200 bytes later; if no other symbol lies between (the dumps show none, but they
  only list referenced globals), that is **128 pointer slots** — enough for 87 merged rows. HIGH CONFIDENCE by
  layout, not read.

Evidence — `glob/xref_0x013bb8a0.txt` and `glob/xref_0x013bbaa0.txt`: besides the fill, the pointer array is read
by **0x00558FD0, 0x00559010, 0x00559040, 0x00559070, 0x005592F0, 0x005593C0, 0x005594E0**, and every one of those
seven also compares a value against the u16 counter at 0x013BBAA0 — the shape of "take an index, bounds-check it
against the count, dereference the slot". Two of them, 0x005592F0 and 0x005594E0, are also in the caller list of
the Wwise hash wrapper 0x00462960 (its dump's header, 0x00559384 and 0x00559573) — the shape of a **name → index
resolver** (hash the query, scan the array). None of the seven is dumped, and the vehicle-side reader of `+0x7AC`
is outside this index, so the index-vs-hash question is **narrowed, not closed**: every consumer of the table
indexes it by position, and a by-name resolver returning a position exists in the same code cluster.

Spec text changes (§10):
- First paragraph: strike "No bound check on this pointer array was found in the traced code (**OPEN** — exact
  capacity unknown)." → *"No bound check exists in the loader or the fill (2026-10-01, disassembly: the u16
  counter at `0x013BBAA0` indexes the pointer array directly). The array's room is the 0x200 bytes between
  `0x013BB8A0` and the counter — 128 slots — if no other symbol sits between them (HIGH CONFIDENCE by layout,
  not read; a range dump settles it). The counter is never reset, so indices run on across base and DLC loads;
  after the base load the u16 at `0x013BBAA4` records the base count. A non-NULL slot is reused rather than
  re-allocated (a reload path). DLC selection is a sign test on the index argument (≥ 0 → `dlcN_`, with the DLC
  allocation pool `0x01495500`; negative → plain, main pool `0x01495410`)."*
- Table row `+0x0D`: append *"`+0x0E`–`+0x0F` are zero padding (2026-10-01, disassembly)"*.
- Strike the `[OPEN — desk review 2026-09-30: spec-vehicle-data.md §7.3/§7.4 describe +0x7AC as a "u16 sound
  id" …]` note; replace with: *"**[2026-10-01, re-derived (job 20261001T123123-team-a-ytgi): the pointer array
  is read by seven functions (`0x00558FD0`, `0x00559010`, `0x00559040`, `0x00559070`, `0x005592F0`,
  `0x005593C0`, `0x005594E0`), each of which bounds-checks an index against the u16 counter at `0x013BBAA0`;
  two of them (`0x005592F0`, `0x005594E0`) also call the Wwise hash, i.e. they resolve a name to a position. So
  every consumer addresses this table by index, and a by-name → index resolver exists; whether the vehicle
  loader stores `+0x7AC` from such a resolver, and under which name (`Veh_` prefixed or not), remains OPEN on
  the vehicle side — none of the seven is dumped.]**"*
- New Review status: **Review status (2026-10-01): CONFIRMED — disassembly (record layout, seed 0 / no limit,
  no bound check, run-on indices, pool selection) — not cleared: capacity is HIGH CONFIDENCE 128 by layout, and
  the vehicle `+0x7AC` consumer is narrowed (index-addressed table, resolver candidates `0x005592F0` /
  `0x005594E0`) but unread — re-derived from the executable (job 20261001T123123-team-a-ytgi).**

Next dump: range 0x013BB8A0-0x013BBAA8 (anything else between the array and the counter?), func 0x005592F0,
func 0x005594E0 (the name → index resolvers), func 0x005593C0, func 0x00558FD0 (one field getter, to learn
which record field each getter returns).

---

### §7 addendum — real-data check of the corrected rule (CONFIRMED — empirical)

Read from the extracted base-game copies in `tools/ao_xtbl/` (read-only): `radio_stations.xtbl` carries nine
`wwise_id` values — `OFF`, `GENX`, `KRHYME`, `K12`, `KLASSIC`, `ADULT_SWIM`, `KABRON`, `KRUNCH`, `THE_MIX` (the
`Newsbreaks` and `End Credits` stations have none). `persona_radio_prefs.xtbl`'s distinct `Radio_Station`
values are exactly those nine tokens: `THE_MIX` 46, `KRHYME` 45, `K12` 30, `KRUNCH` 24, `KLASSIC` 24, `KABRON`
22, `GENX` 18, `ADULT_SWIM` 18, `OFF` 15 — total 242 = every row. So **242/242 rows resolve by exact
`wwise_id` equality**, with no stem matching needed (`GENX`, `K12` are literal `wwise_id` texts). Spec §7.1:
add *"`OFF` (15)"* to the value list and the sentence *"every one of the nine distinct `Radio_Station` values is
literally a `wwise_id` value of `radio_stations.xtbl` (2026-10-01, data check)"*.

---

## §11 `radio_stations.xtbl` — station-0 initialisation, `+0x689` bit 5, absent `wwise_id`, `FUN_0055DDC0`

**Verdict: CORRECTED (station 0 gets only bit 3 from the loader — bit 5 is cleared, not set; the "Mix Tape"
string is written to station `+0x00` at runtime, a different field from `+0x40`'s `"My Radio 85.5"`) +
CONFLICT with `spec-save-format.md` §12.8.1 RESOLVED (bit 5 on station 0 is maintained by the mix-tape list
setter as "list non-empty"; on stations 1..N it is half of `Selectable`'s `0x60`; the enumerator tests bit 6)
+ CONFIRMED (absent `wwise_id` → `+0x140` = 0; every other field of §11.2) + new facts on `FUN_0055DDC0`.**

Evidence — `func2/func_0x0055e760.txt` (the loader, 0x0055E760, body to 0x0055EC07, single caller 0x0055F2C5
in 0x0055F270):
- Open; first `NewEntity`; the three flag counters 0x013C58D8/0x013C58D4/0x013C58D0 := 0 and 0x013BBAB0 := 1.
  `Simultaneous_NPC_Radios` present → its text goes through 0x00EA47E1 (an address in the C runtime block next
  to `strncmp`; not dumped — HIGH CONFIDENCE the CRT integer parser) → 0x013BBAB0.
- `Radio_Station_List` absent → close and return with the count unchanged. Otherwise N = number of `Info`
  children; **the count 0x013C58DC is zeroed for the duration of the build**; (N+1) × 0x690 bytes are allocated
  on the main pool 0x01495410 (0x00DAD460) → 0x013C58CC; NULL → return (count left at 0); otherwise the whole
  block is **cleared with `memset`**.
- Station 0 (0x0055E842–0x0055EA42): `+0x40` := `My Radio 85.5` (bounded copy, 0x40); **`+0x689` &= 0xDF
  (bit 5 cleared — a no-op after the clear)**; `+0x144`/`+0x145`/`+0x146` := 0; `+0x674` := −1; `+0x5A8`,
  `+0x678`, `+0x67C`, `+0x680`, `+0x684` := 0; then, bracketed by 0x00D9F620/0x00D9F630 on the object
  0x013C6110 (an enter/leave pair), eight 0x18-byte blocks at `+0x5B0` … `+0x668` are each filled from the same
  four sources (dword 0x011199C0, dword 0x011199C4 — both static zeros per the decompiler — qword 0x029CDB98,
  dword 0x029CDBA0 — runtime globals); finally **`+0x689` |= 0x08 (bit 3)**. **No instruction sets bit 5 or bit
  6 of station 0 here.** The spec's "bit 5 cleared then set (bits 3/5 … set directly)" is wrong: only bit 3 is
  set.
- Stations 1..N (0x0055EA80–0x0055EBE3), per `Info` child: `xtbl_name` → `Filename` text (0x00DABA10) →
  bounded copy 0x00DA78D0(station `+0xC0`, 0x40, text); `Genre` → its node text → 0x00DA78D0(`+0x100`, 0x40,
  text); `Station_flags` present → `+0x689` &= 0x9F, `+0x68A` &= 0xF1, then each `Flag` child's text is
  `_stricmp`-ed against all four literals in turn: `Selectable` → `+0x689` |= 0x60 and 0x013C58D8++;
  `Police_Station` → `+0x68A` |= 0x02 and 0x013C58D4++; `FBI_Station` → `+0x68A` |= 0x04 and 0x013C58D0++;
  `News_Station` → `+0x68A` |= 0x08. `wwise_id` present → node text → 0x00462960 → `+0x140`; **absent → `+0x140`
  keeps the memset's 0**. Nothing else is written for stations 1..N — no `+0x40` name, confirmed.
- After the walk the count 0x013C58DC := N+1; close.

Evidence — `func2/func_0x0055e380.txt` (0x0055E380, body to 0x0055E46A, callers 0x0083E7F6/0x0083E81B in
0x0083E6F0 — a script-binding-shaped caller): takes (u16 count, pointer to a u16 list); count > 0x200 (512) →
returns 0. Otherwise, on **station 0** (it always uses the array base): for each of the old `+0x5A4` entries,
the u16 at song-table record [`+0x1A4`[i]] `+0x0C` (0x013BBC24 = 0x013BBC18 + 0xC, stride 20) := 0xFFFF; then
for i < count: `+0x1A4`[i] := list[i], song record [list[i]] `+0x0C` := i, and 0x00BDA2C0(song record `+0x00`
dword) is called (not dumped); then `+0x5A4` := count, 0x013C60E0 := count, and if the station count is ≥ 1 and
`+0x5A4` ≠ 0, the cursor `+0x5A6` := 0; finally **bit 5 of `+0x689` := (count ≠ 0)** (0x0055E450–0x0055E462:
the bit is set equal to the "count above zero" flag through an XOR mask). So **bit 5 on station 0 means "the
mix-tape list is non-empty"** — exactly `spec-save-format.md` §12.8.1's reading — and it is maintained here,
not by the loader. The inline list `+0x1A4` (u16 song-table indices), its count `+0x5A4` and cursor `+0x5A6`
are confirmed as station 0's own fields; the song table at 0x013BBC18 has a u16 at `+0x0C` holding the record's
mix-tape slot or 0xFFFF (new, for §12/§20 item 3).

Evidence — `func2/func_0x0055db40.txt` (0x0055DB40, body to 0x0055DBDC, three callers in 0x0055FD90): takes a
station pointer; advances the cursor `+0x5A6`; when it reaches `+0x5A4` (or `+0x68A` bit 4 is set) it wraps: if
`+0x689` bit 3 is set, `+0x19C` (u16) := `+0x5A4`, else 0x0055D7D0(station) (not dumped); cursor := 0; and
**only then, only if the global byte 0x013C60E4 is non-zero and the station's `+0x00` byte is 0, the nine
bytes `Mix Tape` are copied to station `+0x00`** (0x0055DB97–0x0055DBA9). Returns a pointer to song record
[`+0x1A4`[cursor]] (stride 20 from 0x013BBC18) or NULL. So `"Mix Tape"` and `"My Radio 85.5"` live in **two
different fields** (`+0x00` runtime-conditional, `+0x40` loader) — the two documents are not in conflict.

Evidence — `func2/func_0x0055ddc0.txt` (0x0055DDC0, body to 0x0055DEA3, callers 0x0083CC40, 0x0083E230 ×2):
takes (ordinal n, 16-byte output). Walks stations 0..count−1 and counts those with **`+0x689` bit 6 (0x40)
set**, `+0x68A` & 0x06 == 0 (neither Police nor FBI), and whose **`+0x40` text does not begin with the first 8
characters of `RADIO_OFF`** (`strncmp` with length 8 on `+0x40` — the name field, not `Genre`). For the n-th
such station (0-based) it fills: `+0x00` → pointer to station `+0x40`, `+0x04` → pointer to `+0x100` (`Genre`),
`+0x08` → pointer to `+0x80` (a string field this loader never writes), `+0x0C` byte := bit 6 of `+0x689`,
`+0x0E` byte := station index + 1 (the division by 0x690 at 0x0055DE78–0x0055DE8B), `+0x0F` byte := bit 7 of
`+0x689`; returns 1, else 0. **The enumerator keys on bit 6, not bit 5**; and since the loader never sets bit 6
on station 0, the mix tape appears in this list only if something else sets it (not in these dumps). Note also
that `+0x40` and `+0x80` of stations 1..N must be filled by another loader — 0x0055C9F0 (pushes the `Info`
literal 24 times and references the song table 0x013BBC18 and the main pool; seen in the globals sections) is
the obvious candidate: HYPOTHESIS, not dumped.

Spec text changes (§11):
- §11.1, station-0 bullet: strike "`+0x689` bit 5 cleared then set (bits 3/5 of the flag byte set directly,
  matching `spec-save-format.md` §12.8.1's identification of record 0 as **"Mix Tape"**)" → *"`+0x689` bit 5
  cleared (a no-op after the block-wide `memset`) and **bit 3 set** — the loader sets no other bit on station 0;
  bit 5 is set and cleared later by the mix-tape list setter `0x0055E380` to mean "the list is non-empty", and
  the runtime label `"Mix Tape"` is written to station **`+0x00`** (not `+0x40`) by `0x0055DB40` when the
  global byte `0x013C60E4` is set and `+0x00` is still empty — so `"My Radio 85.5"` (`+0x40`) and `"Mix Tape"`
  (`+0x00`) are two different fields (2026-10-01, disassembly — CORRECTED)"*; strike "(EQ-like constants at
  `+0x5B0`–`+0x668`, not xtbl-derived, not decoded further here …)" → *"(eight 0x18-byte blocks at
  `+0x5B0`–`+0x668`, each filled from the same four constants — two static zeros at `0x011199C0`/`0x011199C4`
  and two runtime globals `0x029CDB98`/`0x029CDBA0` — inside an enter/leave pair on `0x013C6110`; not
  xtbl-derived, out of scope)"*.
- Strike the whole `[OPEN — desk review 2026-09-30, conflict with spec-save-format.md §12.8.1 …]` note; replace
  with: *"**[2026-10-01, re-derived (job 20261001T123123-team-a-ytgi): no conflict. Bit 5 of `+0x689` has one
  meaning per station class: on station 0 it is "mix-tape list non-empty", written by `0x0055E380` (count ≠ 0 →
  set, 0 → cleared); on stations 1..N it is set together with bit 6 by `Selectable`. The selectable-station
  enumerator `FUN_0055DDC0` tests **bit 6**, not 5. Record 0's two names are two fields. CONFIRMED —
  disassembly. Still OPEN: who sets bit 6 on station 0 (nothing in these dumps), and `0x0055F580` (not dumped).]**"*
- §11.1, `Simultaneous_NPC_Radios` bullet: append *"(`0x00EA47E1` sits in the C runtime block beside
  `strncmp`; HIGH CONFIDENCE it is the CRT integer parser; the default 1 is written before the lookup; 2026-10-01)"*.
- §11.2 table, `Genre` row: append *"copied with the bounded copy `0x00DA78D0` (dst, max, src), as is
  `Filename` (2026-10-01, disassembly)"*. `Station_flags` row: append *"each `Flag` text is compared against all
  four literals in sequence (not else-if), so a text can never match two"*.
- §11.3: strike the `[OPEN — desk review 2026-09-30: what +0x140 holds for the 2 of 11 rows without wwise_id
  …]` note → *"**[2026-10-01, disassembly: `+0x140` is left at 0 — the whole station block is `memset` to zero
  before the walk and the hash is only written when the element exists. CONFIRMED.]**"*; strike "consistent with
  a hard-coded `"RADIO_OFF"` name string the runtime "selectable station" enumerator (`FUN_0055DDC0`, …)
  explicitly filters out by `strncmp`" → *"the runtime enumerator `FUN_0055DDC0` lists a station only if `+0x689`
  bit 6 is set, neither `Police_Station` nor `FBI_Station` is set, and the station's **`+0x40` name** does not
  begin with the 8 characters `RADIO_OF` (`strncmp`, length 8) — a name field this loader never writes for
  stations 1..N, so it is filled elsewhere (2026-10-01, disassembly)"*.
- §12.2 / §20 item 3 (song table): add *"2026-10-01: the stride-20 song table at `0x013BBC18` has a `u16` at
  `+0x0C` that holds the record's slot in station 0's mix-tape list, or 0xFFFF when not listed (written by
  `0x0055E380`); `+0x00` is a dword handed to `0x00BDA2C0` when a song enters the list."*
- New Review status: **Review status (2026-10-01): CORRECTED (station 0: only bit 3 set by the loader; `"Mix
  Tape"` → `+0x00`, `"My Radio 85.5"` → `+0x40`) and CONFIRMED — disassembly (bit-5 semantics per station class,
  resolving the §12.8.1 overlap; enumerator keys on bit 6; absent `wwise_id` → 0; all §11.2 fields) — not
  cleared: writer of station-0 bit 6 and `0x0055F580` unread; the stations-1..N `+0x40`/`+0x80` name writer
  (candidate `0x0055C9F0`) unread — re-derived from the executable (job 20261001T123123-team-a-ytgi).**

Team B relevance: the mix-tape "has songs" bit and the `Selectable` bits share bit 5 on different stations; an
implementation must not treat bit 5 uniformly. The enumerator's `RADIO_OF` test is on the name at `+0x40`.

Next dump: func 0x0055F580, func 0x0055C9F0 (per-station file loader — `+0x40`/`+0x80` names, song table
population), func 0x0055F270 (the loader's caller: load order and what else it initialises), func 0x0055D7D0.

---

## §12 `playlist_artist_track.xtbl` — `+0x190` vs `+0x5A4`, station-0 `+0x194`, and the join chain

**Verdict: CONFIRMED (loader shape, 145 cap, catalog layout, the `0x0083E230` join chain, the roles of
`+0x190` / `+0x194` / `+0x5A4`) + CORRECTED (`WWise_ID` uses the "always" reader; the radio accessors take a
**1-based** station number, which is why `+0x690 − 0x500` lands on `+0x190`) + station-0 `+0x194` still OPEN
(no dump writes it), but the station-0 list is confirmed inline at `+0x1A4` with count `+0x5A4` (§11).**

Evidence — `func5/func_0x0083e0b0.txt` (0x0083E0B0, body to 0x0083E1AF, single caller 0x0083E5EE in
0x0083E5C0): `Track_Listing` → `Tracks` → `Track` rows; the loop runs while a row exists **and** the count at
0x02317230 is < 0x91 (145) — a row beyond that is simply not visited. Per row: **`WWise_ID` via 0x00DABDF0 (the
"always" `u32` reader)** straight into entry `+0x00` (0x0083E121) — not "if present"; `Artist_Name` text via
0x00DABA10 → 0x00DB12C0 with ECX = the object 0x023172C0 and two zero arguments → `+0x04`; `Track_Name` likewise
→ `+0x08`; count++. Stride 0xC from 0x02316B60. The count is not reset here (its other writer is not in this
job). Catalog layout and cap: CONFIRMED.

Evidence — `func2/func_0x0055df50.txt` (0x0055DF50): one byte argument k; returns 0 unless 0 ≤ k < station
count; otherwise returns the **s16 at `base + k × 0x690 − 0x500`**, i.e. **`+0x190` of station k−1**. So the
argument is a 1-based station number, and because the test is `k < count`, the last station (number = count) is
never reachable through it.
Evidence — `func2/func_0x0055dfb0.txt` (0x0055DFB0): arguments (slot, k); returns NULL unless 0 ≤ k < count and
slot ≥ 0 and **slot ≤ the u16 at station(k−1) `+0x5A4`** (0x690 − 0xEC; the compare at 0x0055DFE0 rejects only
slot > bound — an inclusive test, so slot == count passes and reads one entry past the list); then returns
song-table record 0x013BBC18 + 20 × (s16 at (pointer at station(k−1) `+0x194`)[slot]). So `+0x194` is a pointer
to a u16 index array and `+0x5A4` its (inclusively checked) bound — CONFIRMED, with the 1-based k.
Evidence — `func2/func_0x0055ddc0.txt` (§11): its output byte `+0x0E` is station index + 1, and
`func5/func_0x0083e230.txt` passes exactly that byte to both accessors (0x0083E2D7, 0x0083E2F6). The same
1-based numbering is what §7's `0x0055DD50` returns. **The radio API numbers stations from 1; 0 never denotes a
station.**

Evidence — `func5/func_0x0083e230.txt` (0x0083E230, body to 0x0083E5B8; referenced only from a data table at
0x0083E656 — a binding table, consistent with a script function): requires ≥ 1 argument; fetches the script
state through 0x00E1E8C0(0x0231722C)/0x00E1E940; calls 0x0055DDA0 for the number of listable stations (its
body is not dumped, but the globals sections show it computes 0x013C58D8 − 0x013C58D4 − 0x013C58D0, i.e.
`Selectable` count minus `Police_Station` minus `FBI_Station` counts); reads the first argument as an integer
mode via 0x006CB2E0. Mode 0: for n in 0..count−1, 0x0055DDC0(n) and push (name pointer at station `+0x40`,
station number byte). Mode 1: for each such station, k := its number; `+0x190` (via 0x0055DF50(k)) is the loop
bound; for slot i < that bound, 0x0055DFB0(i, k) → song record; if non-NULL, a **linear scan of the catalog**
(count 0x02317230, stride 12) compares catalog `+0x00` with the song record's first dword; on a match it pushes
catalog `+0x04` (artist), catalog `+0x08` (track), the s16 from 0x0055E000(i, k) (not dumped), the station
number, and **the s16 at song record `+0x0C`** — the mix-tape slot index (or 0xFFFF) that §11's 0x0055E380
maintains. CONFIRMED — disassembly for the whole chain.

On the two counts: `+0x190` (s16) is what 0x0055DF50 hands out and what the marshaller iterates; `+0x5A4`
(u16) is the bound 0x0055DFB0 checks the slot against and, for station 0, the count 0x0055E380 writes. 0x0055E380
never touches `+0x190` or `+0x194`; neither does the station loader (§11). So for stations 1..N both fields,
and the `+0x194` pointer, are written by code outside this job (the per-station file loader — candidate
0x0055C9F0, which references the song table). For station 0 the inline list at `+0x1A4` and its count `+0x5A4`
are confirmed (§11); whether `+0x194` points at `+0x1A4` and who sets `+0x190` on station 0 remain OPEN.

Spec text changes (§12):
- First paragraph: strike "`+0x00` `WWise_ID` (`u32`, "if present")" → *"`+0x00` `WWise_ID` (`u32`, **"always"**,
  `FUN_00DABDF0`; 2026-10-01, disassembly — CORRECTED)"*; after "(`DAT_02317230 < 0x91`)" append *"— the check
  precedes each row, so the 146th and later rows are never visited"*.
- §12.2, `FUN_0055DF50` clause: strike "(returns the slot *count*, reading station `+0x690 − 0x500 = +0x190`)" →
  *"(takes a **1-based** station number k — the numbering every radio accessor uses, 0x0055DDC0 reports it and
  0x0055DD50 returns it — and returns the s16 at station k−1's `+0x190`; the `+0x690 − 0x500` form is that
  1-based offset; k = count is rejected, so the last station is unreachable through it; 2026-10-01,
  disassembly)"*; `FUN_0055DFB0` clause: append *"(same 1-based k; the slot test is `slot ≤ +0x5A4`, inclusive —
  an off-by-one that admits one entry past the list; 2026-10-01, disassembly)"*; after "to find the matching
  `Artist_Name`/`Track_Name` for display" append *"— and, pushed with them, the song record's `+0x0C` s16, the
  record's slot in station 0's mix-tape list or 0xFFFF (§11, 0x0055E380)"*.
- Strike the `[OPEN — desk review 2026-09-30: two different station "count" fields are cited …]` note; replace
  with: *"**[2026-10-01, re-derived (job 20261001T123123-team-a-ytgi): `+0x190` (s16) is the slot count the
  marshaller iterates (returned by `0x0055DF50`); `+0x5A4` (u16) is the bound `0x0055DFB0` checks and, on
  station 0, the mix-tape count `0x0055E380` writes; `+0x194` is the u16-array pointer. Neither the station
  loader nor `0x0055E380` writes `+0x190`/`+0x194`; their writer for stations 1..N (and station 0's `+0x194`) is
  outside this job — candidate `0x0055C9F0`. CONFIRMED — disassembly for the roles; OPEN for the writer.]**"*
- New Review status: **Review status (2026-10-01): CONFIRMED — disassembly (loader, cap, catalog layout,
  `0x0083E230` chain, roles of `+0x190`/`+0x194`/`+0x5A4`, 1-based station numbers) and CORRECTED (`WWise_ID`
  "always") — not cleared: station-0 `+0x194`/`+0x190` writer unread (candidate `0x0055C9F0`) — re-derived
  from the executable (job 20261001T123123-team-a-ytgi).**

Next dump: func 0x0055C9F0, func 0x0055E000, func 0x0083E5C0 (the loader's caller — resets the count?),
func 0x0083E200 (the id → track-name lookup, to re-read rather than inherit).

---

## §13 `radio_activities.xtbl` — the denominator `DAT_012A2DD8` and slot addressing

**Verdict: CORRECTED on the denominator (it is the constant double **100.0**, read-only in `.rdata` — the
"reads as 0" was the low dword of 100.0, the exact trap this task flagged) + CORRECTED (`Level`/`Percentage`
use the "always" reader; only the eight `Percentage` floats are pre-zeroed) + CONFIRMED (slots filled in row
order, no bound check, the add-2³² step).**

Evidence — `func3/func_0x0060e790.txt` (0x0060E790, body to 0x0060E87F, reached through a jump thunk at
0x0060E880):
- Before opening the file, **eight single-precision zeros** are stored at 0x014B02A4, 0x014B02AC, …, 0x014B02DC
  — the `Percentage` slots only; the `Level` dwords at 0x014B02A0, 0x014B02A8, … are not touched here.
- `RadioActivities` → `ChancesToPlay` → `ChanceToPlay` rows. Per row, with the slot pointer starting at
  0x014B02A0 and advancing by 8 per row (0x0060E85B) — **row order, not `Level`**, and **no compare against any
  limit** — `Level` via **0x00DABDF0 ("always" `u32`)** into slot `+0x00`; `Percentage` via 0x00DABDF0 into a
  stack dword; that dword is loaded into the x87 unit as a signed integer, the single-precision constant at
  0x012A3078 (raw 0x4F800000 = 2³²) is added if it was negative, then it is **divided by the double at
  0x012A2DD8** and stored as `f32` at slot `+0x04` (0x0060E83C–0x0060E85E). The decompiler folds that constant
  as **`/ 100.0`**; `glob/xref_0x012a2dd8.txt` lists 279 uses in 195 functions, every one a read (`FDIV`,
  `DIVSD`, `MULSD`, `COMISD` …) and none a write, and the block is file-backed `.rdata` — a shared, constant
  100.0 (low dword 0x00000000, which is what the earlier "0 in the static image" reading saw).
- So the stored value is `Percentage ÷ 100` as a fraction (division in x87 precision, rounded to single at the
  store). With the retail values 1, 2, 3, 5, 10, 15, 20, 25 the eight fractions sum to 0.81, not 1.0.

Spec text changes (§13):
- First paragraph: strike "pre-zeros **exactly 8** module-level globals (stride 8 bytes: `Level` `u32` + a
  normalised `Percentage` `f32`)" → *"pre-zeros the eight `Percentage` floats (`0x014B02A4` … `0x014B02DC`,
  stride 8; the `Level` dwords are not pre-written)"*; strike "walking … rows into them at `DAT_014B02A0` —
  **with no bound check in the row-walking loop itself.**" → keep, and append *"Rows fill slots in **row order**
  (the slot pointer advances by 8 per row); `Level` is stored, not used as the index (2026-10-01, disassembly)."*
- Second paragraph: strike "`Level` (`u32`, "if present") and `Percentage` (`u32`, "if present", …" → *"`Level`
  (`u32`, **"always"**, `FUN_00DABDF0`) and `Percentage` (`u32`, **"always"**, …"*; strike "before dividing by a
  denominator global (`DAT_012A2DD8`) that reads as **0** in the static image, meaning its real value is primed
  by code not traced this pass; **OPEN**)" → *"before dividing by the constant double **100.0** at `0x012A2DD8`
  (file-backed `.rdata`, 279 read-only uses across 195 functions, no writer; the earlier "0" reading was the
  zero low dword of 100.0) and storing the quotient as `f32` — i.e. percent → fraction (2026-10-01, disassembly
  — CORRECTED)"*.
- Strike the `[OPEN — desk review 2026-09-30: "normalised" in the lead sentence is not supported …]` note;
  replace with: *"**[2026-10-01, re-derived (job 20261001T123123-team-a-ytgi): denominator 100.0; the retail
  fractions sum to 0.81, so "normalised" means percent → fraction, not "sums to one"; slot = row order.
  CONFIRMED — disassembly.]**"*
- §20 item 7: strike the `DAT_012A2DD8` clause (resolved).
- New Review status: **Review status (2026-10-01): CORRECTED (denominator = constant 100.0; "always" readers;
  only the floats pre-zeroed) and CONFIRMED — disassembly (row-order slots, no bound check, add-2³² step) —
  cleared for implementation — re-derived from the executable (job 20261001T123123-team-a-ytgi).**

Team B relevance: a reimplementation must store `Percentage / 100` (fraction), not the raw value, and must
place rows by order, not by `Level`.

---

## §14 `radio_events.xtbl` — the `memset` length, the CRC seed, `EventType` case rule, absent `Name`, and the `+0x2C` consumer

**Verdict: CONFIRMED (`memset` length = row count × 4 **bytes**; seed 0 / no length limit; `EventType` matched
case-insensitively; the 16-byte written set and the two defaults of 1) + CORRECTED (an absent `Name` skips
nothing — every field is still written; the spec's "skips the rest silently for that row" is wrong) + the `+0x2C`
consumer IDENTIFIED: it is the index of the matching entry in the stride-20 radio-content table at `0x013BBC18`
(the "song table" of §12) — resolved by `FUN_0055E470`, 0xFFFF when no entry matches. The "heap residue" claim
stays HIGH CONFIDENCE (the allocator is not dumped).**

Evidence — `func2/func_0x0055c1d0.txt` (the allocator/loader, 0x0055C1D0, body to 0x0055C273, single caller
0x0055F318 in 0x0055F270 — the radio-manager initialiser that also calls the station loader at 0x0055F2C5, §11):
- Opens `radio_events.xtbl` through 0x00DAC9A0 with (name, 0, 1). **No NULL test on the document**: the result
  goes straight to the first-child and count accessors (0x0055C1E1–0x0055C1F6), so a missing file's behaviour is
  whatever those accessors do with a NULL document (not in this job).
- Allocation: row count × 0x34 bytes from the main pool 0x01495410 via 0x00DAD460 with (size, 4, 0, 0), stored
  at 0x013BBBF8. Then `memset(array, 0, length)` where the length register is loaded as **row count × 4**
  (0x0055C214: a scaled-index load of the count by four, pushed as the byte count). So the cleared prefix is
  `count × 4` **bytes**, not `count` dwords — for the 13 retail rows that is 52 bytes = exactly the first 0x34-byte
  record; records 1–12 receive no clearing from this loader at all.
- The count at 0x013BBBFC (a **u16**) := 0. Per row: the current count is handed to 0x0055C100 in EAX (the callee
  uses only its low 16 bits) with the row node on the stack; the count is then incremented as a word; next sibling.
  Document closed; no return value, no cap (the array was sized to the exact count).
- Whether 0x00DAD460 returns zeroed memory is not in these dumps. The engine `memset`s after the same allocator in
  §5 and §11, which is consistent with it not zeroing — so "genuine uninitialised heap memory" is HIGH CONFIDENCE,
  not read.
- Globals: the array pointer 0x013BBBF8 is referenced by three functions only — this loader (write), the fill
  (read) and **0x0055C0B0** (read), which also reads the u16 count — the single consumer of the array, not dumped.

Evidence — `func2/func_0x0055c100.txt` (the per-row fill, 0x0055C100, body to 0x0055C1C0, single caller
0x0055C246; the slot index arrives in EAX, the row node on the stack):
- Record = base 0x013BBBF8 + (index & 0xFFFF) × 0x34.
- `Name` via the text accessor 0x00DABA10. The pointer is passed **unconditionally** to 0x00D9E8B0 with the
  three-argument shape of §1.3 item 2 — pushes `-1` (maximum byte count 0xFFFFFFFF = no limit), `0` (**seed 0**),
  the text pointer, ECX = a stack slot (0x0055C124–0x0055C12F) — and the result dword is stored at **`+0x08`**.
  There is no NULL test and no branch: **an absent `Name` skips nothing**. What 0x00D9E8B0 stores for a NULL
  pointer is not read for this entry point (not in this job); its sibling 0x00D9E7E0 stores 0 for NULL (§1.3 item
  2) — HIGH CONFIDENCE the same here, by family. The lower-casing is that callee's business, as before.
- The same `Name` pointer then goes through 0x00462960 (the Wwise id, §1.2 — 0 for NULL) and the id to
  **0x0055E470**; the returned value's low 16 bits are stored at **`+0x2C`** (a word store, 0x0055C14B). See the
  §16 evidence below for 0x0055E470: it is a linear scan of the stride-20 table at 0x013BBC18 for an entry whose
  `+0x00` equals the id, returning the index or −1 (so 0xFFFF after the word store).
- `+0x14` byte := 0 (0x0055C14F).
- `EventType`: the first child node of that name (0x00DC4FF0 — the node, not its text) is handed to 0x00DAC830
  with (pointer table at 0x01119960, 5, node, 0); the result dword → **`+0x0C`** (0x0055C172).
- `Post_Time`: 0x00DABD20 — the **"if present" `s32` reader** — with (pointer to a stack dword, node, name); it
  returns a byte flag in AL: non-zero → `+0x10` := the dword; zero → `+0x10` := **1** (0x0055C18A). CONFIRMED.
- `MaxTimesPlayed`: same reader; found → `+0x2F` byte := the low byte of the value; absent → **1** (0x0055C1B7).
- Bytes written per record: `+0x08` (4) + `+0x0C` (4) + `+0x10` (4) + `+0x14` (1) + `+0x2C` (2) + `+0x2F` (1) =
  **16** — the desk review's arithmetic is right; nothing else in the 0x34 bytes is touched here.
- This is the first dump in the job that shows the "if present" reader's calling shape (destination pointer first,
  presence returned in AL) — it agrees with the §1.2 catalogue mapping the earlier CORRECTED verdicts rely on.

Evidence — `func8/func_0x00dac830.txt` (the enum matcher, 0x00DAC830, body to 0x00DAC879, 30+ callers across the
image; only `_stricmp` is called): arguments (pointer table, entry count, node, unused). **NULL node → −1**
(0x00DAC838). Otherwise the node's text (`+0x0C`) is compared with `_stricmp` against table entries 0 … count−1
in order; a NULL table entry ends the scan with −1; the first equal entry's index is returned; no match → **−1**.
So `EventType` is matched **case-insensitively** (`news` = `News`), and both an absent `EventType` element and an
unrecognised text store **0xFFFFFFFF** at `+0x0C`. CONFIRMED — disassembly. The fourth argument (0) is never read.
The pointer table at 0x01119960 begins with the address 0x01119924, which the dumps show is the literal
`Commercial` (the same literal `commercials.xtbl`'s loader uses as its row-element name) — entry 0 = `Commercial`
is read; entries 1–4 (`News`, `Police`, `FBI`, `Police and FBI`) are not visible in these dumps and rest on the
earlier pass's literal-table read.

Load order: 0x0055F270 calls the station loader (0x0055F2C5) before this loader (0x0055F318), and writes the
radio-content count 0x013C045C at 0x0055F29B before both. Whether the stride-20 table has been populated (by the
per-station file loader, candidate 0x0055C9F0) by the time `+0x2C` is resolved is not in this job — if it has not,
every `+0x2C` would be 0xFFFF at this point. OPEN.

Spec text changes (§14):
- First paragraph: after "but **only `memset`s the first `count × 4` bytes of that `count × 0x34`-byte buffer**,
  not the whole thing" insert *"(2026-10-01, disassembly: the length is the row count scaled by four and passed as
  a byte count — for the 13 retail rows, 52 bytes, i.e. exactly the first record; records 1–12 get no clearing)"*;
  strike "start as genuine uninitialised heap memory, not zero" → *"start as whatever the pool allocator
  `0x00DAD460` hands out — HIGH CONFIDENCE uninitialised (the allocator is not dumped; the engine `memset`s after
  the same allocator in §5 and §11, consistent with it not zeroing)"*.
- Strike the `[OPEN — desk review 2026-09-30: whether the memset length is count × 4 bytes or count dwords …]`
  note; replace with: *"**[2026-10-01, re-derived (job 20261001T123123-team-a-ytgi): CONFIRMED — disassembly:
  `memset` length = row count × 4 bytes; `Name` is hashed with seed 0 and no length limit; `EventType` is matched
  by `FUN_00DAC830` with `_stricmp` (case-insensitive), and an absent element or unknown text stores −1
  (0xFFFFFFFF); an absent `Name` skips nothing — the NULL pointer is hashed and every other field is still
  written. The u16 count lives at `0x013BBBFC`; the only consumer of the array and count is `0x0055C0B0` (not
  dumped). Still OPEN: what `FUN_00D9E8B0` stores for a NULL pointer (its sibling `FUN_00D9E7E0` stores 0);
  whether the pool allocator zeroes; whether the radio-content table is already populated when `+0x2C` is
  resolved.]**"*
- Table row `+0x08`: strike "(required — absent skips the rest silently for that row via a 0-length hash, not
  traced further)" → *"(absent: nothing is skipped — the NULL pointer goes to the hash function and every other
  field is still written; 2026-10-01, disassembly — CORRECTED)"*; after "`FUN_00D9E8B0`, lower-cased" append
  *"— seed 0, no length limit (2026-10-01, disassembly)"*.
- Table row `+0x0C`: after "enum, `FUN_00DAC830` over a fixed 5-literal table" append *"— case-insensitive
  (`_stricmp`); absent element or unrecognised text → −1 (0xFFFFFFFF); entry 0 = `Commercial` confirmed from the
  pointer table at `0x01119960`, entries 1–4 as previously read (2026-10-01, disassembly)"*.
- Table row `+0x2C`: strike "Exact consuming structure for this specific index not traced (**OPEN**)" → *"the
  index of the entry in the stride-20 radio-content table at `0x013BBC18` (count: s16 at `0x013C045C`) whose
  `+0x00` equals the Wwise id of `Name` — `FUN_0055E470` is a linear scan of that table — or 0xFFFF when none
  matches. This is the same table §12's playlist slots and §16's commercial records live in (2026-10-01,
  disassembly; see §20 item 3)"*.
- Table rows `+0x10` and `+0x2F`: append *"(the "if present" reader `FUN_00DABD20`; default 1 confirmed
  2026-10-01)"*.
- New Review status: **Review status (2026-10-01): CONFIRMED — disassembly (`memset` = count × 4 bytes, seed 0 /
  no limit, case-insensitive `EventType` with −1 for absent/unknown, 16 written bytes, defaults of 1) and
  CORRECTED (absent `Name` skips nothing; `+0x2C` is an index into the `0x013BBC18` radio-content table) —
  cleared for implementation except the NULL-`Name` hash value and the allocator's zeroing (both HIGH CONFIDENCE,
  OPEN) — re-derived from the executable (job 20261001T123123-team-a-ytgi).**

Team B relevance: `+0x2C` must be resolved against the radio-content table, not stored as a hash; `EventType`
compares case-insensitively and yields −1, not 0, when missing.

Next dump: func 0x0055C0B0 (the consumer of the event array and count), func 0x00D9E8B0 (NULL-pointer result and
the lower-casing, for this group's own record), ptrs 0x01119960 5 (the five `EventType` literals).

---

## §15 `commercial_events.xtbl` — signedness of the `< 30` test, the CRC seed, the second dword, load order

**Verdict: CONFIRMED (slot = `EventValue`, bound 30, stride 8, only the first dword written, `Name` hashed by
`FUN_00D9E740`) + the signedness OPEN SETTLED: the test is **unsigned** (`EventValue` above 29 as an unsigned
value — which includes every negative value — drops the row; nothing can index before the array) + seed 0 read +
the second dword still OPEN (no dumped function reads it; the array has a static-initialisation writer outside
any function).**

Evidence — `func2/func_0x0055bd30.txt` (0x0055BD30, body to 0x0055BDDB, single caller 0x0055BF90):
- Opens `commercial_events.xtbl` via 0x00DAC9A0 with (name, 0, 1); **NULL document → return at once** (nothing
  written, nothing closed). The `Event` child count must be > 0 (signed test, 0x0055BD5D) or the document is just
  closed. First `Event` child; NULL → close.
- Per row: `Name` via the text accessor 0x00DABA10; **NULL → the row is skipped** (0x0055BD87 → next sibling).
  `EventValue` via **0x00DABC70 — the "always" `s32` reader** — into a stack dword (the literal is shown by the
  dump's label printer as `;@EventValue`; the dword at 0x011198B8 is the bytes `Even`, a display artefact of the
  same kind as §8's `?MinimumSpeed`). Then **`CMP [slot], 0x1D` / `JA`** (0x0055BD9C–0x0055BDA1): an **unsigned**
  "above 29" test. So the accepted range is 0 … 29 as unsigned values; a negative `EventValue` (0x80000000 and
  above as unsigned) is **dropped**, exactly like 30 and above — it can never index before the array.
- Accepted rows: 0x00D9E740 is called with **two** stack arguments — `0` (**seed 0**) and the `Name` text — i.e.
  this entry point has no length-limit argument (unlike the three-argument 0x00D9E7E0 / 0x00D9E8B0 of §1.3 item 2
  and §14); the result (EAX) is stored at **0x013BBB08 + `EventValue` × 8** — the first dword of the 8-byte slot
  (0x0055BDB2). The store is unconditional, so a duplicate `EventValue` leaves the **last** row's hash in the
  slot. The second dword of the slot is **not written** here. Document closed at the end. CONFIRMED — disassembly.
- The lower-casing attributed to 0x00D9E740 is that callee's business (not in this job; `spec-tables-weapons-combat.md`
  §1.4 as before).
- Globals for 0x013BBB08: besides this writer, the first dword of a slot is **read** by 0x0055BDE0 (§16, twice —
  the `EnableEvent` / `DisableEvent` scans), by **0x0055C070** (a third reader, not dumped), and there is a write at
  **0x00FEE110 "(no function)"** preceded by `MOV EAX, 0x13BBB08` at 0x00FEE100 — a store through a pointer that
  the listing attributes to both 0x013BBB08 and 0x013BBB10, i.e. a startup initialiser walking the array. What it
  stores (0, −1, …) is not readable from the listing. **No dumped function reads the second dword** of any slot
  (0x013BBB0C, 0x013BBB14, …), so its purpose stays OPEN.
- Load order: the one caller 0x0055BF90 calls this loader at 0x0055BF90 and `commercials.xtbl`'s loader at
  0x0055BF95 (the two callers/refs headers) — the event hashes are therefore in place before §16 resolves
  `EnableEvent` / `DisableEvent` against them, as that section's mechanism requires. CONFIRMED from the two
  caller lists.

Spec text changes (§15):
- First paragraph: strike "an `EventValue` strictly **less than 30** (`0x1E` — values `≥30` are silently dropped,
  not clamped)" → *"an `EventValue` that is **below 30 as an unsigned value** (the test is an unsigned "above
  29", so values ≥ 30 **and every negative value** are silently dropped, not clamped — nothing can index before
  the array; 2026-10-01, disassembly)"*; after "(`FUN_00D9E740`, lower-cased)" insert *"— seed 0; this entry
  point takes (text, seed) only, no length-limit argument"*; after "only the first dword — the name hash — is
  written by this loader, indexed by `EventValue`" append *"; a duplicate `EventValue` leaves the last row's hash
  (unconditional store); a row without `Name` is skipped"*.
- Strike the `[OPEN — desk review 2026-09-30: whether the < 30 test is signed … nor the CRC-32 seed …]` note;
  replace with: *"**[2026-10-01, re-derived (job 20261001T123123-team-a-ytgi): CONFIRMED — disassembly: unsigned
  bound test (negatives dropped), seed 0, first dword only, stride 8. The second dword of each slot is read by no
  dumped function; the array is also written by a startup initialiser outside any function (`0x00FEE100`–
  `0x00FEE110`, content not readable from the cross-reference listing), and read by a third function `0x0055C070`
  (not dumped) — the second dword's purpose and the slots' initial content remain OPEN. The one caller
  `0x0055BF90` runs this loader immediately before `commercials.xtbl`'s (`0x0055BF90`, then `0x0055BF95`).]**"*
- Second paragraph: "`EventValue` (`s32`, "always", `FUN_00DABC70`) is the slot index, bound-checked `< 30`" →
  append *"(unsigned)"*.
- New Review status: **Review status (2026-10-01): CONFIRMED — disassembly (unsigned `< 30` test, seed 0, first
  dword only, stride 8, `Name`-less rows skipped, load order before `commercials.xtbl`) — not cleared: the
  second dword's reader and the startup initialiser's content (`0x00FEE100`) remain OPEN — re-derived from the
  executable (job 20261001T123123-team-a-ytgi).**

Team B relevance: negative `EventValue`s are dropped, not wrapped; the hash seed is 0.

Next dump: range 0x00FEE0F0-0x00FEE130 (the startup initialiser of the 30-slot array), xref 0x013BBB0C (any
reader of a slot's second dword), func 0x0055C070 (the third reader of the array).

---

## §16 `commercials.xtbl` — record offsets and stride, the not-found rule, the CRC seed, and what the "separate registry" is

**Verdict: CONFIRMED (every offset `+0x04`/`+0x11`/`+0x12`/`+0x13`, every default, `InitialState` rule, seed 0 on
both sides of the §15 cross-reference) + CORRECTED (the "separate registry" is **the stride-20 radio-content
table at `0x013BBC18`** — the "song table" of §12 and of `spec-save-format.md` §12.8 — so the record stride is
0x14, and these commercial records are entries of that table) + the not-found rule SETTLED (skip; no record is
ever created here).**

Evidence — `func2/func_0x0055e470.txt` (the hash → index resolver, 0x0055E470, body to 0x0055E49C, no callees;
callers 0x0055BF1F in this loader and 0x0055C140 in §14's fill): takes one dword; the entry count is the **s16 at
0x013C045C** sign-extended (must be > 0, else −1); it compares the dword at **0x013BBC18 + i × 0x14** (`+0x00` of
entry i) with the argument for i = 0 … count−1 and returns the first equal **index, zero-extended from 16 bits**;
no match → **−1**. So the table it resolves into is the stride-20 table at 0x013BBC18 — the address and stride
§12.2 already attributes to the runtime song table and §11's `0x0055E380` indexes for the mix-tape list. The
count 0x013C045C is written by 0x0055C950 and 0x0055F270 and read by 0x0055C910, 0x0055C9F0, 0x0055DF80,
0x0055E340, 0x0055E4A0, 0x0055F660, 0x00560A50 (globals section). CONFIRMED — disassembly.

Evidence — `func2/func_0x0055df80.txt` (the index → record accessor, 0x0055DF80, body to 0x0055DFA2, no callees;
callers 0x0055BF28 here, 0x0055BFA8 in 0x0055BFA0, 0x0083E696 in 0x0083E670): takes a 16-bit index; **negative,
or ≥ the s16 count at 0x013C045C (signed compares) → NULL**; otherwise returns `0x013BBC18 + index × 20`
(0x0055DF95–0x0055DF9B: the index times five, times four). CONFIRMED. Consequence: the resolver's "not found"
value 0xFFFF, zero-extended by the loader (0x0055BF24) and then re-read by this accessor as a 16-bit value, is
−1 → NULL.

Evidence — `func2/func_0x0055bde0.txt` (the loader, 0x0055BDE0, body to 0x0055BF86, single caller 0x0055BF95 in
0x0055BF90 — the same caller that runs §15's loader five bytes earlier):
- Opens `commercials.xtbl` (0x00DAC9A0 with (name, 0, 1)); NULL → return. `Commercial` child count must be > 0
  (signed) else close; first `Commercial` child; NULL → close.
- Per row: `Name` via 0x00DABA10; **NULL → the whole row is skipped** (0x0055BE44 → next sibling). Otherwise the
  text is Wwise-hashed by 0x00462960 (§1.2) and the id kept as the key. Four locals are primed: `Length` 0,
  enabled 1, enable-slot 0xFF, disable-slot 0xFF (0x0055BE58–0x0055BE6A).
- `InitialState` via 0x00DABA10; if non-NULL and `_stricmp(text, "Disabled") == 0` → enabled := 0 (the compare's
  zero result is what is stored, 0x0055BE8B). Anything else, including an absent element, leaves 1. CONFIRMED.
- `EnableEvent` via 0x00DABA40; if non-NULL → 0x00D9E740 with **two** arguments (`0` = **seed 0**, the text) —
  the same entry point, same seed and same argument shape as §15's writer — and the result is compared with the
  first dword of **all thirty** slots 0 … 29 of 0x013BBB08 (stride 8, the loop counter is a signed byte compared
  against 0x1E) in order; first hit → enable-slot := that index; none → 0xFF. The scan covers slots §15 never
  wrote (their content comes from the startup initialiser noted in §15), so a hash equal to a never-written slot's
  initial content would match it — only possible if that content can equal a real CRC. CONFIRMED for the rule.
- `DisableEvent`: identical, into disable-slot.
- `Length` via **0x00DABC70 — the "always" `s32` reader** → a stack dword. CONFIRMED as the spec states.
- Then 0x0055E470(key) → index (0xFFFF if none) → zero-extended → 0x0055DF80(index) → record pointer or NULL.
  **Only if the pointer is non-NULL and the record's `+0x00` equals the key** (a re-check at 0x0055BF34) are the
  four stores made: **`+0x04` (dword) := Length; `+0x13` (byte) := enabled; `+0x11` (byte) := enable-slot;
  `+0x12` (byte) := disable-slot** (0x0055BF40–0x0055BF49). **Otherwise nothing is written — the row is dropped
  and no record is created** (0x0055BF4C → next sibling). CONFIRMED — disassembly for every offset.
- Document closed at the end.

Consequences:
- The "separate registry" is the stride-20 table at 0x013BBC18 with count s16 at 0x013C045C. Its per-entry
  layout, assembled from this job's dumps: **`+0x00` dword = a Wwise id** (the key three independent readers
  compare Wwise hashes against — this loader, §14's fill via 0x0055E470, and §12's 0x0083E230 against the
  catalog's `WWise_ID`); **`+0x04` dword = `Length`** (for commercials); `+0x08` not written by anything dumped;
  **`+0x0C` u16 = slot in station 0's mix-tape list or 0xFFFF** (§11, 0x0055E380); `+0x0E`–`+0x10` not written by
  anything dumped; **`+0x11` s8 = `EnableEvent` slot**, **`+0x12` s8 = `DisableEvent` slot**, **`+0x13` byte =
  enabled flag**. Songs, commercials and (§14) radio events are all resolved in this one table by Wwise id —
  "radio-content table" is the neutral name; the per-station file loader candidate 0x0055C9F0 (which references
  the `Length` literal seven times and the `Commercial` literal once, per the globals sections) and the
  find-or-add-shaped 0x0055C950 (compares `+0x00`, stores into the table, writes the count) are the likely entry
  creators — HYPOTHESIS, neither is dumped.
- Both sides of the §15/§16 cross-reference hash with 0x00D9E740 and seed 0, so the empirical 33/33 + 28/28 match
  is a real equality of CRC values, not merely "any shared seed".

Spec text changes (§16):
- First paragraph: strike "used to find a **pre-existing** commercial record through a separate registry
  (`FUN_0055E470` hash→index, `FUN_0055DF80` index→record — neither independently decompiled; these commercial
  records are not built by this loader itself, only patched by it)" → *"used to find a **pre-existing** entry of
  the stride-20 radio-content table at `0x013BBC18` (count: s16 at `0x013C045C`) — the same table §12.2 calls
  the runtime song table and `spec-save-format.md` §12.8 names — through `FUN_0055E470` (a linear scan of the
  table's `+0x00` Wwise ids; returns the index or −1) and `FUN_0055DF80` (index → entry, NULL when negative or ≥
  the count). A `Name` whose id is in no entry (or an entry whose `+0x00` does not re-match the id) is **silently
  dropped — this loader never creates an entry**; a row without `Name` is skipped before hashing (2026-10-01,
  disassembly — CORRECTED: not a separate registry)"*.
- Table: add a header note *"Record = a 20-byte entry of the `0x013BBC18` table; `+0x00` is the Wwise id the row
  was resolved by, `+0x0C` the mix-tape slot (§11) — 2026-10-01, disassembly."* `InitialState` row: append
  *"(an absent element leaves 1)"*. `EnableEvent` row: strike "Hashed via the **engine** CRC-32 (`FUN_00D9E740`,
  **not** the Wwise hash …)" → keep and append *"— seed 0, the same entry point and seed `commercial_events.xtbl`'s
  loader used (2026-10-01, disassembly); the scan covers all 30 slots in order, including ones §15 never wrote"*.
  `Length` row: append *"(`+0x04` is a dword; default 0 is primed but the "always" reader overwrites it)"*.
- Strike the `[OPEN — desk review 2026-09-30: the record offsets +0x04/+0x11/+0x12/+0x13 have no independent
  corroboration …]` note; replace with: *"**[2026-10-01, re-derived (job 20261001T123123-team-a-ytgi):
  CONFIRMED — disassembly: offsets `+0x04` (dword), `+0x11`, `+0x12`, `+0x13` (bytes); stride 0x14 (entries of
  the `0x013BBC18` table); not-found → row dropped, never created; CRC seed 0 on both sides. Still OPEN: which
  code creates the entries (candidates `0x0055C9F0`, `0x0055C950`) and the meaning of `+0x08` / `+0x0E`–`+0x10`.]**"*
- New Review status: **Review status (2026-10-01): VALIDATED-BY-DATA (unchanged) + CONFIRMED — disassembly
  (offsets, stride 20 in the `0x013BBC18` table, skip-not-create, seed 0 both sides) and CORRECTED (the registry is
  the radio-content/song table, not a separate structure) — cleared for implementation; entry creation is outside
  this job (OPEN) — re-derived from the executable (job 20261001T123123-team-a-ytgi).**

Team B relevance: commercials are entries of the same table the playlists index; a reimplementation that keeps a
separate commercial registry will not reproduce `+0x0C`/`+0x11`–`+0x13` living in one record. Unknown
`Name`s are dropped, not added.

Next dump: func 0x0055C9F0 (entry creation; also §11/§12's candidate), func 0x0055C950 (find-or-add; writes the
count 0x013C045C), func 0x0055BF90 (the caller: what else it loads and in which order), func 0x0055BFA0 (the
third caller of the index → entry accessor).

---

## §17 `voc_sb_line_sit.xtbl` — the `+0x54` bit-15 gate, `FUN_00468ED0` / `FUN_00468CD0`, and the `DMLV` header

**Verdict: CONFIRMED (the gate is bit 15 of the `u16` at bank `+0x54`; filename `<Soundbank>.lm_pc`; 600-entry
scratch list; running total; `Soundbank` bounded to 0x41) + CORRECTED (`Persona_id` and `Num_line_situations`
use the "always" `u32` reader, and `Persona_id` is read and discarded by this loader) + the `.lm_pc` reader
`FUN_00468CD0` now read: it requires the magic `DMLV` (dword 0x564C4D44) and version 1 — closing the
"`DMLV` unidentified" side of `spec-audio-format.md` §9.2's open item — + the internal tension RESOLVED in the
uncomfortable direction: **with the `+0x54` writers found in this job (§2), none of the 265 shipped entries
passes the gate** — 0/265 `Soundbank` values are `load_at_boot = True` banks (data check), so either bit 15 is
also set by code outside this job (candidates named) or this path opens no `.lm_pc` at all.**

Evidence — `func1/func_0x00468ed0.txt` (the loader, 0x00468ED0, body to 0x004690F4, single caller 0x00469492 in
0x00469460):
- Builds `voc_sb_line_sit.xtbl` into a 0x400-byte stack buffer (bounded append 0x00DA7A60); calls the no-argument
  method at vtable slot `+0x1C` of the allocator object 0x0317285C (the same object whose slot `+0x38` allocates
  in §8/§9 and below) — and calls the same slot again at the very end (0x004690D6): an enter/leave-shaped pair,
  purpose not read. The running total (a stack dword) := 0.
- Existence check 0x00DA90D0; **missing file → return 1** (success; the closing `+0x1C` call is skipped).
- A scratch list is set up on the stack: count := 0, **capacity 0x258 = 600**, pointer to a 2400-byte pointer
  array (0x00468F32–0x00468F48); 0x00DC52A0 is then called with the array pointer in EDX (not dumped — a clear,
  presumably). Open via 0x00DAC9A0 with (name, 0, 1); NULL → skip the walk (the post-processing below still runs).
- `Entries` first child → NULL → close; `Entry` first child under it → NULL → close. Per `Entry`:
  - `Persona_id` via **0x00DABDF0 — the "always" `u32` reader** — into a stack dword at frame −0x458 that
    **no instruction reads afterwards**: the value is discarded by this loader (as §5 discards `Name`).
  - `Soundbank` via the bounded copy 0x00DABA70 into a stack buffer, **limit 0x41** (0x00468FC0). CONFIRMED.
  - `Num_line_situations` via **0x00DABDF0 ("always" `u32`)** into a stack dword.
  - 0x004647C0 with the buffer in EAX — the Wwise-id-keyed bank lookup read in §2 (547 buckets) → record or
    NULL. NULL → next entry. Then **`MOV CX, [record + 0x54]` and `TEST CX, 0x8000`** (0x00468FE9–0x00468FF5):
    the **`u16` at `+0x54`, bit 15**; clear → next entry. CONFIRMED — disassembly: the gate is exactly as stated.
  - Gated in: total += `Num_line_situations`; a 0x40-byte block is reserved on the stack (an `alloca` probe —
    failure → the function returns 0); the `Soundbank` text is copied into it (0x00DA7930, limit 0x40), then
    `.lm_` (literal 0x0129F014) and the literal at 0x0129F01C — whose dword the dump shows as the bytes `pc` — are
    appended (0x00DA7A60, limit 0x40 each): **`<Soundbank>.lm_pc`**, CONFIRMED. If count + 1 ≤ 600 the block's
    address is stored at list[count] and count++; otherwise the name is dropped (the total was still added).
  - Next `Entry`; afterwards close.
- Post-walk (also reached when the document failed to open): 0x0046B310 is called with EAX = the running total,
  EDI = the allocator object and ECX = the static object **0x01353760** (file-backed `.data`; its first dword is a
  pointer 0x0129F0B8 written by the static initialiser 0x01016760 — a C++ static object; not dumped: HYPOTHESIS a
  "reserve the line-situation registry for N entries" call). Then for each queued name, 0x00468CD0 with EAX = the
  name; **a 0 return aborts the whole function with 0**; otherwise, after the last name, the closing `+0x1C` call
  and return 1.

Evidence — `func1/func_0x00468cd0.txt` (the `.lm_pc` reader, 0x00468CD0, body to 0x00468EC1, single caller
0x004690B9; name in EAX):
- Existence check 0x00DA90D0: **missing `.lm_pc` → return 1** (tolerated). Open via 0x00DAB0C0 with (name, the
  `rb` literal 0x0129A85C, 0, 0, −1) — the same open-by-name §2's allocator uses for `_media.bnk_pc`; NULL →
  return 1.
- Reads 12 bytes (0x00DAA5D0 with (destination, 12, handle, 0)). **dword 0 must equal 0x564C4D44 — the bytes
  `D` `M` `L` `V` in file order — and dword 1 must equal 1**; otherwise the file is closed and 1 returned
  (0x00468D23–0x00468D36). dword 2 = the entry count (unsigned; 0 → close, return 1). So the `DMLV` magic
  `spec-audio-format.md` §9.2 observed is **the reader's own check, followed by a version dword of 1** —
  CONFIRMED — disassembly.
- Per entry, a 16-byte header is read (call them A, B, C, D in file order). **The global 0x03171A9C += C** (its
  only reference in the image is this add — a statistic nothing reads). A packed block of C × 6 + 1 bytes (D ≠
  0) or C × 4 + 1 bytes (D = 0), plus 1 more byte when C > 1, is allocated through the allocator's slot `+0x38`
  with (size, 1, 0, 0); **NULL → return 0** — the only failure that propagates. Block byte 0 := (D ≠ 0 ? 1 : 0) |
  (C << 1) (C truncated to seven bits); if C > 1 the next byte := 0xFF. Then C sub-entries of 16 bytes (P, Q, R,
  S) are read one at a time: P is stored as a dword; if D ≠ 0 a word follows: ((R & 0x7F) << 8) | ((Q & 0x7F) <<
  1) | (S ≠ 0 ? 1 : 0) | (bit 15 of the previously stored word — a stack slot that nothing initialises before the
  first sub-entry, so the first word's bit 15 is residue). This is the `.lm_pc` interior at the depth read; it is
  `spec-audio-format.md`'s territory and is recorded here only as a hand-off.
- After each entry: **if the byte at 0x0135377A is 0 the function stops** (closes, returns 1) — that byte is
  0x1A past the static object 0x01353760, i.e. one of its members, zero in the image and with no absolute-address
  writer listed (a method writing through `this` would not appear) — HYPOTHESIS: set by 0x0046B310. Non-zero →
  0x0046B3B0 with (B, A, pointer to the block) on the stack and EAX = the object; a −1 return stops the file;
  otherwise the next entry while i < count. Close; return 1.

Data check (read-only, `tools/ao_xtbl/`): `audio_banks.xtbl` has exactly 23 rows with `load_at_boot = True`
(`Ambience`, `Weapons_Cache`, `VFX`, `Interface`, `Radio_GenX`, `Init`, `Objects`, `Vehicles_Cache`,
`Animation_Brute`, `Radio_The_Mix`, `Movement`, `Radio_Kabron`, `Interface_Cache`, `Radio_Krunch`, `Radio_K12`,
`Radio_Misc`, `ext_sources`, `Radio_Swim`, `Radio_Klassic`, `Animation`, `Radio_Newscasts`, `Radio_Krhyme`,
`Misc_SFX`) — **`Init` is one of the 23**, not an additional row. `voc_sb_line_sit.xtbl`'s 265 distinct
`Soundbank` values intersect that set in **0 names**; all 265 are `load_at_boot = False`, `voice = True`,
`streaming = True` banks (e.g. `voc_Angel`, `voc_Bobby`). CONFIRMED — empirical.

Putting the two together: §2's re-derivation found exactly two writers of `+0x54` in this job — the allocator
(`:= 1`) and the loader (`:= 0x8004`, base-game `Init` / `load_at_boot = True` rows only). With those alone, bit 15
is set on 23 banks, none of which is named by this table, so **this loader would open no `.lm_pc` and the total
would be 0**. For the mechanism to do anything, bit 15 must also be set by code outside this job — the natural
candidates are the boot-load request 0x004673C0 (called by the loader, but only for the same 23 rows), the bank
state machine 0x00465CC0 (14 registry references, §2 — if bit 15 means "resident" rather than "boot-load", it
would set it when a voice bank is loaded on demand, and 0x00469460's timing would then decide what this loader
sees), and the `voice` adjustment 0x00464820 (run before the allocator's own `+0x54 := 1`, so it cannot be the
answer by itself). Which of these holds is OPEN; the spec's second alternative ("few `.lm_pc` files are ever
opened by this path") is, on the evidence in this job, the stronger one.

Spec text changes (§17):
- First paragraph: strike "reading `Persona_id` (`u32`, "if present"), `Soundbank` (bounded string copy, `0x41`
  bytes) and `Num_line_situations` (`u32`, "if present")" → *"reading `Persona_id` (`u32`, **"always"**,
  `FUN_00DABDF0` — read into a stack slot that this loader never uses again), `Soundbank` (bounded string copy,
  `0x41` bytes) and `Num_line_situations` (`u32`, **"always"**) (2026-10-01, disassembly — CORRECTED)"*; after
  "only if that bank's own `+0x54` flag word has bit `0x8000` set" insert *"(the `u16` at `+0x54`, bit 15 —
  2026-10-01, disassembly; by §2.3 that bit is written only as part of the loader's `0x8004` for base-game
  `Init`/`load_at_boot = True` rows, as far as this job's dumps show)"*.
- Item 2: strike "(up to a 600-entry scratch capacity) … (`FUN_00468CD0`, not traced — reading the actual
  `.lm_pc` file's own interior is `spec-audio-format.md`'s territory, out of this document's xtbl-schema scope)"
  → *"(a 600-entry scratch list on the stack; a 601st name is dropped) and, after the walk, passes the running
  total to `0x0046B310` on the static object `0x01353760` and then runs `FUN_00468CD0` on each queued name.
  `FUN_00468CD0` (read 2026-10-01) tolerates a missing or malformed file (returns success) and requires a
  12-byte header of **`DMLV`** (dword `0x564C4D44`), **version 1**, entry count; its entry parsing is
  `spec-audio-format.md`'s territory and is handed over in `review/exe-notes-2026-10-01/rederive_tables-audio-radio.md`
  §17. The only failure that aborts the whole load is a pool-allocation failure inside it."*
- Strike "and the acronym `DMLV` itself remains unresolved — that is `spec-audio-format.md`'s own open item, not
  re-derived here" → *"the `DMLV` magic is this reader's own header check (`0x564C4D44`, followed by a version
  dword that must be 1) — 2026-10-01, disassembly; what the letters stand for is still not known"*.
- Strike the `[OPEN — desk review 2026-09-30, internal tension: the gate "bit 0x8000 of +0x54 = marked
  boot-loaded" is asserted …]` note; replace with: *"**[2026-10-01, re-derived (job 20261001T123123-team-a-ytgi):
  CONFIRMED — disassembly for the gate (`u16` at `+0x54`, bit 15) and the filename; CONFIRMED — empirical that
  0/265 `Soundbank` values are `load_at_boot = True` banks (all 265 are `False`, `voice = True`, `streaming =
  True`; `Init` is one of the 23 `True` rows, not an extra). Since the only bit-15 writer found (§2.3) is the
  loader's `0x8004` for those 23 rows, this path opens **no** `.lm_pc` unless bit 15 is also set elsewhere —
  candidates `0x00465CC0` (bank state machine, if the bit means "resident") and `0x004673C0`; the caller
  `0x00469460`'s timing matters equally. OPEN; the "few `.lm_pc` files are opened by this path" alternative is the
  better supported one.]**"*
- §17.1: append *"(2026-10-01: the same 265 names are all `load_at_boot = False` and `voice = True` in
  `audio_banks.xtbl` — see the note above)"*.
- §18.1 third bullet: strike "the naming and gating mechanism that produces `.lm_pc` filenames is now
  disassembly-confirmed; the file's own interior format and the `DMLV` acronym remain that document's open items"
  → *"the naming and gating mechanism is disassembly-confirmed and the `DMLV` magic plus version 1 is the
  reader's own check; but on the writers found so far the gate admits none of the shipped entries (§17), so
  whether this path ever opens a `.lm_pc` is OPEN; the interior layout at the depth read is handed over in the
  re-derivation notes"*.
- New Review status: **Review status (2026-10-01): CONFIRMED — disassembly (bit-15 gate, filename, 600-list,
  `DMLV`/version-1 header, missing-file tolerance) and CORRECTED ("always" readers; `Persona_id` discarded) +
  CONFIRMED — empirical (0/265 entries name a boot-load bank) — not cleared: whether bit 15 is set outside the
  loader (`0x00465CC0`, `0x004673C0`, caller `0x00469460`) decides whether this mechanism ever fires; the
  registry calls `0x0046B310`/`0x0046B3B0` and the gate byte at `0x0135377A` are unread — re-derived from the
  executable (job 20261001T123123-team-a-ytgi).**

Team B relevance: do not assume `.lm_pc` files are opened at table-load time for voice banks; the gate depends on a
runtime bit. For `spec-audio-format.md`: `DMLV` + version 1 + count header, then per entry a 16-byte header and
16-byte sub-entries (layout above).

Next dump: func 0x00465CC0 (does the state machine set `+0x54` bit 15 when a bank becomes resident?), func
0x004673C0, func 0x00469460 (when the loader runs), func 0x0046B310, func 0x0046B3B0, range
0x01353760-0x01353780 (the static object: the gate byte at `+0x1A`).

---

## §18.1 → `spec-audio-format.md` — the `+0x5b` bit-6 / `+0x54` mapping behind "answers the boot-time half"

**Verdict: still HIGH CONFIDENCE (not upgraded), but NARROWED: the loader side of the mapping is now read; the
state-machine side (`0x00465CC0`) and the boot-load request (`0x004673C0`) are not in this job.**

Evidence (all from units above, nothing new read here):
- §2: for a base-game row that is `Init` or `load_at_boot = True`, the loader writes `+0x54` := 0x8004 (bit 15
  and bit 2), calls 0x004673C0 with argument 1 and EDI = the registry, and (for `Init` only) sets `+0x5B` bit 0;
  it also sets `+0x5B` bits 2/3/4 from `cacheable_pc`/`ram_bank_pc`/`voice`. **No function in this job writes
  `+0x5B` bit 6** (0x40) — not the loader, not the allocator. The 32 references to the registry pointer
  0x0320E520 name the state machine 0x00465CC0 (14 uses) as the obvious candidate writer; it is not dumped.
- §17: the only *reader* of `+0x54` bit 15 in this job is the `voc_sb_line_sit.xtbl` loader, and on the shipped
  data it would read it set on no entry it names — so whether bit 15 is a "boot-load" mark (loader-only writer) or
  a "resident" mark (also set by the state machine) is exactly the unread question that decides both §17 and this
  bullet.
- So "this table is what sets the boot-time trigger" is supported by the loader's writes (0x8004, the
  0x004673C0(1) call) but the hook `spec-audio-format.md` §7.4 branches on (`+0x5B` bit 6) is set by code outside
  this job. HIGH CONFIDENCE stands; nothing here contradicts it.

Spec text changes (§18.1, first bullet):
- Strike "`+0x40` (numeric bank id), `+0x44` (streaming-companion flag), `+0x54` bit 15 / bits (boot-load),
  `+0x5b` bits 0–3 are all populated from `audio_banks.xtbl` (§2.3)" → *"`+0x40` (numeric bank id), `+0x44`
  (streaming flag), `+0x54` = 0x8004 (bits 15 and 2) for boot-load rows, `+0x56` (companion header word),
  `+0x5A` (DLC index) and `+0x5B` bits 0, 2, 3, 4 are all populated from `audio_banks.xtbl` (§2.3, re-derived
  2026-10-01); boot-load rows additionally trigger `0x004673C0(1)`"*.
- Strike the `[OPEN — desk review 2026-09-30: spec-audio-format.md §7.4 has the hook branch on +0x5b bit 0x40
  …]` note; replace with: *"**[2026-10-01, re-derived (job 20261001T123123-team-a-ytgi): narrowed, not closed.
  `+0x5B` bit 6 is written by no function in this job (loader and allocator read); the loader's persistent
  boot-load traces are `+0x54` = 0x8004 and the `0x004673C0(1)` call; the state machine `0x00465CC0` and
  `0x004673C0` are unread, so the hook bit's writer and the meaning of `+0x54` bit 15 (boot-load mark vs
  resident mark — see §17) stay OPEN. "Answers the boot-time half" remains HIGH CONFIDENCE.]**"*
- New Review status (§18): **Review status (2026-10-01): DESK-PASS, text fixes applied; §18.1 narrowed by the
  re-derivation of §2/§17 (loader-side writes CONFIRMED — disassembly; `+0x5B` bit 6 writer and `0x00465CC0`
  OPEN); inherits the open items of §2, §11, §12, §17 — re-derived from the executable (job
  20261001T123123-team-a-ytgi).**

Next dump: func 0x00465CC0, func 0x004673C0 (both already listed under §2/§17).

---

## §20 Open items — what this pass closes, narrows or leaves

Per item (spec text change = replace the item's text as indicated):

1. `audio_line_tags.xtbl`'s unreachable rows — no exe question was asked; §5 adds that the cross-reference listing
   shows no function other than the loader referencing the array `0x01504484` or its count `0x01504480` by name.
   *Append that sentence; item stays.*
2. Dynamic bank loading — unchanged (and §18.1 above keeps the boot-time half at HIGH CONFIDENCE).
3. **The stride-20 table at `0x013BBC18` — substantially mapped (CONFIRMED — disassembly, §11/§12/§14/§16).**
   `+0x00` is a Wwise id: three independent readers compare Wwise hashes of names against it (`FUN_0055E470` for
   commercial and radio-event `Name`s; `0x0083E230` against `playlist_artist_track.xtbl`'s `WWise_ID`), which
   closes the "one of its fields is itself a `WWise_ID`" indirection this item asked about — the save file's
   `u16`s index this table, and this table's `+0x00` is the id the catalog is joined on. Also mapped: `+0x04`
   `Length` (commercials), `+0x0C` u16 mix-tape slot / 0xFFFF, `+0x11`/`+0x12` enable/disable event slots,
   `+0x13` enabled flag; count = s16 at `0x013C045C`. Songs, commercials and radio events share it. Still OPEN:
   `+0x08`, `+0x0E`–`+0x10`, and the entry creators (`0x0055C9F0`, `0x0055C950`). *Replace the item with this.*
4. Station 0's `+0x194` — still OPEN (§12: no dump writes it; the inline list `+0x1A4`/count `+0x5A4` confirmed).
   *Keep; add "§12, 2026-10-01: narrowed to the per-station file loader `0x0055C9F0`".*
5. `persona_radio_prefs.xtbl` matching rule — **closed** (§7: Wwise-id equality with the station's `wwise_id`,
   byte = index + 1, 242/242 rows resolve). *Strike the item.*
6. `foley_engine.xtbl` capacity / vehicle `+0x7AC` — capacity HIGH CONFIDENCE 128 by layout; every reader of the
   pointer array is index-addressed and two name → index resolvers exist (§10); vehicle side still OPEN. *Replace
   with that wording.*
7. Small unread fields: `audio_personas.xtbl` `+0x26` still OPEN (the fill writes 0xFFFF, no reader in this job),
   `+0x2C` closed (a u16 zeroed by the fill, §6); `radio_activities.xtbl`'s `DAT_012A2DD8` closed (the constant
   double 100.0, §13); `radio_events.xtbl`'s `+0x2C` consumer closed (it is an index into the `0x013BBC18`
   table, §14). *Rewrite the item to list only `+0x26`.*
8. `.lm_pc` interior — unchanged in principle, but §17 now supplies the header (`DMLV`, version 1, count) and the
   entry structure at the depth read, handed to `spec-audio-format.md`. *Append a pointer to §17.*
9. *New item:* **whether the `voc_sb_line_sit.xtbl` path ever opens a `.lm_pc`** — on the `+0x54` writers found,
   no shipped entry passes its bit-15 gate (§17); decided by `0x00465CC0` / `0x004673C0` / `0x00469460`.
10. *New item:* **the `+0x5B` bit-6 writer and the free-list capacity** (`0x00466ED0`, §2) — the two remaining
    exe gaps of the priority table.

---

## Summary table

| Unit | Verdict | Cleared? | Still OPEN (narrowed to) |
|---|---|---|---|
| §1.2 Wwise string-to-id | CONFIRMED — disassembly (pure forward; 0 for NULL / empty / `none`) | no | the SDK hash body `0x00F4C350`, converter `0x0046FC50` |
| §1.3 item 1 `wwise_id` parser | CONFIRMED (grammar; 32-bit modular accumulator) | **yes** | — |
| §1.3 item 2 `FUN_00D9E7E0` | CONFIRMED (seed 0, no limit, no XOR, separate entry point) | **yes** | — |
| §2 `audio_banks` | CORRECTED (`voice` → `+0x5B` bit 4; `+0x48`; `+0x54`/`+0x56`; `Unknown` unreachable) + CONFIRMED (−6/−4, `+0x5A` DLC byte, bits 0/2/3/4, `wep_` pass base-only, 547-bucket map) | no | free-list capacity `0x00466ED0`; `+0x5B` bit 6 writer |
| §3 `audio_constants` | CORRECTED (12 `u32` `PlayTimers`, integer squaring) + CONFIRMED (all destinations, double multiply) | **yes** | — |
| §4 `audio_settings` | CORRECTED (Doppler default 10.0) + CONFIRMED (priming sites, 0.0 fallback) | **yes** | — |
| §5 `audio_line_tags` | CONFIRMED (119 cap, test-after-store) + CORRECTED ("always" reader; array zeroed) | **yes** | — |
| §6 `audio_personas` | CONFIRMED (`strstr` case-sensitive, full name, `+0x2C` u16, linear dedup) + CORRECTED ("always"; cap in fill; sign-test DLC) | no | absent-`wwise_id` value (`0x00DABDF0`) |
| §7 `persona_radio_prefs` | CORRECTED (Wwise-id match on station `+0x140`; byte = index + 1) + CONFIRMED + empirical 242/242 | **yes** | — |
| §8 `foley_collision` | CORRECTED ("always" `Frequency`) + CONFIRMED (layout, double conversion, absent `Name` → 0) | yes (minor) | accessor NULL-node behaviour |
| §9 `foley_touch` | CONFIRMED (seed 0 / no limit / no XOR; u16 count) + CORRECTED ("always" `Frequency`) | no | consumer `0x00561BB0`/`0x00561A70`/`0x00561A90` |
| §10 `foley_engine` | CONFIRMED (layout, seed 0, no bound check, run-on indices) | no | capacity (HIGH CONFIDENCE 128); vehicle `+0x7AC` side |
| §11 `radio_stations` | CORRECTED (station 0: only bit 3; two name fields) + CONFIRMED (bit-5 semantics, enumerator on bit 6, absent `wwise_id` → 0) | no | station-0 bit 6 writer; `0x0055F580`; `+0x40`/`+0x80` writer (`0x0055C9F0`) |
| §12 `playlist_artist_track` | CONFIRMED (145 cap, join chain, `+0x190`/`+0x194`/`+0x5A4` roles, 1-based numbers) + CORRECTED ("always") | no | `+0x190`/`+0x194` writer (`0x0055C9F0`) |
| §13 `radio_activities` | CORRECTED (denominator 100.0; "always"; floats-only pre-zero) + CONFIRMED (row-order slots) | **yes** | — |
| §14 `radio_events` | CONFIRMED (`memset` count × 4 bytes, seed 0, case-insensitive `EventType` → −1, 16 bytes) + CORRECTED (absent `Name` skips nothing; `+0x2C` = radio-content index) | yes (minor) | NULL-name hash value; allocator zeroing; table population timing |
| §15 `commercial_events` | CONFIRMED (unsigned `< 30`, seed 0, first dword only, order before §16) | no | second dword reader; startup initialiser `0x00FEE100` |
| §16 `commercials` | CONFIRMED (offsets, stride 20, skip-not-create, seed 0 both sides) + CORRECTED (registry = the `0x013BBC18` table) | yes (minor) | entry creators `0x0055C9F0` / `0x0055C950` |
| §17 `voc_sb_line_sit` | CONFIRMED (bit-15 gate, filename, 600 list, `DMLV`/v1 header) + CORRECTED ("always" readers; `Persona_id` discarded) + empirical 0/265 boot-load | no | whether bit 15 is set outside the loader (`0x00465CC0`, `0x004673C0`, `0x00469460`) |
| §18.1 | HIGH CONFIDENCE, narrowed (loader side read) | no | `+0x5B` bit 6 writer; `0x00465CC0` |
| §20 | items 5 and 7 (two of three) closed; 3 and 6 advanced; 9 and 10 new | — | — |

Totals: 19 units with CONFIRMED — disassembly findings; 14 units with at least one CORRECTION (§2, §3, §4, §5,
§6, §7, §8, §9, §11, §12, §13, §14, §16, §17); 7 units fully cleared (§1.3 items 1 and 2, §3, §4, §5, §7, §13)
plus 3 cleared with a minor OPEN (§8, §14, §16); 10 units still carrying a substantive OPEN (§1.2, §2, §6, §9,
§10, §11, §12, §15, §17, §18.1). The index's one NOT-DUMPED address (0x0055F580, §11) stays on the list.

Team B relevance (consolidated): §2 (`voice` bit, DLC byte, `wep_` cache ignores DLC rows), §3 (`PlayTimers`
are integers), §4 (Doppler default 10.0), §7 (station byte = index + 1, matched by `wwise_id`), §11 (bit 5 has two
meanings; enumerator keys on bit 6 and the `+0x40` name), §12 (1-based station numbers; inclusive slot test), §13
(fraction, row order), §14 (`+0x2C` is a radio-content index; `EventType` → −1 when missing), §15 (negatives
dropped), §16 (commercials live in the shared radio-content table), §17 (do not open `.lm_pc` at table load).

Clean-room self-check: the whole file was grepped for decompiler auto-names (`iVar`, `uVar`, `piVar`, `puVar`,
`local_`, `uStack_`, `unaff_`, `extraout_`, `undefined`, `param_`, `LAB_`, `PTR_`) after the last unit was
written — result: zero hits outside this sentence; function/data citations are bare addresses and
`FUN_`/`DAT_` only. No `<!-- CONTINUE -->` marker remains; nothing outside the two output files was modified.

## Next-dump list (consolidated; also written to `nextdump_tables-audio-radio.txt`)

- §1.2: func 0x00F4C350, func 0x0046FC50
- §2: func 0x00466ED0, func 0x004673C0, func 0x004641F0, func 0x00DA7D20, func 0x00464820, func 0x00DA7930
- §5/§6/§8/§9 accessors: func 0x00DABDF0, func 0x00DABE80, func 0x00DABA10, func 0x00DABA40, func 0x0070B980
- §9: func 0x00561BB0, func 0x00561A70, func 0x00561A90
- §10: range 0x013BB8A0-0x013BBAA8, func 0x005592F0, func 0x005594E0, func 0x005593C0, func 0x00558FD0
- §11/§12/§16: func 0x0055F580, func 0x0055C9F0, func 0x0055C950, func 0x0055F270, func 0x0055D7D0,
  func 0x0055E000, func 0x0083E5C0, func 0x0083E200, func 0x0055BF90, func 0x0055BFA0
- §14: func 0x0055C0B0, func 0x00D9E8B0, ptrs 0x01119960 5
- §15: range 0x00FEE0F0-0x00FEE130, xref 0x013BBB0C, func 0x0055C070
- §17: func 0x00465CC0, func 0x00469460, func 0x0046B310, func 0x0046B3B0, range 0x01353760-0x01353780
