# Saints Row: The Third — Audio Subsystem Specification

**Prepared by:** SPEC TEAM (cleanroom reverse-engineering process)
**Phase:** 2 (per `HANDOFF.md` §28.1/§28.5) — the audio boundary pass
**Scope:** Establish the boundary between third-party Audiokinetic Wwise middleware (out of cleanroom scope, publicly documented) and any Volition-specific wrapper, container, or trigger logic layered around it (in scope). Covers the six dedicated audio archives (`sounds.vpp_pc`, `sounds_common.vpp_pc`, `voices.vpp_pc`, `soundboot.vpp_pc`, `sound_turbo.vpp_pc`, `cutscene_sounds.vpp_pc`), the internal structure of the `_media.bnk_pc` wrapper format, and the engine code that connects Wwise's public API surface to the game's own resource system.
**Method:** (a) file-side empirical work against real shipped archives, using the already-confirmed `.vpp_pc` outer container (`spec-vpp-container.md`) via `tools/harnesses/parse_vpp.py` and new audio-specific harnesses (`tools/harnesses/audio_*.py`) that read raw entry bytes at their container-declared payload offsets — every rate below states its denominator, control, and predicate per this project's standing rule (`HANDOFF.md` §5/§27.6); (b) targeted Ghidra disassembly/decompilation (`SaintsRowTheThird.exe`, headless, scripts prefixed `Audio*` in `tools/scripts/`) starting from named entry points — the statically-linked Wwise export table and the custom hook object it calls back into — never a whole-binary predicate scan, per the project's own repeatedly-learned lesson against that approach.
**Cleanroom compliance:** No disassembly listings or decompiled source are reproduced verbatim; all code behaviour below is restated in this document's own words. No original Volition identifiers are used — Ghidra's auto-generated `FUN_00xxxxxx` addresses are cited only as addresses-as-evidence. Magic numbers, offsets, sizes, and other literal format data are exact and stated freely, per the project's cleanroom ground rules (`spec-vpp-container.md`'s header sets the precedent this document follows).

**Confidence key** (identical to every other spec in this project):
- **CONFIRMED — disassembly**: directly observed in the code that reads/writes/validates the field or behaviour.
- **CONFIRMED — empirical**: directly and repeatedly verified against real file bytes.
- **HIGH CONFIDENCE — inferred**: consistent, unforced pattern across independent samples/evidence, not fully closed by a direct trace.
- **HYPOTHESIS — unconfirmed**: a plausible reading that fits the evidence gathered so far but has a real, stated way it could still be wrong.
- **OPEN / UNKNOWN**: not determined in this pass, with a concrete next step named.

**Review status summary (2026-09-30).** Adversarial desk review (`review/adv_audio.md`), 22 units: DESK-PASS 2 (§3, §7.6); DESK-PASS, text fixes applied 5 (§1, §4.3, §7.1, §10, §11); VALIDATED-BY-DATA 3 (§4.1, §4.2, §5, backed by Team B's full-population 536/536 run, `team-b/HANDOFF.md` §9.57); NEEDS-EXE 9 (§6.1, §6.2, §6.3, §6.4, §7.2, §7.3, §7.4, §7.5, §8); NEEDS-DATA 3 (§2, §9.1, §9.2; §2 and §9.1 also had text fixes). A desk pass alone does not clear a unit — it was read for internal consistency, not re-derived from the executable; only the VALIDATED-BY-DATA units are already backed by Team B's full-population run. Awaiting the executable: §6.1–§6.4, §7.2–§7.5, §8 (including the §7.3/§7.4 vtable-slot conflict). Awaiting real data: §2 (2 unaccounted `sounds.vpp_pc` entries), §4.2 (`extra` placement, `tag` uniqueness — optional), §9.1 (`.mbnk_pc` walk), §9.2 (`DMLV` count).

---

## 1. Executive Summary — where the third-party/in-scope boundary actually falls

This pass answers the two questions `HANDOFF.md` §28.1 posed, plus a bounded look at the third:

1. **Is `VWSBPC` (the `_media.bnk_pc` wrapper) a genuine, undocumented Wwise chunk, or a Volition-authored wrapper?** **Resolved as a Volition-authored wrapper, `CONFIRMED — empirical` on the container/structure side and `HIGH CONFIDENCE — inferred` on the "not Wwise-native" side.** The wrapper has a fully characterised, self-consistent internal directory format (§4) **[desk review 2026-09-30: the directory walk is fully characterised; header fields `+0x08`/`+0x14`/`+0x1C` and the record `tag` remain OPEN, §4.1–§4.2]** that behaves nothing like any documented Wwise chunk, is never referenced by the string or byte patterns that mark Wwise's own chunk-tag dispatch code in the executable (§6.3), and cross-references its sibling genuine-Wwise bank by a field that is empirically identical to that bank's own Wwise-assigned SoundBankID in every case checked (§5). Wwise's own bank parser, found and read directly (§6.3), recognises `BKHD`/`DIDX`/`ENVS`-style tags and has no equivalent path for an 8-byte `VWSBPC` tag anywhere in the binary.
2. **Does audio bypass the generic resource-registration system, or is there a thin Volition wrapper around Wwise's own API?** **Neither pure reading is correct; the real shape is a third one, `CONFIRMED — disassembly` for its existence and `HIGH CONFIDENCE — inferred` for the exact field-level link to the `_media.bnk_pc` table.** Audio genuinely has no row in the master resource-registration table (`FUN_00700780`, confirmed absent — §7.1), but it is **not** loaded purely through Wwise's own API surface either: Wwise is statically linked (162 `AK::`-namespaced exported symbols, §6.1) and registers its **own public extension points** — a custom combined `IAkFileLocationResolver`/`IAkLowLevelIOHook` object — and Volition's code plugging into those extension points (§7.2–§7.4) is a real, disassembly-traced wrapper, small and specific to audio, entirely separate from the 43-row generic table.
3. **What drives playback triggering?** Scoped, not solved, per the task's own permission to do so. §8 names concrete next steps (a bank-record state machine with a small, countable set of category roots, and specific breakpoint targets) rather than forcing a premature answer from file-side data alone.

The practical boundary: **the raw sample/waveform payload and the plain `.bnk_pc` bank format are genuine, third-party Wwise data — out of cleanroom scope, same category as Bink video and the Steamworks shim (`spec-output.md` §1 (unpublished Team A working document, not in this repository)).** **The `_media.bnk_pc` block-directory wrapper, the small Volition-authored bank registry/state-machine, and the custom Low-Level I/O hook that bridges the two are Volition-specific engineering — in scope, and characterised below to the extent static analysis allows.**

**Review status (2026-09-30): DESK-PASS, text fixes applied — desk review (not re-derived from the executable).**

## 2. Archive Inventory and Extension-Variant Landscape

All six archives use the already-fully-characterised `.vpp_pc` outer container (`spec-vpp-container.md`); nothing new was needed at that layer. Header fields read directly with `tools/harnesses/parse_vpp.py`:

| Archive | Total size | Container flags | Compression mode | Entries | `_media.bnk_pc` entries |
|---|---|---|---|---|---|
| `sounds.vpp_pc` | 284,567,368 B | `0x00000000` | ~~(b) independent per-entry streams~~ raw (uncompressed) | 253 | 122 |
| `sounds_common.vpp_pc` | 653,412,352 B | `0x00000000` | ~~(b)~~ raw | 105 | 53 |
| `voices.vpp_pc` | 1,391,007,744 B | `0x00000000` | ~~(b)~~ raw | 275 | 275 |
| `soundboot.vpp_pc` | 4,141,080 B | `0x00004803` | ~~**(a) shared compressed stream**~~ **(b) shared stream** | 930 | 0 (uses `_media.mbnk_pc`, §9.1) |
| `sound_turbo.vpp_pc` | 1,274,082 B | `0x00004801` | ~~(b)~~ (a) per-entry streams | 550 | 0 (entries are `.lm_pc`, not `.bnk_pc` at all — §9.2) |
| `cutscene_sounds.vpp_pc` | 193,624,064 B | `0x00000000` | ~~(b)~~ raw | 172 | 86 |

**CONFIRMED — empirical.** All figures above are direct reads of container-header fields already given disassembly-level confidence in `spec-vpp-container.md` §1 (entry count `+0x154`, flags `+0x14C`, total size `+0x158`) **[desk review 2026-09-30: `spec-vpp-container.md` §1 labels `+0x158` total size `HIGH CONFIDENCE — inferred` (8/8 samples), not disassembly; `+0x154` and `+0x14C` are as stated]**; nothing here required new disassembly. **[⚠ Label corrected 2026-09-30: this column had the mode letters inverted relative to `spec-vpp-container.md` §3.3/§7.1 — flags `0x0` = raw (uncompressed), `0x4801` = mode (a) per-entry streams, `0x4803` = mode (b) shared stream. "Mode-(b) archives" elsewhere in this document means the four raw (`0x0`) archives.]**

**Every entry across the four ~~mode-(b)~~ raw (`0x0`) archives is exactly one of two shapes** (`CONFIRMED — empirical`, 253+105+275+172 = 805 directory entries read, zero exceptions) **[OPEN — desk review 2026-09-30: the per-archive split below does not close — `sounds.vpp_pc` 129 plain + 122 media = 251 of 253 entries, and the four archives total 267 plain + 536 media = 803 of 805, so 2 `sounds.vpp_pc` entries are unaccounted for (a third shape, or a miscount of plain banks); to be settled against the real data.]**: a plain `<name>.bnk_pc` beginning with the byte sequence `42 4B 48 44` (ASCII `BKHD`), or a `<name>_media.bnk_pc` beginning with `56 57 53 42 50 43 20 20 00 00 00 00 02 00 01 00` (ASCII `VWSBPC` padded to 8 bytes, then 8 more header bytes constant across every sample checked). `soundboot.vpp_pc` and `sound_turbo.vpp_pc` are the two exceptions to this shape, and both are exceptions for reasons unrelated to the audio format boundary itself — one is a compression-mode limitation, the other ships an entirely different, non-`.bnk_pc` file type under the same directory convention (§9).

**Not every plain bank has an in-archive `_media` sibling, and not every sibling lives in the same archive.** Measured directly (base-name intersection over the four ~~mode-(b)~~ raw (`0x0`) archives' directory entries, `CONFIRMED — empirical`):

- `sounds.vpp_pc`: 129 plain / 122 media; **7 plain banks have no in-archive media sibling** (`activities`, `diversions`, `ext_sources`, `init`, `interface`, `test_animatic_01`, `test_animatic_01_bink`).
- `sounds_common.vpp_pc`: 52 plain / 53 media; **1 media file has no in-archive plain sibling** (`interface_media.bnk_pc`).
- `voices.vpp_pc`: **0 plain / 275 media — every single entry is `_media`-only**, no in-archive plain counterpart at all.
- `cutscene_sounds.vpp_pc`: 86 plain / 86 media, fully paired.

The `interface` orphan on both sides is the same bank, split across archives: `sounds.vpp_pc`'s plain `interface.bnk_pc` and `sounds_common.vpp_pc`'s `interface_media.bnk_pc` share the identical cross-reference id described in §5 (verified directly, `CONFIRMED — empirical`, one pair checked byte-for-byte). **Media companions are not guaranteed to ship in the same physical `.vpp_pc` file as their plain bank.** `voices.vpp_pc`'s complete absence of any in-archive plain sibling is left as `OPEN` rather than explained: the most likely reading (`HYPOTHESIS — unconfirmed`) is that per-character dialogue events are defined once in a shared, not-yet-located bank (plausibly inside `soundboot.vpp_pc` or `sounds_common.vpp_pc`) rather than per character, with each `voices.vpp_pc` entry holding only that character's streamed audio — but this was not traced.

**Review status (2026-09-30): NEEDS-DATA: `sounds.vpp_pc` 253 entries vs 129 plain + 122 media = 251 (2 unaccounted; list those entries' names and first 4 bytes); text fixes applied — desk review (not re-derived from the executable).**

## 3. The Plain `.bnk_pc` Variant — Genuine Wwise SoundBank (Third-Party, Out of Scope)

The plain variant is a standard, publicly-documented Audiokinetic Wwise SoundBank. **CONFIRMED — empirical** for the magic (`spec-output.md` §3.3 already named Wwise as the statically-linked audio middleware; this pass confirms the on-disk bank format itself). The first 16 bytes decompose, using the well-known public Wwise `BKHD` chunk layout, as: magic `BKHD` (4 bytes), chunk data length (4 bytes, little-endian — read as `0x18`, `0x1C`, or similar small values across the samples checked here), a bank-generator version field (4 bytes — samples read `0x34`/`0x35`, i.e. decimal 52/53), and a **SoundBankID** (4 bytes) that is the load-bearing field for §5 below.

No cleanroom RE work is owed to this variant's internal chunk structure (`HIRC`, `STID`, `STMG`, etc. are all public Wwise chunk types); this document only goes as deep into it as needed to establish the cross-reference in §5 and the native chunk-dispatch evidence in §6.3.

**Review status (2026-09-30): DESK-PASS — desk review (not re-derived from the executable).**

## 4. The `_media.bnk_pc` Wrapper — Internal Structure (Volition-Authored, In Scope)

This is the primary new-format result of this pass: a complete, replay-verified internal directory structure for the `_media.bnk_pc` wrapper, built and checked with `tools/harnesses/audio_media_walk3.py`.

### 4.1 Header layout (offsets relative to the start of the entry's own payload, i.e. the first byte after the outer `.vpp_pc` container has located it)

**[Desk review 2026-09-30: the `u32` fields `+0x10`–`+0x1C` are little-endian — the reading under which Team B's reader validates `+0x10` 260/260 and `+0x18` 536/536 (`team-b/src/media_bank.cpp` lines 57–60, `team-b/HANDOFF.md` §9.57 lines 6876–6879).]**

| Offset | Size | Field | Confidence |
|---|---|---|---|
| `+0x00` | 8 | Magic, exact bytes `56 57 53 42 50 43 20 20` (ASCII `VWSBPC` + two space-pad bytes) | **CONFIRMED — empirical**, identical across all 536 files read (~~§1.3~~ §4.2) |
| `+0x08` | 8 | Constant in every sample checked: `00 00 00 00 02 00 01 00` (~~two `u16` fields reading `2` and `1`~~ **[desk review 2026-09-30: a `u32` reading `0`, then two `u16` fields reading `2` and `1`]**) | **OPEN** — role not determined; never varied, so no file-side test could distinguish "format version" from any other constant |
| `+0x10` | 4 | Cross-reference id. **Equals the sibling Wwise bank's own `BKHD` SoundBankID field exactly, in every case checked** | **CONFIRMED — empirical** (§5) |
| `+0x14` | 4 | Numeric field, varies per file, does **not** equal the real record count (ruled out directly — see the walk below) | **OPEN** — no candidate tested this pass fit it |
| `+0x18` | 4 | **Record count.** Equals the real, walked record count exactly in every file where the walk itself terminates cleanly | **CONFIRMED — empirical**, 536/536 |
| `+0x1C` | 4 | Numeric field, varies per file, no candidate tested this pass matched it | **OPEN** |
| `+0x20` | — | Start of the record table (~~§1.2~~ §4.2) | **CONFIRMED — empirical** |

**[OPEN — desk review 2026-09-30: this section never states the reader's behaviour for a file whose `+0x18` is `0`; Team B's validator counts such files (`g_zeroDeclaredCount`, `team-b/tools/validation/validate_media_bank.cpp` line 225) but no figure is recorded; to be settled against real data.]**

**Review status (2026-09-30): VALIDATED-BY-DATA: `+0x00` magic, `+0x10` 260/260, `+0x18` 536/536, table at `+0x20` (`team-b/HANDOFF.md` lines 6876–6879; `team-b/STATE.md` line 34); text fixes applied — desk review (not re-derived from the executable).**

### 4.2 Record table — a block-quantized directory, structurally analogous to the outer `.vpp_pc` container's own convention

Each record is **16 bytes**, four little-endian `u32` fields: `{offset, extra, size, tag}`.

- **`offset`**: this record's payload start, as a byte offset from the start of the `_media.bnk_pc` entry's own payload (i.e. from this wrapper's own `+0x00`). ~~Record 0's offset is always `0x800` — the first `0x800`-byte block after the header region.~~ **⚠ CORRECTED (Team B, independent re-verification, 2026-09-13): only true on 278 of 536 files.** The real rule, replay-verified across the full population: `offset[0] = round_up(0x20 + recordCount*16, 0x800)` — i.e. record 0 sits after the header *and* the full directory of `recordCount` 16-byte records, rounded up to the block-quantization boundary, mirroring the convention `spec-vpp-container.md` §1.1 documents for the outer `.vpp_pc` container. `0x800` is simply what this formula gives whenever `0x20 + recordCount*16 ≤ 0x800` (small directories) — the common case, not the rule. **CONFIRMED — empirical**, superseding the flat-constant reading above.
- **`extra`**: ~~zero in the overwhelming majority of records~~ **⚠ CORRECTED (Team B, independent re-verification, 2026-09-13) — the frequency claim was inverted.** Measured: **91.8% of records have `extra != 0`**; the earlier reading had the direction backwards. The underlying finding is unchanged and still holds: `extra` is a genuine, non-padding field, and ignoring it breaks the walk (~~§1.3~~ §4.3) — only the "how common is zero" direction was wrong, not the mechanism. It is itself always an exact multiple of `0x800` when non-zero (values observed across the full population: `2048, 4096, 6144, 8192, 10240, 12288, 14336, 16384`). **CONFIRMED — empirical** that it is not always zero (in fact rarely zero) and that ignoring it breaks the walk; **OPEN** what it represents semantically — a plausible but untested reading (`HYPOTHESIS — unconfirmed`) is a Wwise-style prefetch/zero-latency-streaming cache size, since Wwise's own public feature of that name reserves exactly this kind of small fixed extra block ahead of a streamed source's main data.
- **`size`**: this record's own payload length in bytes. **[OPEN — desk review 2026-09-30: whether the `extra` block precedes or follows the `size` payload inside the span `[offset, offset + round_up(size + extra, 0x800))` is not stated — the chain rule fixes only the total span; to be settled against real data (payload magic at `offset` vs `offset + extra`).]**
- **`tag`**: a 32-bit value that differs per record **[desk review 2026-09-30: no denominator — uniqueness not measured by this pass or by Team B]**; role not determined this pass (**OPEN** — see §7.4 for why this is the most valuable single field left to explain, since it is the most plausible candidate for the identifier Wwise's own streaming-source lookup would need to resolve a request against this table).

**Chain rule, replay-verified**: `offset[i+1] = offset[i] + round_up(size[i] + extra[i], 0x800)`. The walk terminates the moment the running cursor equals the container-declared uncompressed size of the whole `_media.bnk_pc` entry — i.e. the *outer* `.vpp_pc` directory's own `+0x0C` field for that entry (`spec-vpp-container.md` §2), an oracle the container itself publishes rather than an endpoint chosen by this pass. **CONFIRMED — empirical, 536/536 files across all four ~~mode-(b)~~ raw (`0x0`) archives (`sounds.vpp_pc` 122/122, `sounds_common.vpp_pc` 53/53, `voices.vpp_pc` 275/275, `cutscene_sounds.vpp_pc` 86/86), 89,631 individual records walked, zero chain breaks, zero overshoots.** Header field `+0x18` equals the real walked record count in the same 536/536 files — a second, independent confirmation using a value the wrapper itself declares.

**Review status (2026-09-30): VALIDATED-BY-DATA: 536/536 files, 89,631 records, zero chain breaks; `offset[0]` rule 536/536; `extra` nonzero in 82,261/89,631 records (91.8%) (`team-b/HANDOFF.md` lines 6876–6879, 6887–6890, 6904–6906); `tag` uniqueness and `extra` placement not measured — desk review (not re-derived from the executable).**

### 4.3 What a failing case would have looked like, and what the first (wrong) model's failure actually showed

Before the `extra` field was accounted for, the naive chain rule `offset[i+1] = offset[i] + round_up(size[i], 0x800)` ~~walked cleanly on 530/536 files and broke on exactly 6 — every one of the 6 diverging by precisely one extra `0x800` block at the exact record where `extra` was non-zero for that file.~~ **[⚠ SUPERSEDED 2026-09-30 by Team B's full-population figure: the naive model is clean on only 255/536 files and wrong on 281; 82,261 of 89,631 records (91.8%) carry `extra != 0`, across 281 of 536 files, and the naive-clean set is exactly the no-`extra` set (`team-b/HANDOFF.md` §9.57, lines 6902–6909). The "exactly one block" detail is also refuted by §4.2's own `extra` values, which run up to 16384 = 8 blocks.]** This is the load-bearing evidence that `extra` is a real, additive field and not reserved padding: a naive model that ignores it fails in a way that points directly at the fix, rather than failing uniformly or silently. **A second, separate failure mode is worth recording precisely because it looked like success:** an even earlier version of the walk continued past the real end of the table into the wrapper's zero-padded reserved space (the table is over-allocated relative to the real record count in every file **[desk review 2026-09-30: "every file" was not measured with a denominator by this pass or by Team B]**, headroom for `+0x14`'s still-unexplained value perhaps being the reason), and every all-zero record trivially satisfied `0 == 0 + round_up(0,0x800)` — silently reinflating the apparent match count. Restricting the walk to stop at the container's own declared total size, rather than trusting either in-wrapper count field blindly, is what actually closed this; see §11 for the general lesson.

**Review status (2026-09-30): DESK-PASS, text fixes applied (530/536 and "exactly 6" superseded by Team B's 255/536 clean, 281 broken) — desk review (not re-derived from the executable).**

## 5. Cross-Reference Between Plain and `_media` Banks

The wrapper header's `+0x10` field is empirically identical to its sibling Wwise bank's own `BKHD`-chunk field at offset `+0x0C` — the position the public Wwise SoundBank layout gives to the bank's SoundBankID — in **260 of 260 in-archive sibling pairs checked** across all four ~~mode-(b)~~ raw (`0x0`) archives (`sounds.vpp_pc` 122/122, `sounds_common.vpp_pc` 52/52, `cutscene_sounds.vpp_pc` 86/86; `voices.vpp_pc` contributes 0 checked pairs, since it has no in-archive plain siblings at all — §2). **CONFIRMED — empirical.** The match is exact (a 32-bit value, never a partial or truncated match) and the values vary freely across files (examples: `0x22dba0ef`, `0xadc617ee`, `0x8024d475`, ...), ruling out the trivial failure mode where a "match" is really just two constant fields agreeing by coincidence — a failing case here would have been any pair with differing 32-bit values, and none occurred.

**The cross-reference also works across archive boundaries, at least once, checked directly**: `sounds.vpp_pc`'s plain `interface.bnk_pc` (SoundBankID `0x40e182ea`) and `sounds_common.vpp_pc`'s `interface_media.bnk_pc` (cross-reference id `0x40e182ea`) are the same value, despite living in two different physical `.vpp_pc` files. This is consistent with Wwise's own SoundBankID being a global namespace (a hash of the bank's authored name, not scoped to any one archive) rather than a per-archive counter, and it means a general implementation cannot assume a bank and its media companion ship together.

This gives a precise, testable answer to the "is this Wwise-native" question from a completely different angle than the chunk-tag evidence in §6.3: **a genuine Wwise chunk field (SoundBankID) is being read and copied into a non-Wwise wrapper header at packaging time.** That is exactly what a Volition-authored build/packaging step would need to do to let the two files find each other again, and it is not something Wwise's own runtime would need to do on its own behalf — Wwise resolves streamed media by a source/media id, not by re-deriving a sibling bank's identity from inside a media package. The field's *existence* here is therefore itself a small piece of evidence for the wrapper being a packaging-time, Volition-side construct rather than a Wwise runtime artifact — offered as one strand among the several in §1's summary, not as a standalone proof.

**[Team B: 260/260 in-archive pairs plus the 1/1 cross-archive `interface` pair, with a distinct-id diversity control (`team-b/HANDOFF.md` lines 6878–6879; `team-b/tools/validation/validate_media_bank.cpp` header). `voices.vpp_pc` bank-id resolution remains unvalidated, as stated above.]**

**Review status (2026-09-30): VALIDATED-BY-DATA: 260/260 + 1/1 cross-archive (`team-b/HANDOFF.md` line 6878) — desk review (not re-derived from the executable).**

## 6. Statically-Linked Wwise SDK Surface

### 6.1 The export table, enumerated exactly

`spec-output.md` §1.1 estimated "~150" C++ mangled Wwise symbols in the executable's export table from a manual sample. This pass enumerated the PE export table directly via Ghidra's external-entry-point list (`tools/scripts/AudioDumpWwiseExports.java`): **217 total exports, of which 162 match an `AK::`/Wwise-related name pattern** — fully demangled, human-readable C++ names are present in the binary's own export table (e.g. `AK::SoundEngine::LoadBank`, `AK::StreamMgr::CreateDevice`, `AK::MemoryMgr::CreatePool`, `AK::MotionEngine::AddPlayerMotionDevice`, `AK::DynamicSequence::Break`). **CONFIRMED — empirical**, refining (not contradicting) the earlier estimate. The remaining 55 non-Wwise exports are the previously-identified c-ares (DNS) and a handful of others; none are audio-related. **[OPEN — desk review 2026-09-30: the 217/162/55 split and the make-up of the 55 cannot be replayed from the text; to be settled against the executable (re-list the export table).]**

**Review status (2026-09-30): NEEDS-EXE: export-table count 217/162/55 not replayable desk-side — desk review (not re-derived from the executable).**

### 6.2 The active codec is Ogg Vorbis, and the plugin roster is large — both disassembly-confirmed, not inferred

The engine's own Wwise-initialization routine (address `0x0046fdd0`) — reached from the game's startup path, not found by any string or magic search — sets up the stream manager and sound engine (calling the exported `AK::StreamMgr::Create`, `AK::SoundEngine::Init`, `AK::MusicEngine::Init`) and then explicitly registers roughly twenty Wwise SDK plugins by their numeric company/plugin id pairs (the standard `AK::SoundEngine::RegisterPlugin` call shape), followed by one explicit `AK::SoundEngine::RegisterCodec` call passing Wwise codec id `4` together with the SDK's own exported `CreateVorbisFilePlugin` entry point. **CONFIRMED — disassembly: the audio codec backing this game's Wwise integration is Ogg Vorbis** — itself a further public, third-party, out-of-scope codec layered inside the already-out-of-scope Wwise payload. This closes what would otherwise be an open question about the raw sample data's encoding without needing to touch a single sample byte. **[OPEN — desk review 2026-09-30: the codec id `4` / Vorbis registration and the "roughly twenty" plugin count at `0x0046fdd0` (distinct from the `0x0046fd00` cited in `spec-lua-api-behaviour.md`) are to be settled against the executable.]**

**Review status (2026-09-30): NEEDS-EXE: `RegisterCodec`/`RegisterPlugin` arguments at `0x0046fdd0` — desk review (not re-derived from the executable).**

### 6.3 Wwise's own native chunk-tag dispatch, located and read directly

Searching the executable's full loaded memory image for the literal ASCII strings `BKHD` and `DIDX` — both standard, public Wwise SoundBank sub-chunk tags — found **exactly one occurrence of each** (`tools/scripts/AudioFindVwsbpcString.java`, `AudioDumpChunkTagNeighborhood.java`). In both cases the string bytes sit **inline inside an x86 comparison instruction** (a four-byte immediate compare against the tag, followed by a conditional branch), not as a stray, unreferenced data string. The `DIDX` occurrence sits in the same short instruction run as a second immediate compare against `ENVS` (another standard Wwise chunk tag) — i.e. this is a genuine, multi-tag chunk-type dispatcher, structurally consistent with Wwise's own generic SoundBank sub-chunk walker. Both addresses (`0x00f562c1` for the `BKHD` compare, `0x00f59a13` for `DIDX`/`ENVS`) fall inside the same broad address range as the bulk of the `AK::`-exported function cluster, consistent with this being part of the statically-linked Wwise library rather than Volition's own code. **CONFIRMED — disassembly** that Wwise's own code, not a Volition wrapper, is what recognises and dispatches on these standard chunk tags. **[OPEN — desk review 2026-09-30: only `BKHD`/`DIDX`/`ENVS` were checked, and the two sites are `0x3752` bytes apart; whether other tag compares (`HIRC`, `STID`, `STMG`) sit in the same functions is to be settled against the executable.]**

**Review status (2026-09-30): NEEDS-EXE: view `0x00f562c1` and `0x00f59a13` and list the other tag compares — desk review (not re-derived from the executable).**

### 6.4 The decisive negative: `VWSBPC` is not checked the way `BKHD`/`DIDX` are checked, anywhere in the binary

The same search for the full 8-byte wrapper magic (`VWSBPC` padded) and every one of its 4-byte sub-fragments (`VWSB`, `WSBP`, `SBPC`, `BPC `, `PC  `) returned **zero hits, for all six patterns, anywhere in the executable's loaded memory image.** **CONFIRMED — empirical (a real negative, not an absence of search — the same tooling found `BKHD`/`DIDX` immediately).** This rules out one specific, plausible mechanism — a stored string or a single contiguous 4-byte immediate compare against the wrapper's magic — anywhere in the binary. It does **not** by itself prove the wrapper is never validated at runtime: a per-byte loop compare, or (per §7.4) a resolution path that never needs to re-check the magic because it locates records by a different mechanism entirely, would also produce this exact negative. Stated at the precision the evidence supports: **`HIGH CONFIDENCE — inferred`, not `CONFIRMED`, that no runtime code validates the `VWSBPC` magic the same way Wwise's own code validates `BKHD`/`DIDX`.** **[OPEN — desk review 2026-09-30: a check keyed on the extension strings (`.bnk_pc`/`_media.bnk_pc`/`.mbnk_pc`) rather than on the magic was not searched for; to be settled against the executable.]**

**Review status (2026-09-30): NEEDS-EXE: extension-string xrefs and a re-run of the six-pattern search — desk review (not re-derived from the executable).**

## 7. Engine Integration — Neither Bypass Nor Thin Passthrough: a Dedicated Low-Level I/O Hook

### 7.1 No row in the generic resource table

`spec-format-inventory.md`'s master table (built from the registered-constructor table at `FUN_00700780`, already established elsewhere in this project as exhaustive at 43 rows) contains no row naming any audio extension — verified directly against that document's text this pass (a plain text search for "bnk"/"audio"/"sound"/"wwise", case-insensitive, zero matches). **CONFIRMED**, no new disassembly needed since the table's exhaustiveness was already established by earlier work. **[Desk review 2026-09-30: this `CONFIRMED` rests on the cross-reference to `spec-format-inventory.md` (43 rows, line 15), not on a re-read of `FUN_00700780`; that table's rows are type names with their extensions, so the search shows no audio type, not merely no audio extension.]**

**Review status (2026-09-30): DESK-PASS, text fixes applied — desk review (not re-derived from the executable).**

### 7.2 Wwise's own public extension points are what audio uses instead

Wwise's SDK exposes two public callback interfaces for exactly this situation — a file-location resolver and a low-level I/O hook, intended for a game to plug its own archive/file scheme into Wwise's streaming manager. The engine's Wwise-startup routine (`0x0046fdd0`, §6.2) calls a device-setup helper (`0x0046b850`) which: confirms the requested device's storage-medium field equals a fixed constant; creates a named memory pool (an embedded ASCII debug label reads "Audiolib Stream Manager"); and then calls the exported `AK::StreamMgr::CreateDevice`, passing a single fixed-address object as **both** the `IAkFileLocationResolver*` and (moments earlier, via `AK::StreamMgr::SetFileLocationResolver`) the same role again. **CONFIRMED — disassembly.** **[OPEN — desk review 2026-09-30: as written this sentence names the resolver role twice; in the public Wwise SDK `SetFileLocationResolver` takes the resolver and `CreateDevice` takes the low-level I/O hook, so which pointer (and which of §7.3's two vtables) each call receives, and the storage-medium constant's value, are to be settled against the executable at `0x0046b850`.]**

**Review status (2026-09-30): NEEDS-EXE: call arguments and medium constant at `0x0046b850` — desk review (not re-derived from the executable).**

### 7.3 The hook object: one combined class, two interface vtables

That fixed-address object's constructor (`0x0046b690`) is a plain global C++ constructor: it assigns two vtable pointers, recovered statically at addresses `0x0129f13c` and `0x0129f14c` — **sixteen bytes apart**, consistent with a single class implementing two interfaces via the standard MSVC multiple-inheritance vtable-adjustment-thunk layout (one combined object presenting itself as both an `IAkFileLocationResolver` and an `IAkLowLevelIOHook`) — and zero-initializes a large set of buffers in fixed-size chunks, consistent with a pre-allocated streaming read-ahead pool. **CONFIRMED — disassembly** for the vtable addresses and the construction sequence; the buffers' exact role is **HIGH CONFIDENCE — inferred** by shape (matches the standard Wwise sample I/O hook's own buffer-pool pattern), not traced field-by-field.

**[⚠ CONFLICT — desk review 2026-09-30: `0x0129f14c` − `0x0129f13c` = `0x10`, so the table at `0x0129f13c` can hold at most four 4-byte slots (0–3) before the second table begins; §7.4 cites slots 1, 5 and 11 (`0x0046b970`, `0x0046ba20`, `0x0046bad0`) of "the recovered vtable", and slot 11 needs at least 48 bytes. The two figures fit only if §7.4's vtable is the one at `0x0129f14c`, or if these two addresses are something other than the starts of the two vtables; the text does not say which. To be settled against the executable (`0x0046b690`; dump both tables with slot counts).]**

**Review status (2026-09-30): NEEDS-EXE: 16-byte vtable spacing vs §7.4 slots 1/5/11 — desk review (not re-derived from the executable).**

### 7.4 The Open()-shaped method, and what it delegates to

Reading the recovered vtable directly (`tools/scripts/AudioDumpIOHookVtables.java`), slot 1 (`0x0046b970`) matches the shape of an `Open()`-style method: it takes a requested identifier and an output record, allocates a fixed-size record through a generic allocator, and calls a second function (`0x00465830`) to actually resolve the request. That resolver:

1. First searches an existing in-memory table via a "find by handle" call.
2. **Only if that lookup misses**, falls back to iterating a small, fixed list of up to eleven slots (address `0x02cea950`) and calling a per-slot search routine (`0x004637a0`) on each populated one — consistent in count with "one slot per audio archive category plus headroom," though this pass did not confirm the six known archives map one-to-one onto specific slots.
3. Once a candidate record is found, reads a category/type value from it and uses that to index a second small table (`0x0321c5d0`), yielding a context pointer.
4. Branches on a flag bit (bit `0x40`) read from offset `+0x5b` of that context — **the same byte offset, in the same 0x5c-byte-stride record shape, that the load/unload state machine in §7.5 manages** — before completing the resolution through further generic, non-audio-specific calls.

**CONFIRMED — disassembly** for this entire call chain and its branching structure. **HIGH CONFIDENCE — inferred, not confirmed this pass**, that the byte range this resolver ultimately returns is read out of a `_media.bnk_pc`/`.mbnk_pc` record table exactly as characterised in §4: the trace was not carried far enough into the final generic calls (`0x00467110`, `0x004637a0` itself, `0x00d9e740`) to see a literal displacement matching that table's own `+0x20` base and `16`-byte stride. This is the single most valuable next step named in this document (§7.6, §8) — closing it would upgrade the whole "wrapper feeds the I/O hook" account from inferred to disassembly-confirmed.

The matching `Close()`-shaped method (vtable slot 5, `0x0046ba20`) decrements the same record's reference count and frees it through a generic deallocator; a further slot (11, `0x0046bad0`) walks the same hash-bucket record list to dispatch a completion callback per matching entry. All of this is one coherent, small, request-tracking subsystem specific to audio I/O — not a rediscovery of the generic 43-type table, and not Wwise's own code either. **[OPEN — desk review 2026-09-30: which vtable the slot numbers index (see the §7.3 conflict marker), the stride of the slot list at `0x02cea950`, the entry size of the table at `0x0321c5d0`, and whether the context from that table is the same `0x5c`-byte record as §7.5 are not given; to be settled against the executable.]**

**Review status (2026-09-30): NEEDS-EXE: `0x0046b970`, `0x00465830`, `0x004637a0`, `0x00467110`, `0x00d9e740`; globals `0x02cea950`, `0x0321c5d0` — desk review (not re-derived from the executable).**

### 7.5 A second, independent point of contact with the same record type: the bank load/unload state machine

A separate function (`0x00465cc0`) — not reached from the hook object at all, so this is a second and independent confirmation of the record shape, not a re-reading of the same code path — walks several bucketed lists of the identical `0x5c`-byte-stride bank record every tick. Based on flag bits at each record's own offsets `+0x54` and `+0x5b`, it calls Wwise's own exported bank-loading or bank-unloading entry point — specifically the overload that takes a **numeric bank id** (read from the record's own `+0x40`) plus an asynchronous completion callback and a cookie (the record's own address), never a filename or an in-memory buffer pointer. Of the eight `LoadBank` overloads and the several `UnloadBank` overloads statically linked into the binary, **only this one `LoadBank` overload (and two `UnloadBank` overloads) have any caller anywhere in the executable** — every filename-based and buffer-based overload has zero call references. **CONFIRMED — disassembly**: audio banks are requested from Wwise exclusively by numeric id, through one reference-counted, Volition-authored state machine, never by handing Wwise a filename or a pre-loaded buffer directly. **[OPEN — desk review 2026-09-30: the overload caller census and the widths of the `+0x40` and `+0x54` fields are to be settled against the executable (xrefs to each `LoadBank`/`UnloadBank` export).]**

**Review status (2026-09-30): NEEDS-EXE: overload census and record field widths at `0x00465cc0` — desk review (not re-derived from the executable).**

### 7.6 Answering the either/or framing precisely

Neither of the two readings `HANDOFF.md` §28.1 posed is correct as stated. It is not "loaded entirely through Wwise's own third-party API surface" — Wwise's public API is a set of extension points that something else must fill in, and that something (§7.2–§7.5) is real, disassembly-traced, Volition-authored code with its own small state machine and its own record format. It is also not "a thin wrapper function that hands a buffer to Wwise's own bank-loading call" — no such buffer-passing call exists anywhere in the binary (§7.5's LoadBank-overload census is the direct evidence). The actual shape is a **third one**: a dedicated, small, request-tracking subsystem that plugs into Wwise's own published Low-Level I/O extension points and drives Wwise's bank lifetime purely by numeric id — separate from, and never touching, the generic 43-row resource table.

**Review status (2026-09-30): DESK-PASS (inherits the §7.4 open link) — desk review (not re-derived from the executable).**

## 8. Playback Triggering — Scoped for a Dynamic Pass, Not Solved Here

Per the task's own instruction to scope rather than force a premature answer, this section names exactly what is known, what is not, and where to breakpoint.

**What is known (`CONFIRMED — disassembly`, §7.5):** bank loading and unloading is not ad hoc — it is settled once per tick by a single state-machine function (`0x00465cc0`) that walks several bucketed lists of a shared `0x5c`-byte bank-record type, and issues `AK::SoundEngine::LoadBank`/`UnloadBank` calls keyed by numeric bank id whenever a record's own flag bits (at `+0x54` and `+0x5b`) say "wants loading" or "wants unloading". This function **settles** the state machine; it does not appear to originate the requests.

**What is genuinely open:** what sets those flag bits in the first place. Candidates, none tested this pass:

- A data-driven table (an `.xtbl`, per this project's own established convention for tunable gameplay data — `spec-xtbl-format.md`) naming which banks belong to which mission, zone, or trigger.
- The Lua scripting layer already documented in `spec-lua-bindings.md`, which that document's own §6 already flags as "would likely be resolved faster with a dynamic pass... than more static analysis" for its own open items — the same shape of problem.
- Hardcoded per-subsystem calls (mission code, cutscene code, zone-load code) writing the flag bits directly, with no single data table involved at all.

This was not distinguished this pass, and file-side data alone is unlikely to distinguish it — exactly the same situation `spec-anim-format.md`'s keyframe payload and `spec-morph-format.md`'s value decode were in before they were opened by disassembly-driven, formula-deriving work rather than a container hunt. **A well-characterised "this needs a dynamic pass, and here is exactly where to breakpoint" is the honest, useful result here.**

**Concrete next-step targets, named precisely so a future session does not have to re-derive them:**

1. **Breakpoint `0x00465cc0` itself** (the settling function), or its three call sites onward into Wwise (`LoadBank` at `0x00465e34`, `UnloadBank` at `0x00466072` and `0x0046615c`) — watch which bank ids get requested as specific missions/zones/cutscenes load, and correlate against the already-known bank names from §2's directory listings.
2. **Watch the write sites to bank-record offsets `+0x54`/`+0x5b`** (the flag bits `0x465cc0` reads) — finding what sets them is what actually answers "what triggers a load," since `0x465cc0` only reacts to them.
3. **Instrument the Open()-shaped hook method (`0x0046b970`, §7.4) directly**, and compare the byte range it returns against the already-known, per-file record tables from §4 — this is the concrete, byte-exact test that would upgrade §7.4's "high confidence, inferred" link between the wrapper table and the I/O hook to disassembly-confirmed, and it is cheap precisely because §4 already gives an oracle (the declared total size, the record boundaries) to check a live read against.

**Review status (2026-09-30): NEEDS-EXE: write-xrefs to bank-record `+0x54`/`+0x5b` — desk review (not re-derived from the executable).**

## 9. Adjacent, Unresolved Extension Variants — Flagged, Not Characterised

Two further file types surfaced during this pass that are genuinely audio-adjacent but outside this document's specific BKHD/VWSBPC boundary question. Recorded here per this project's own scope-jump discipline (`HANDOFF.md` §5): noticed, flagged, not chased.

### 9.1 `_media.mbnk_pc` in `soundboot.vpp_pc`

Same `<name>.bnk_pc` / `<name>_media.mbnk_pc` naming shape as the main wrapper, but `soundboot.vpp_pc` is a ~~mode-(a)~~ **mode-(b)** shared-compressed-stream container (container flags `0x00004803`, §2), ~~where only directory entry 0 reliably decompresses independently~~ — an already-closed, general limitation of this container mode (`HANDOFF.md` §5's Entry-0-only rule), not an audio-specific gap. Entry 0 in this archive is a plain `.bnk_pc` and decompressed cleanly to a genuine `BKHD` chunk, consistent with §3. Its `_media.mbnk_pc` companion is entry 1 and was **not** independently decodable this pass. **OPEN.** **[⚠ SUPERSEDED 2026-09-30: `soundboot.vpp_pc` is mode (b) (`0x4803`) and all 930 of its entries extracted with 0 errors (`spec-ai-behavior-format.md` §2); the entry-0-only rule is itself superseded (`spec-vpp-container.md` §7). Decoding is no longer the blocker — what remains open is only the `.mbnk_pc` chain-walk, not yet run.]** Next step: ~~run this project's general mode-(a) shared-stream decompression (not something audio-specific) over the whole archive, then~~ **[superseded 2026-09-30: extraction already works, see the note above]** re-run `tools/harnesses/audio_media_walk3.py`'s chain-walk against the recovered bytes to check whether `.mbnk_pc` is byte-for-byte the same wrapper under a different extension, or something else. **[OPEN — desk review 2026-09-30: the chain-walk on entry 1 has not been run by this pass or by Team B (whose validator excludes `soundboot.vpp_pc`); to be settled against real data.]**

**Review status (2026-09-30): NEEDS-DATA: `.mbnk_pc` chain-walk on `soundboot.vpp_pc` entry 1; text fixes applied — desk review (not re-derived from the executable).**

### 9.2 `.lm_pc` in `sound_turbo.vpp_pc`

Entries share exactly the same base names as `voices.vpp_pc`'s per-character banks (`voc_angel.lm_pc`, `voc_anna.lm_pc`, `voc_brute.lm_pc`, ...) but begin with a distinct 4-byte magic, ASCII `DMLV`, not seen anywhere else in this project and not a public Wwise chunk tag. **CONFIRMED — empirical** for the magic and the naming correspondence; **OPEN** for everything else, including what `DMLV` stands for — a per-character lip-sync or line-timing metadata table is a plausible gloss given the naming correspondence with voice banks (`HYPOTHESIS — unconfirmed`, no further evidence gathered). This is genuinely new territory, outside this pass's assigned boundary question, and is flagged for a future scoping pass rather than pursued here. **[OPEN — desk review 2026-09-30: the number of the 550 `sound_turbo.vpp_pc` entries actually checked for `DMLV` is not stated (`spec-ai-behavior-format.md` line 27 records 528/550 extraction failures at the time, since resolved by `spec-vpp-container.md` §7); to be settled against real data.]**

**Review status (2026-09-30): NEEDS-DATA: count of 550 entries beginning `DMLV` — desk review (not re-derived from the executable).**

## 10. Summary: Confirmed, Refuted, Open

**CONFIRMED — empirical:**
- Plain `.bnk_pc` begins with the public Wwise `BKHD` magic (§3), matching `HANDOFF.md` §28.1's finding, now with the internal `_media.bnk_pc` wrapper structure fully characterised (§4) on top of it.
- The `_media.bnk_pc` wrapper's 16-byte record table, its block-quantized chaining rule, and its termination against the outer container's own declared size — 536/536 files, 89,631 records, zero exceptions (§4).
- The wrapper's `+0x10` cross-reference field equals its sibling Wwise bank's own SoundBankID exactly — 260/260 in-archive pairs, plus one verified cross-archive pair (§5).
- 162 statically-linked, fully-demangled `AK::`-namespaced exports; Ogg Vorbis is the registered Wwise codec (§6).
- Audio has no row in the master 43-type resource table (§7.1); it is driven exclusively by numeric bank id through one reference-counted Volition state machine, never by filename or buffer (§7.5).

**CONFIRMED — disassembly:**
- Wwise's own code (not Volition's) dispatches on `BKHD`/`DIDX`/`ENVS` chunk tags via inline immediate compares (§6.3).
- A combined `IAkFileLocationResolver`/`IAkLowLevelIOHook` object, Volition-authored, is registered with Wwise's stream manager at startup, and its Open()-shaped method delegates to a resolver that reuses the same bank-record type the load/unload state machine manages (§7.2–§7.5).

**REFUTED / ruled out:**
- `VWSBPC` is **not** checked via any embedded string or single 4-byte immediate compare anywhere in the binary, unlike `BKHD`/`DIDX` — a real, six-pattern negative (§6.4).
- Audio is **not** loaded via any filename-based or in-memory-buffer `LoadBank` overload — all such overloads have zero callers; only the numeric-id-plus-callback overload is ever used (§7.5).
- The wrapper header's `+0x14` field is **not** the record count (ruled out directly against the walked total); `+0x18` is (§4.1).
- ~~An early, uncorrected chain-walk model that ignored the `extra` field produced a false 530/536-looking pass rate by trivially matching zero-padded reserved table space beyond the real record count — recorded and corrected in place, not smoothed over (§4.3, §11).~~ **[Corrected 2026-09-30 to match §4.3, which records two separate findings:]** (1) the model that ignored `extra` ~~genuinely walked 530/536 files and broke on exactly 6, each by one `0x800` block at the record where `extra` was non-zero~~ **[⚠ superseded 2026-09-30: clean on only 255/536, broken on 281 (`team-b/HANDOFF.md` lines 6904–6906); see §4.3]**; (2) a separate, even earlier walk that ran past the real end of the table inflated its match count by trivially matching zero-padded reserved table space (§4.3, §11).

**OPEN, each with a next step already named above:**
- Header fields `+0x08`, `+0x14`, `+0x1C`, and the per-record `tag` field's exact role (§4.1–§4.2).
- The `extra` field's semantic identity — plausibly a Wwise prefetch/zero-latency-streaming allowance, untested (§4.2).
- Whether the Open()-hook's resolution path reads this exact table structure byte-for-byte, or some other structure with the same declared-size oracle (§7.4, §8 item 3).
- What sets the load/unload state machine's own trigger flags — data table, Lua, or hardcoded calls (§8).
- Whether `_media.mbnk_pc` (`soundboot.vpp_pc`) is the same wrapper ~~under mode-(a) compression~~ (the archive is mode (b) and fully extractable, §9.1 note; the walk is not yet run), and what `.lm_pc`/`DMLV` (`sound_turbo.vpp_pc`) actually is (§9).

**Review status (2026-09-30): DESK-PASS, text fixes applied — desk review (not re-derived from the executable).**

## 11. Methodology Notes From This Pass

Recorded here in the format `HANDOFF.md` §5 uses, for anyone reusing this table format or this class of hook object:

- **A record-count-looking header field can be a decoy; the container's own declared total size is a better terminating oracle than any header field claiming to be a count.** This pass had two candidate "count" fields (`+0x14` and `+0x18`) before checking either against a real walk; walking to the outer container's own declared payload size — a value the format was already known to publish, per this project's own standing preference for a declared oracle over a chosen endpoint (`HANDOFF.md` §5, "an oracle the file declares beats an endpoint you choose") — is what actually identified which one was real.
- **A zero-padded tail can inflate a match rate silently, and the exact way it does so is worth stating precisely rather than just avoiding.** An early chain-walk that did not restrict itself to the real record count kept "matching" through the wrapper's reserved-but-unused table space, because `0 == 0 + round_up(0, 0x800)` is trivially true — the same general vacuous-predicate trap this project's own `HANDOFF.md` §5 already names for other formats, recorded here as its audio-specific instance since the failure mode (walking into structured-looking zero padding rather than an obviously-corrupt tail) is easy to reproduce with any similarly over-allocated table.
- **A field you might assume is padding because it is usually zero can be a genuine, load-bearing secondary field.** The `extra` word in each 16-byte record read as zero often enough to look like reserved space **[⚠ superseded 2026-09-30: `extra` is non-zero in 91.8% of records (`team-b/HANDOFF.md` lines 6904–6906, §4.2); the lesson stands, but the field was not usually zero]**; the tell that it was not was that a model ignoring it failed in a *specific, informative* way (every failure diverging by exactly one block, at exactly the record where the field was non-zero **[⚠ superseded 2026-09-30: 281/536 files fail and `extra` reaches 8 blocks, §4.3]**) rather than failing randomly or not at all.
- **An absence-of-a-string-or-immediate result is a real negative, but only for that one specific checking mechanism.** Finding zero occurrences of `VWSBPC` and every 4-byte sub-fragment, anywhere in the binary, is strong evidence against one plausible design (a magic-gated parser reading this wrapper the way Wwise's own code reads `BKHD`/`DIDX`) — but it cannot, by itself, prove the field is never validated at all, since a per-character loop or an entirely different resolution path (not needing to re-check a magic it never has to look at) would leave the identical trace. Both readings are stated in §6.4 rather than collapsing to the more interesting-sounding one.
- **Two public interfaces can share one combined object's vtable, a fixed small offset apart.** Recovering the standalone constructor (rather than trusting the object's live, runtime-populated memory image, which reads all-zero in a static analysis) was what actually found the two vtable addresses; a future pass reading another Wwise-style multi-interface hook in this binary should expect the same shape and go straight to the constructor rather than the object. **[See the §7.3 conflict marker: the "small offset" is the 16-byte gap between the two table addresses, which does not fit §7.4's slot 11 on the first table.]**

**Review status (2026-09-30): DESK-PASS, text fixes applied — desk review (not re-derived from the executable).**

## Changelog

- 2026-09-30 (cloud consistency review, `review/spec-consistency.md`): corrected the inverted compression-mode labels in the §2 table (`0x0` raw, `0x4801` mode (a), `0x4803` mode (b)) with a note on the "mode-(b) archives" wording; marked §9.1/§10's soundboot entry-0 limitation superseded (`spec-ai-behavior-format.md` §2, `spec-vpp-container.md` §7); fixed 3 cross-references (§1.3→§4.2, §1.2→§4.2, §1.3→§4.3); split §10's misdescribed 530/536 finding into §4.3's two findings; flagged the `spec-output.md` citation as unpublished.
- 2026-09-30 (cloud, self-containment pass): restated 0 load-bearing HANDOFF/WALLS-only facts inline; repointed 0 `HANDOFF.md` §27.x references to the archived headings; bare "§5" meaning HANDOFF made `HANDOFF.md` §5 (§11); 1 left (see review): the Method line's `HANDOFF.md` §27.6 (no copy of §27.6 carries the rate/denominator rule; §5 does).
- 2026-09-30 (format desk review, `review/adv_audio.md`): added review-status lines to all 22 units and a front-matter summary; marked §4.3/§10/§11's 530/536 "exactly 6" and "usually zero" text superseded by Team B's 255/536 clean, 281 broken, 91.8% `extra != 0` (`team-b/HANDOFF.md` lines 6902–6909); added OPEN markers for §2's 253 vs 129+122=251 count, `extra` placement, `+0x18 == 0`, §6.x/§7.x executable checks and §9.1/§9.2 data checks; added a CONFLICT marker for §7.3's 16-byte vtable spacing vs §7.4's slots 1/5/11; fixed the `+0x08` gloss, the `+0x158` confidence attribution, 4 "mode-(b)" archive references, the stale §9.1 next step, and qualified §7.1's CONFIRMED basis.
