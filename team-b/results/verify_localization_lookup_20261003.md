# Localisation key lookup wired into `vint-doc` text rendering (2026-10-03)

Implements the queued follow-up to the text-rendering merge (`51e8747`/`a5359a5`): `spec-localization-
format.md`'s `.le_strings` format and key-hash lookup, wired into `tools/sr3_viewer.cpp`'s `text_tag`
resolution so a STRING `text_tag` is tried as a real localisation key before falling back to the raw
string, per that spec's own Sec6.1/Sec7 item 2 correction to the earlier CHOSEN behaviour.

## New library: `sr3locstr`

`include/sr3locstr/le_strings.h` + `src/le_strings.cpp`: the `.le_strings` on-disk format (Sec2: magic/
version/power-of-two bucket table/records) and the key-hash lookup (Sec6.1: first match across tables
searched in load order, hash 0 = no key, no cache). Reuses `sr3save::nameHash()` directly for the key hash
(Sec4: lower-cased seed-0 no-final-XOR CRC-32 - already documented in `include/sr3vintdoc/vint_doc.h` as
the same function Vint property-name hashing uses, so not a new derivation). Stores each table as a plain
owned `hash -> text` map rather than the real loader's zero-copy pointer-into-buffer scheme - the spec's own
Sec6.1 describes observable lookup behaviour, not storage shape, and nothing in this host needs in-place
buffer patching.

Two test executables, same convention as `sr3font`: `synthetic_le_strings_test` (hand-built byte buffers,
no game data - parsing, bucket collisions, malformed-file rejection, the multi-table search order, and the
spec's own `CONTROL_OKAY` -> `0xda87172c` known-answer hash check) and `real_le_strings_test` (the 10 US
tables from `misc.vpp_pc`/`CRREISH_GAME_CACHE_DIR`, exit 77 = SKIPPED when absent): the Sec2.4 worked
example (`voice_script_us.le_strings`, byte-exact), Sec1's per-table record counts, Sec4's known key->text
answers (`CONTROL_OKAY`/`CONTROL_YES`/`CONTROL_NO`/`MENU_TITLE_WARNING`/
`FOREIGN_CHARACTER_DETECTED_WARNING_MESSAGE`), and Sec8's validation sample (`MAINMENU_DOWNLOADS`,
`MENU_GAME_PAUSED`, `HORDE_MODE_GAMEPLAY_TITLE`, the plain word `default`).

## Wiring into `tools/sr3_viewer.cpp`

Loads the 10-11 US tables (Sec5.2 step 4's own order) from the same `--fonts`/`--textures` archives the
fonts already use (misc.vpp_pc holds both). `drawText()`'s source-text step (Sec14.2 step 2) changed:
- A STRING `text_tag` is tried via `lookupByKey()` first; a hit draws the resolved text, a miss falls back
  to the existing raw-string decode exactly as before this change (Sec7 item 2: "for any non-key text_tag
  string... the raw string is drawn").
- A key-hash form (`text_tag_crc`, or `text_tag` set as a number) resolves directly by hash (Sec6.3 slot
  `+0x04`); a hit draws the resolved text, a miss (never exercised by any shipped document, Sec8: "no
  text_tag_crc record ships") now falls to "no source text, nothing drawn" instead of staying permanently
  OPEN, since the lookup is a real, determinate answer either way. This closes the `text_tag_crc`/numeric-
  `text_tag` OPEN gap the original text-rendering merge carried forward - at effectively zero risk, since
  it is provably unreached by the real population (confirmed below) and uses the exact same lookup
  mechanism the string path already needs.
- `insert_values`' own two localisation-dependent branches (an unmodified string value tried as a key,
  `{N:text_tag_crc}`) are explicitly NOT wired this pass - also unexercised by any shipped document, and
  a separate call path (`src/vint_text.cpp`'s `formatInsertions`) from the one this pass touches. Left OPEN
  and documented, same honesty convention as the gap being closed above.

New stdout: a `[loc] N/11 US .le_strings tables loaded (... records total)` line per `vint-doc` run, and the
existing text summary line gained a `resolved via a real localisation key N` field.

## Verification

**Unit level**: 65 synthetic + real checks across the two new test executables, all passing, including the
spec's own known-answer hash (`CONTROL_OKAY` -> `0xda87172c`) and text (`CONTROL_OKAY` -> `OK`) against the
real shipped tables.

**Population level** (the strongest check): a one-off scratch probe (not committed, per this project's own
"helper scripts live in the scratchpad" convention already used by the spec team itself) walked all 159
distinct `.vint_doc` documents in `interface_startup.vpp_pc`, collected every `text_tag` record via
`sr3vintdoc::Document`'s real element tree, and ran each STRING value through this same `sr3locstr` lookup.
Result, matching spec Sec8's own figures **exactly**:

| | records | string | key-shaped | key-shaped resolved | other-shape resolved | **total resolved** |
|---|---|---|---|---|---|---|
| probe (this pass) | 513 (512 string + 1 empty) | 512 | 100 | 89 | 1 (`default`) | **90** |
| spec Sec8 | 513 | 512 | 100 | 89 | 1 | **90** |

The 11 key-shaped strings that do not resolve matched spec's own list exactly: `HELP_TEXT_CHEATS_SUBTITLE`,
`HELP_TEXT_MISSIONS_TITLE`, `CONTENT_DESCRIPTION`, `KEYS_TEXT`, `!!DIVERSION_TITLE`, `!!DIVERSION_VALUE`,
`!!WEAPON_TITLE`, `additive_alpha`, `TEXT_LOCALIZED`, `METER_LABEL` (x2).

**Build/test**: fresh build (`build_verify_player_input_merge`, continuing from the player-input merge
verification in the same session), `ctest -C Release`: **67/67 passed** (65 from the player-input merge +
`synthetic_le_strings_test` + `real_le_strings_test`).

**Golden scenes**: `tests/golden/build.bat` needed two additive changes - `src/le_strings.cpp` added
alongside the existing `vint_font.cpp`/`vint_text.cpp` compile line, and (unrelated to this pass, but
first surfaced by it) `src/player_input.cpp`/`src/player_control_state.cpp` added, since
`EngineState::EngineState()` has unconditionally constructed an `sr3player::InputSystem`/
`ControlStateMachine` since the player-input merge (`71b7fb1`) and this standalone script was never updated
for that merge (it does not itself touch `tools/sr3_viewer.cpp`, so the earlier merge correctly skipped it
at the time). **All 11/11 golden scenes PASS** after rebuilding, both `capture.png` and `stdout.txt`
byte-exact. Both `vint-doc` text goldens (`hud_mask`, `vint_hud_running_man`) needed a stdout-only
re-freeze: **pixels are byte-identical** (same MD5 as before this change for both PNGs - confirmed
directly, not assumed), since neither document's `text_tag` strings are real localisation keys (each run's
own new summary field reads "resolved via a real localisation key 0"); only the printed banner wording and
the new `[loc]`/summary lines changed. The previous baselines are archived at
`tests/golden/hud_mask_pre_locstr/` and `tests/golden/vint_hud_running_man_pre_locstr/` (same convention as
`_pre_text`/`_pre_exact`).

## Known gaps, carried forward honestly (not closed here)

- `insert_values`' own localisation-dependent branches (a string value tried as a key, `{N:text_tag_crc}`)
  stay OPEN - unexercised by any shipped document, left for a follow-up if `src/vint_text.cpp`'s
  `formatInsertions` ever needs the lookup threaded in.
- The 0x80-tagged `text_tag` string form (decoder not specced byte-exactly) is unrelated and unchanged.
- patch0 (`patch_compressed.vpp_pc`) is not loaded by either golden's own command line (neither passes that
  archive via `--textures`/`--fonts`), so this host's `vint-doc` runs only ever load 10/11 tables unless a
  caller adds it explicitly. Not a defect - matches the existing archive arguments every `vint-doc` command
  line already uses.
