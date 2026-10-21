# Differential test results: original game vs rebuilt code (Team D)

**What this is.** Team D runs single routines of the original DX9 game executable on chosen inputs (emulated, using the real shipped bytes, no live game), runs the matching rebuilt Team B function on the same inputs, and compares the two. This file reports only data: inputs, the original's outputs, the rebuilt outputs, and plain-language rules that reproduce the original's outputs exactly. Larger vector sets are in `spec-differential-vectors-progression.md` (round 1), `spec-differential-vectors-combat.md` (round 2) `spec-differential-vectors-tables.md` (round 3) `spec-differential-vectors-xml.md` (round 4) `spec-differential-vectors-engine.md` (round 5) `spec-differential-vectors-ai.md` (round 6) and `spec-differential-vectors-cash.md` (round 7) next to this file.

**Rebuilt code compared:** Team B snapshot `7436a78` (2026-10-19), compiled unchanged as 64-bit release code.

**Status words.** MISMATCH = the rebuilt code gives a different answer than the original on at least one input. MATCH = no difference on any recorded input. Every "Rule" below was checked by re-deriving the original's output for every recorded case from the rule alone (100% agreement unless stated).

## Summary (rounds 1 to 7, 2026-10-19)

| # | Area (spec section) | Cases | Mismatches | Severity |
|---|---|---:|---:|---|
| D-1 | Respect multiplier and per-kill factor rounding (spec-tables-progression B.1.1, B.1.2 rows 1, 5, 13) | 7,998 + 1,568 | 170 + 21 | **HIGH**: visible in normal play with any bought respect bonus |
| D-2 | Diversion trick reward formula (B.1.2 row 1) | 2,040 | 428 | **HIGH**: cash payouts are often $1 too high |
| D-3 | Reward-toast finalisation (B.1.2 row 1, toast grant) | 2,880 | 2,490 | **MEDIUM**: respect-or-cash rule, cash rounding, notification rule |
| D-4 | Respect award routine and level-up loop (B.1.1, B.2) | 2,000 | 32 | LOW: only 32-bit overflow inputs |
| D-5 | Rotate-left-6 / XOR name hash (spec-vpp-container 2.2) | 625 | 296 | LOW: only names containing bytes 128-255 |
| D-6 | CRC-32 name hashes, lower-cased and case-sensitive | 3,750 | 0 | MATCH |
| D-7 | Dollars to raw cash (x100, clamp) | 616 | 0 | MATCH |
| D-8 | Activity completion bonus (B.1.2 row 4) | 4,902 | 58 | LOW: only negative or huge Props_Award |
| D-9 | Shotgun pellet factor for NPC shooters (spec-weapons-runtime 4.5.3) | 384 (+ random-draw table) | 0 at the lowest draw; **the draw is missing** | **HIGH**: NPC shotgun damage at range is too low |
| D-10 | Base weapon damage with distance falloff (4.5.3) | 5,712 | 364 | LOW: last-bit rounding; inverted ranges |
| D-11 | Damage assist, low-health mercy (4.5.4 step 10c) | 1,853 | 540 | LOW: last-bit rounding only |
| D-12 | Health-regeneration wait after damage (4.5.4 step 19, spec-player-runtime 18.3) | 660 | 21 | LOW: 1 ms (hard 7,799 not 7,800) |
| D-13 | Health-regeneration ticks (4.5.5) | 1,512 | 116 | LOW: full health one tick earlier or later |
| D-14 | Table float grammar (spec-tables-traffic-ai 1.3, spec-xtbl-format 8) | 485 + 6,000 stress + real-data census | 144; **20,769 shipped values** | **MEDIUM**: 6.5% of shipped numeric table values read 1 bit off; exponent and long-integer edge cases |
| D-15 | Table element readers: signed / unsigned 32-bit, bool, unsigned 8-bit; integer grammar | 96 + 230 | 0 (one hex edge) | MATCH |
| D-16 | Table XML document parser (spec-xtbl-format 7) | 140 documents + 542 shipped files | 94; 447 shipped files differ | **MEDIUM**: text is trimmed; first text run only; strict rejection rules |
| D-17 | Vehicle engine torque curve (spec-physics-runtime 10.2) | 1,620 shipped-engine cases (+5,400 model check) | 1,620 | **HIGH**: field roles are not crossed; curve shape differs everywhere |
| D-18 | Engine RPM update (10.2) | rule + vectors | n/a | rule for the port |
| D-19 | Automatic gear shifting (10.3) | 12 sequences | 12 | **MEDIUM**: no cooldowns, no mid-shift block, strict thresholds |
| D-20 | Steering angle limiter (10.1, 10.11.7) | 15 sequences + 4,000 single calls | 7 | **MEDIUM**: reversal seed missing; exact rule given |
| D-21 | Notoriety gain and decay (spec-ai-runtime 19.8) | 98 sequences | 44 | **MEDIUM**: negative / zero points, remainder kept at the floor, entry floor |
| D-22 | Cash wallet add (spec-tables-progression B.1.1 note, 4.4 row 38) | 684 | 363 | **HIGH**: cash-bonus multiplier never applied; 32-bit wrap to 0 near the cap |
| D-23 | Sun light direction (spec-tables-environment 19.2) | 11 hours (+ 19.1-19.3 vectors confirmed) | 11 | **MEDIUM**: stand-in sun path; original never sets, peaks at 13:30 |

## D-1. Multiplier products are rounded to single precision before rounding to an integer — MISMATCH (HIGH)

Wherever the original computes "round(multiplier x amount)" (Lua `game_award_respect`, the reward-toast respect grant, the gang-kill per-kill factor), it first rounds the product to a **single-precision float**, then rounds that float to the nearest integer **with halves away from zero**. The rebuilt code rounds the exact double-precision product. Because 1.05, 1.15, 1.3 etc. are not exact in single precision, the single-precision product often lands exactly on .5 and rounds up, where the double product is just below .5.

**Rule:** `result = round_half_away_from_zero( float32(multiplier x amount) )`; a result outside the signed 32-bit range comes out as -2147483648. The multiplier itself is a single-precision value (1.0 plus single-precision additions of 0.05 / 0.10).

| multiplier | amount | original | Team B |
|---|---:|---:|---:|
| 1.05 | 10 | **11** | 10 |
| 1.05 | 150 | **158** | 157 |
| 1.05 | 250 | **263** | 262 |
| 1.15 | 10 | **12** | 11 |
| 1.15 | 150 | **173** | 172 |
| 1.3 | 5 | **7** | 6 |
| 1.3 | 75 | **98** | 97 |
| 1.25 | 10 | 13 | 13 (matches: 12.5 is exact) |
| 1.0 | any | amount | amount (matches) |

Per-kill factor (gang kill, table value 10): factor 1.05 gives **11** (Team B 10); 1.15 gives **12** (11); 0.95 gives **10** (9). The rank mapping (3+ kingpin, 2 gangsta, 1 killa, other thug), the gang-member test and the excluded-victim test all matched.

The activity completion bonus (second award = Props_Award x (multiplier - 1)) uses the same rounding; with realistic values (Props_Award 0..99,999, every reachable multiplier) it happened to match on every case. It differs only for a negative Props_Award (the original treats it as an unsigned 32-bit number, so -200 gives -2147483648) and for products past the 32-bit range.

## D-2. Diversion trick reward: cash is floored, and the "nothing" threshold is 0.001 — MISMATCH (HIGH)

Inputs: trick fraction, Max_Respect, Max_Lifetime_Respect, Max_Cash (the routine receives Max_Cash as a float), and whether the lifetime statistic has reached Max_Lifetime_Respect.

**Rule (2,040 / 2,040 cases):**
1. `f = clamp(fraction, 0, 1)`.
2. If `f` is **not greater than 0.001** (precisely: not greater than the single-precision value 0.0010000000474974513), nothing is paid. The rebuilt code uses 1e-6.
3. Lifetime limit not reached: `respect = truncate(f x Max_Respect)` (matches the rebuilt code). Paid only if it is at least 1.
4. Lifetime limit reached: `cash = floor( float32(f x Max_Cash) )` — **floor, not round**. If that is 0, nothing is paid.

| fraction | Max_Cash (lifetime reached) | original cash $ | Team B |
|---|---:|---:|---:|
| 0.57 | 80 (drifting) | **45** | 46 |
| 0.3333 | 80 | **26** | 27 |
| 0.999 | 80 | **79** | 80 |
| 0.01 | 80 | **0** (nothing) | 1 |
| 0.002 | 250 (back-seat driver) | **0** | 1 |
| 1.0 | 80 | 80 | 80 (matches) |

With the shipped Max_Respect values (at most 150) the 0.001 threshold never changes a respect payout; it does with larger values (fraction 0.0005, Max_Respect 10000: original 0, Team B 5).

## D-3. Reward-toast finalisation: respect OR cash, never both — MISMATCH (MEDIUM)

When a reward toast (respect field, cash-dollars field) is finalised:
1. **If the toast's respect field is greater than 0**, only respect is processed: `amount = D-1 rounding of (multiplier x respect)`, then the award routine. **The toast's cash field is ignored entirely**, even when the award grants nothing because the player is at the cap (the award's own overflow-to-cash still happens). The rebuilt code also pays the toast cash in this case.
2. The "nothing granted" notification is posted only when `amount` is greater than 0 and the award returned 0. When `amount` rounds to 0 (e.g. respect 1 x multiplier 0.4) nothing at all happens: no award, no notification, no cash. The rebuilt code sets its zero-grant flag whenever the award returned 0, including toasts with no respect at all.
3. **Otherwise (respect field 0 or negative)**, cash is paid only if the cash field is **greater than 0**: `raw = round_half_away_from_zero( float32(cash x 100) )`, clamped to plus or minus 2,000,000,000. A negative cash field pays nothing (the rebuilt code pays a negative amount).

| toast respect | toast cash $ | multiplier | original | Team B |
|---:|---|---|---|---|
| 250 | 0.125 | 1.0 | respect 250, **no cash** | respect 250, cash 13 raw |
| 30 | 1000 | 1.25 | player at the cap: overflow cash 3,800 raw only, **no toast cash** | 3,800 + 100,000 raw |
| 0 | 0.005 | 1.0 | **1** raw | 0 raw |
| 0 | 0.015 | 1.0 | **2** raw | 1 raw |
| 0 | -1.0 | 1.0 | **nothing** | -100 raw |
| -3 | 5.0 | 1.0 | 500 raw (cash path) | — |
| 1 | 5.0 | 0.4 | **nothing at all** | — |
| 0 | 0 | 1.0 | no notification | zero-grant flag set |

## D-4. Respect award: 32-bit wrap-around — MISMATCH (LOW)

All 1,968 ordinary cases matched exactly, including: shipped and synthetic tables (a zero-cost row, a non-zero last row, a single row, rows of cost 1), the cap clamp, the overflow-to-dollars rule, horde mode, a remote (not locally owned) player, and 200 random save-edited states (negative or out-of-range level, in-level, total). The level-up loop, the levels crossed, the "+N" notification amount and displayed level all matched.

The original keeps every sum in signed 32 bits; the rebuilt code uses 64-bit intermediates. They differ only when a sum passes 2,147,483,647:
- `total + amount` wraps negative, so the cap clamp is skipped: the full amount is granted and returned, and the total (floored at 0) becomes 0. Example: shipped table, total 238,800, L 48, amount 2,147,483,647: original returns 2,147,483,647, total 0, in-level 0, L 48, no level crossed; Team B grants 75 and reaches L 49.
- `in-level + grant` wraps negative, so no level is crossed: in-level 2,147,483,000, amount 1,000: original L stays 0 (in-level 0); Team B crosses to L 49.

These need a single grant above about 2.1 billion respect, or an edited save, so they are listed for completeness.

## D-5. Rotate-left-6 / XOR name hash: bytes are signed — MISMATCH (LOW)

Closes the open question in spec-vpp-container 2.2 (handling of byte values 128 and above). Each byte is read as a **signed** 8-bit value and widened to 32 bits (byte value 128 becomes -128, i.e. the high 24 bits set) before the A-Z lower-casing test and the XOR. ASCII-only names (all shipped file and bone names checked so far) are unaffected: 329 / 329 ASCII cases matched, 296 / 296 cases containing a byte of 128 or more differ.

| name bytes (hex) | original | Team B |
|---|---:|---:|
| 80 | 4294967168 | 128 |
| ff | 4294967295 | 255 |
| 636166e9 ("caf" + e9) | 4269143657 | 25823593 |
| c0e0 | 4063 | 12512 |
| 74696e745f636f6c6f72 ("tint_color") | matches | matches |

The engine has a second copy of this hash (the one spec-texture-format 10.6 questioned). It gives the same value as the first on all 625 inputs; the only difference is an absent (null) name: 4294967295 from the first copy, 65535 from the second.

## D-6. CRC-32 name hashes — MATCH

Lower-cased and case-sensitive variants, seeds 0, 4294967295 and 305419896, 625 strings each including all byte values: 3,750 / 3,750 matched. The lower-casing folds only A-Z (bytes 128-255 are left alone). The routine also takes a length limit: only the first `limit` bytes are hashed (limit 0 gives the seed unchanged). An absent (null) name gives 0 for both variants, whatever the seed.

## D-7. Dollars to raw cash — MATCH

`raw = dollars x 100`, saturating to plus or minus 2,000,000,000 (input above 20,000,000 or below -20,000,000 saturates before multiplying, so there is no wrap). 616 / 616 matched, including every 32-bit extreme.

## D-9. Shotgun pellet factor takes a random draw — MISMATCH (HIGH)

For a shotgun (Weapon_Class 3) fired by a non-player, the per-hit damage is multiplied by a pellet factor. The spec (and the rebuilt code) give `max(1, round((1 + (Shots_Per_Round - 1) t^2) t^2))` with `t = clamp(1 - dist / Range_Max, 0, 1)`. The original **draws one value from the game's table random-number source** and multiplies by a random number between t^2 and 1 instead of by t^2:

**Rule (3,000 / 3,000 random cases):** `A = 1 + (Shots_Per_Round - 1) t^2`; `r = uniform in [t^2, 1]` (single precision: t^2 + (1 - t^2) x u, u in [0, 1)); `factor = max(1, round_half_away_from_zero(A x r))`. With u = 0 the original gives exactly the rebuilt formula (384 / 384 matched), so the rebuilt code always applies the **smallest possible** factor.

| dist / Range_Max | Shots_Per_Round 6 | u = 0 (rebuilt) | u = 0.25 | u = 0.5 | u = 0.75 | u near 1 |
|---|---|---:|---:|---:|---:|---:|
| 0 (point blank) | | 6 | 6 | 6 | 6 | 6 |
| 0.25 | | 2 | 3 | 3 | 3 | 4 |
| 0.5 | | 1 | 1 | 1 | 2 | 2 |

Full table (two ranges, 6 and 8 shots, five distances, five draws) in the vectors file, section C1. Player shooters do not use this factor at all (matches).

**About the random source** (needed to port this faithfully): it is a ring of 8,192 precomputed 32-bit values with a cursor; each draw takes the next value v and returns `lo + (hi - lo) x v / 2^32` in single precision. The ring is filled from a standard 32-bit Mersenne Twister (MT19937). Before the game code runs, the ring is filled with the first 8,192 outputs of MT19937 seeded with the reference default seed 5489 (confirmed: first values 3499211612, 581869302). The game later re-seeds and refills it from a microsecond clock, so live values are not reproducible; any uniform source is acceptable for a port, but the draw itself must exist. (A network code path also appears to re-seed it from a synchronised 16-bit object id, presumably so co-op peers share the sequence; HIGH, not traced further.)

## D-10. Base weapon damage — MISMATCH (LOW)

**Rule (5,712 / 5,712):** if `Damage_Min >= Damage_Max` the result is `Damage_Max`. Otherwise, **in this order**: if `dist^2 >= Damage_Min_Dist^2` (the square rounded to single) the result is `Damage_Min`; else if `Damage_Max_Dist^2 >= dist^2` it is `Damage_Max`; else linear interpolation on `sqrt(dist^2)`, computed in double and rounded to single once at the end (see vectors C2 for the exact steps). Then x the shooter's firearm multiplier and x the riot-shield multiplier when that applies.

Two differences: (1) the rebuilt code rounds each interpolation step to single, which differs in the last 1-5 bits in 260 cases (for example Damage_Max 160, Damage_Min 40, distances 10 / 50, dist^2 584.99756: original 117.439827, rebuilt 117.439835); (2) when `Damage_Max_Dist > Damage_Min_Dist` (an inverted range) the original tests the far limit first and returns `Damage_Min`, the rebuilt code returns `Damage_Max` (104 cases; for example 100 / 25 with distances 50 / 10 at distance 25: original 25, rebuilt 100). Whether any shipped weapon has an inverted range was not checked.

## D-11. Damage assist (low-health mercy) — MISMATCH (LOW, precision only)

The rebuilt formula is right; only the rounding differs (1 to 10 bits of the last place in 540 of 1,853 cases, for example 160 damage at 100 / 100 HP: original 97.421532, rebuilt 97.421539). Bit-exact evaluation order in vectors C3. Note: maxHP is an integer in the original, and a maxHP of 0 makes the original's loop never end (the rebuilt code returns the damage unchanged).

## D-12. Health-regeneration wait — MISMATCH (LOW)

**Rule (660 / 660):** `wait_ms = truncate( Health_Regen_Wait_Mult x (6000 - reduction) )`, the multiplier being the single-precision difficulty value and the product taken in double, **with no rounding guard**. The rebuilt code adds 0.001 ms before truncating, so it gives 7,800 for hard difficulty (1.3) where the original gives **7,799**, and 5,700 where the original gives **5,699** (multiplier 0.95). Multipliers whose single-precision value is exact or slightly above the decimal (0.3, 0.5, 0.6, 1.0, 1.1, 1.75, 2.0) match.

## D-13. Health-regeneration ticks — MISMATCH (LOW)

**Rule (1,512 / 1,512):** health is a **single-precision** value. `rate = 125` (500 while holding a human shield) x difficulty Health_Regen_Speed_Mult x the Health unlock factor, each product rounded to single; per-tick `delta = single(rate x single(1/30))`; each 30 Hz tick `health = clamp(single(health + delta), 0, maxHP)` while `health < maxHP`. The rebuilt code accumulates in double, so the tick on which health becomes full differs by one in 116 cases (0 to 100 HP at 125 per second: original **25** ticks, rebuilt 24; with unlock factor 2: original **12**, rebuilt 13). The value after the first tick always matched.

## D-14. Table float grammar — MISMATCH (MEDIUM)

**Rule (485 hand-picked texts and 6,000 random stress texts, all bit-exact except the sign of a NaN):**
1. If the text starts with `-`, remember "negative" and skip it.
2. If the next character is `.`, the integer part is 0 and the fraction starts here. Else, if the next two characters are `0x` or `0X`, the result is **+0.0** (also for `-0x...`). Else: if the next character is another `-`, skip it and negate the integer part (so `--5` reads as **5**); read the integer part with the integer grammar (decimal digits, wrapping modulo 2^32) and treat the 32 bits as a **signed** integer.
3. Find the **first `.` anywhere in the text**. If there is none, the result is the integer part (negated if "negative") converted to single precision, and **any exponent is ignored** (`1e5` reads as **1**, `-1e5` as **-1**). If there is one, the fraction is read from the character after it, even if other characters came between the digits and the dot (`12abc.5` reads as **12.5**).
4. Fraction: `w = 0.1f; f = 0`; for each digit d: `f = single(d x w + f)` (the multiply-add in double, rounded once to single), then `w = single(w x 0.1f)`.
5. If "negative", negate f and the integer part. `value = single(single(integer part) + f)` (the integer part is first rounded to single, the sum in double).
6. If the character right after the fraction digits is `e` or `E`, the exponent is read with the **C runtime's `atoi`**: leading white space is skipped and **a `+` or `-` sign is accepted** (`1.5e+2` reads as **150**, `1.5e 3` as **1500**). `10^n` is computed by repeated squaring in single precision (for n < 0, `1 / 10^|n|` in double, rounded to single); `value = single(10^n x value)`.

Differences from the rebuilt reader, by effect:
- **Fraction rounding** (step 4): the rebuilt reader multiplies and adds in single precision, two roundings per digit. Of the 16,452 shipped occurrences in this category, 16,356 differ by one bit, 92 by two and 4 by more; the original's result is the closer one to the decimal text in 15,245 of them. Examples: `-8.125000` original -8.125 exactly, rebuilt -8.125001; `10.909000` original 10.909000396728516, rebuilt 10.9089994430542; `3.14159265358979` original 3.1415927, rebuilt 3.1415918.
- **Signed integer part**: a 10-digit integer part above 2,147,483,647 wraps negative in the original. Shipped example (voice-control tables, 555 occurrences): `4193333613` reads as **-101633680.0** through the float reader (the rebuilt reader gives 4193333504.0). 4,308 occurrences of long integer parts in the shipped tables differ (this matters only where such a field is read as a float; the integer readers match, D-15).
- **Exponent `+` sign**: `1.000000e+003` reads 1000 in the original, 1 in the rebuilt reader. No shipped value uses `e+` in the files checked; 9 occurrences of negative exponents differ by 1-2 bits.
- **No `.`**: `1e5` reads 1 in the original, 100000 in the rebuilt reader. `-0.0` reads +0.0 in the original (the rebuilt reader gives -0.0). `+5.5` reads 0.5 in the original (a leading `+` is not a digit, so the integer part is 0), 0 in the rebuilt reader. `--5` reads 5 in the original, -0.0 in the rebuilt reader.

Census, vectors and the top differing shipped values: vectors file, sections T1-T3.

## D-15. Table element readers and integer grammar — MATCH

The signed 32-bit, unsigned 32-bit, bool and unsigned 8-bit readers (one leading `-` handled only by the signed reader; bool true only for whole-text `true` / `yes` in any case) matched on all 96 texts, and the integer grammar on 230 texts (decimal, wrapping modulo 2^32, stopping at the first non-digit). One edge, informational only: for hex text (`0x...`) the original's integer grammar treats the text's terminating zero byte as a hex digit and keeps reading past the end of the text (value of `0x1F` followed by a zero byte and then `Z`: 441, not 31), so the result depends on whatever follows the text in memory. No shipped value is written in hex.

## D-16. Table XML document parser — MISMATCH (MEDIUM)

Settles the open tolerance questions in spec-xtbl-format 7 and the "text is never trimmed" assumption. The original's in-memory table parser was run on 100 hand-made documents and on all 542 extracted shipped table files; full vectors in `spec-differential-vectors-xml.md`.

**Rules of the original (each confirmed on the vectors):**
1. **Text is trimmed.** Leading and trailing bytes with values 1 to 32 (space, tab, CR, LF and the other control characters, including 0x1F) are removed from every text run; bytes inside the text are kept (`a  b`, `a\r\nb` unchanged; 0x7F and 0xA0 are not trimmed). A run that becomes empty gives no text.
2. **One text run per element.** An element's text is its first non-empty run; runs are separated by child elements, comments and processing instructions, and every later run is dropped (`<B>x<C/>y</B>` gives `x`; `<B> <C/>y</B>` gives `y`; `1<!-- c -->2` gives `1`).
3. **Entities:** exactly `&lt;` `&gt;` `&amp;` `&quot;` `&apos;` (lower case) are decoded. Anything else after `&` (numeric references, upper case, unknown names, a bare `&`, a missing `;`) makes the **whole file fail to parse**.
4. **UTF-8:** a two-byte sequence (lead byte C0-DF hex, then 80-BF) is converted to the single byte of its code point when that is 255 or less (`c3 a9` becomes `e9`); a two-byte sequence for a code point above 255 makes the whole file fail; three- and four-byte sequences and lone high bytes are kept unchanged.
5. **The whole file fails** (the loader gets nothing) for: a close tag that does not match its open tag (case differences are accepted), a close tag with nothing open, elements still open at the end, non-blank text before or after the root element, `<` inside text, CDATA, `<!DOCTYPE`, a UTF-8 byte-order mark, a space or tab inside a close tag's name, and an open tag with a space before `>` that has a separate close tag (`<A >v</A>`).
6. **Accepted:** an XML declaration, processing instructions and comments anywhere; `<A/>` and `<A />`; a close tag with a trailing space (`</A >` closes A, and its parent gets the text `>`).
7. **Several top-level elements:** the **last** one is the document returned.
8. **Attributes:** in a self-closing tag the attribute text becomes part of the element name (`<A b="1"/>` is an element named `A b="1"`); in a tag with a separate close tag the element is named `A` and its text starts with `>` (`<A b="1">2</A>` gives text `>2`).

**Effect on the shipped tables** (542 distinct files): 95 parse to identical trees in both. The rebuilt parser keeps text the original drops or trims in the rest: **1,268 values with leading or trailing white space** in 87 files (for example cutscene `LightsEnabled` lists `3 9 14 19 20 23 24 25 26 27 28 ` with a trailing space, vehicle components `grime 75 `, `default_global` `cloud_mesh_filename` = `skybox_clouds` followed by a line break, and `0` followed by a line break in weapon and explosion `Effect` / `Weapon` elements); **1,305 whitespace-only texts** in 361 files that the original treats as no text (most often `TableTemplates` and flag-list parents); and 126 `TableDescription` elements whose original text is `>` (rule 8; no reader visits them). Text after a child element (rule 2) did not occur in these files. From Team B's own population figures, `template.xtbl` (mismatched close tag) and `xbox360_text.xtbl` (spaces in tag names) would **fail to load** in the original (rule 5); `07_out.cte_xtbl`'s 0x1F inside a name is kept.

**Port rule:** trim bytes 1-32 from both ends of each text run, keep only the first non-empty run, and treat files the original rejects as not loaded (or at least never let a value differ because of white space).

## D-17. Vehicle engine torque curve — MISMATCH (HIGH)

The rebuilt engine torque (the live driving model's curve, fed by the "crossed field" reading of spec-physics-runtime 10.2) differs from the original on **all 1,620** recorded cases (36 distinct shipped engines, 15 engine speeds, 3 throttle positions); the rebuilt curve gives 0 or near 0 torque over most of the range for shipped data (for example the DLC compact car at its Opt_RPM, full throttle: original 350, rebuilt 0). The table field names are **not crossed**: the fields named `*_Torque_Factor` shape the torque term and the fields named `*_Resistance` shape the resistance term, as their names say. Shipped values agree with that reading (resistances 0 to 0.2, torque factors around 0.9).

**Rule (positive throttle; 5,400 / 5,400 random cases bit-exact, all products rounded to single precision as written):**
- Inputs: the 10 engine fields (Torque, Min_RPM, Opt_RPM, Max_RPM, Min_RPM_Torque_Factor, Max_RPM_Torque_Factor, Min_RPM_Resistance, Opt_RPM_Resistance, Max_RPM_Resistance, Clutch_Slip_RPM), the current engine RPM, the throttle (0..1) and the transmission RPM.
- `target = Min_RPM + (Clutch_Slip_RPM - Min_RPM) x throttle`; the curve is evaluated at `r = max(engine RPM, target)`.
- `d = r - Opt_RPM`.
- If `d < 0` (below Opt_RPM): `k = 1 / (Min_RPM - Opt_RPM)`; `T = Torque x (1 + (Min_RPM_Torque_Factor - 1) x k^2 x d^2)`; `R = Torque x (Opt_RPM_Resistance + (Min_RPM_Resistance - Opt_RPM_Resistance) x k x d)`; and if the transmission RPM is below Min_RPM, `R = R x transmission RPM / Min_RPM`.
- Else if `r < Max_RPM`: the same with Max_RPM, Max_RPM_Torque_Factor and Max_RPM_Resistance.
- Else (at or above Max_RPM): `T = 0`, `R = Torque x Max_RPM_Resistance`, and the stored engine RPM is set to Max_RPM.
- `torque = T x throttle - R`.
- Then x a per-vehicle multiplier (one of two, picked by a vehicle state flag; 1.0 in these tests), x 0.85 when the attached-object condition of spec 10.2 holds, x the global tweak scale (spec 10.11.10: 1.25; 1.0 in these tests).

So T is a quadratic bump peaking at exactly `Torque` at Opt_RPM and falling to `Torque x *_Torque_Factor` at Min_RPM / Max_RPM; R is a straight line between the three resistance points.

**Zero throttle (coasting), confirmed separately:** `torque = -(vehicle mass x 0.35 x engine RPM / Max_RPM)`, x (transmission RPM / Min_RPM) when the transmission RPM is below Min_RPM; it is 0 while a vehicle timer (unidentified) is running. Examples (mass 1,500, Max_RPM 6,500): RPM 3,000: -242.308; RPM 6,500: -525.0.

| engine (Torque / Min / Opt / Max / MinTF / MaxTF / MinRes / OptRes / MaxRes / Slip) | rpm | throttle | original | Team B |
|---|---:|---:|---:|---:|
| 350 / 1000 / 5700 / 6500 / 0.9 / 0.9 / 0 / 0 / 0.2 / 1750 | 0 | 1.0 | 325.279 | 0.000 |

More rows (12 shipped engines, every 500 RPM) in `spec-differential-vectors-engine.md`, section E1.

## D-18. Engine RPM update — rule only (for the port)

The original's engine-RPM routine (spec 10.2 "ramps RPM up-only") works like this (confirmed by emulation, vectors E2):
- `target = Min_RPM + (Clutch_Slip_RPM - Min_RPM) x throttle`.
- If the transmission RPM is at or above `target`, the new engine RPM **is the transmission RPM** (no ramp at all).
- Otherwise `diff = target - current engine RPM`; `rate = clamp(diff / dt, -10000, 10000)`; `ease = |diff| / 750` if `|diff| < 750`, else 1; `new = current + ease x rate x dt`. The ramp works in both directions (it is not up-only); the engine falls toward the target when the transmission RPM is low.

Example (Min 1,000, Slip 1,750, dt 1/30): current 800, throttle 0.5, transmission 1,200: target 1,375, new 1,055.556; transmission 2,000: new 2,000.

## D-19. Automatic gear shifting — MISMATCH (MEDIUM)

12 RPM sequences at 30 steps per second, 5 gears; all 12 differ. The rebuilt state machine refuses decisions while a shift is in progress and adds two 0.5 s cooldowns (one per direction, both CHOSEN); the original has neither.

**Rule of the original, per step** (inputs: step time dt, current gear index starting at 0, the shift timer, the RPM, the Upshift RPM, the per-gear downshift RPM table, the number of gears, the shift time):
1. `timer -= dt`; if a shift was in progress and `timer <= 0`, the "shifting" flag clears. The timer and flag **do not block** any decision.
2. In reverse, stop here (no shifting).
3. If a vehicle-level shift window is closed, stop here (it is closed for 500 ms after a vehicle timer is armed; what arms that timer was not traced).
4. **Downshift:** if the gear index is above 0 and `downshift_table[gear_index - 1] > RPM` (strictly greater), the gear index drops by one, `timer = shift time`, flag set.
5. **Upshift** (checked in the same step, after step 4): unless an upshift-inhibit flag or timer on the vehicle is active, if `RPM > Upshift RPM` (strictly) and `gear_index + 1 < number of gears`, the gear index rises by one, `timer = shift time`, flag set.

Differences that follow: at exactly the threshold the original does not shift (strict comparisons; the rebuilt code shifts at `>=` / `<=`); with the RPM held above the upshift threshold the original climbs one gear per step (in the game the RPM drops after each upshift, so this does not cascade), the rebuilt code one gear per 0.5 s; downshifts happen immediately after an upshift if the RPM is below the table value.

| RPM held for every step | Upshift RPM | original gears (first 6 steps) | Team B |
|---|---:|---|---|
| 7,000 | 5,500 | 2, 3, 4, 5, 5, 5 | 2, 2, 2, 2, 2, 2 |
| 5,500 | 5,500 | 1, 1, 1, 1, 1, 1 | 2, 2, 2, 2, 2, 2 |

## D-20. Steering angle limiter — MISMATCH (MEDIUM)

15 target sequences (3 steering setups x 5 patterns, 30 steps per second): 8 match to 5 decimals, 7 differ. The rebuilt minimum-time limiter is right on steady turn-in, hold and release; it misses the seed that spec 10.11.11 item 9 leaves open. The rule below reproduces all 15 original sequences (largest difference 0.000005) and 3,999 of 4,000 random single calls of the original limiter to within 0.0001.

**Per-frame inputs** (spec 10.11.7 fields): turn-in speed `V`, turn-in damp angle `d`, return speed `Vr`, return damp angle `dr`; current angle `p` and angular velocity `v` (kept between frames); target angle `T`; frame time `dt`.
- If `T == 0` (centring): use `(Vr, dr)` and seed 0.
- Else use `(V, d)`; the seed is `Vr^2 / (2 x dr)` when `(T - p) x v < 0` (the wheel is still turning away from the new target), else 0.

**The limiter** (`A = V^2 / (2 x d)` for the chosen pair, tolerance 0.000001): up to 8 segments while time remains:
1. `e = T - p`; if `|e| <= tol`: `p = T`, `v = 0`, done. `s = sign(e)` (+1 when `e >= 0`).
2. **Moving away** (`v x e < -tol`): accelerate toward the target with `a = s x seed` if the seed is above 0, else `a = s x A`, for the time it takes to stop (`-v / a`). **This is the difference:** on a reversal the turn is braked with the return pair's acceleration (for example 15 rad/s^2 instead of 5.7 for the setup V 2, d 0.35, Vr 3, dr 0.3).
3. **Inside the braking zone** (`|e| <= d + tol`): curve speed `vt = s x sqrt(v^2 / 2 + A x |e|)`. On the curve (`|vt - v| <= tol`): `v = vt`, `a = -s x A` until the target (time `2e / vt`). Slower than the curve: `a = s x A` until reaching it. Faster: `a = -v^2 / (2e)` (stop exactly at the target).
4. **Outside the zone:** cruise speed `vt = s x V`, distance to the zone `z = e - s x d`. At cruise speed: `v = vt`, `a = 0` until the zone. Otherwise `a = s x A` (or `-s x A` when `|v| > V`) until reaching cruise speed or, if the zone comes first, until reaching the zone.
5. Each segment: `p += v x t + a x t^2 / 2`, `v += a x t`.

Example (V 2, d 0.35, Vr 3, dr 0.3; target +0.6 for 10 steps, then -0.6): original peak +0.424 at step 13, zero crossing at step 25, -0.6 at step 39; rebuilt peak +0.599 at step 18, zero crossing at step 33. Alternating +0.6 / 0 targets: original 0.0125, 0.0079, 0.0029, 0; rebuilt 0.0125, 0.0079, -0.0102, 0. All 15 sequences: `spec-differential-vectors-engine.md` section E3. The same limiter drives the helicopter collective (spec 10.13.2), so the seed rule applies wherever a caller passes one.

## D-21. Notoriety gain and decay — MISMATCH (MEDIUM)

98 scripted sequences (police and gang track, crimes and decay steps, the rebuilt tables); 54 match exactly, 44 differ, and every difference is one of four rules (spec-ai-runtime 19.8 G3 / G4 and the decay step; this also settles Team B's CHOSEN readings 1-3):

1. **Negative crime points are applied** (rebuilt: skipped). A report with Points <= 0 only skips re-arming the first-decay timer; the points still go through the award, and a negative amount rolls the level down exactly like decay (same floor rule as 3). Example, gang track at 2 / 200: `enter owned store` (-20) gives 2 / 180, again 2 / 160; a -200 report gives 1 / 185.
2. **A zero-point report still runs the award.** Below its Min_Level a witnessed zero-point report lifts the track to Min_Level (gang 0 / 0, Min_Level 2: result 2 / 125 after the entry floor); an unwitnessed one with Check_Detection gives the minimum award of 1 point. (Rebuilt: nothing.)
3. **Decay landing on the floor keeps the remainder.** Roll-down: while the fraction is negative, add `limit(level)` and drop one level; only if that takes the level **below** the floor is it set to the floor with fraction 0. Landing exactly on the floor keeps the positive remainder, and decay continues at the floor level's rate until the fraction reaches 0 (only then is the decay finished). Example, police: one 100-point crime gives 1 / 75; 12 decay steps of 6 later it is 1 / 3; the next step gives **0 / 72** (rebuilt 0 / 0), then 0 / 71, 0 / 70 ... one point per step. While that remainder lasts, a small crime brings the first star back almost at once. (CHOSEN reading 3 is therefore wrong; reading 1, "the entry floor applies only when the level changed", is right.)
4. **The level-entry floor uses the fractional part.** After a level change below 5: `fraction = max(fraction, 0.25 x (limit(level + 1) + (fraction - trunc(fraction))))`. With a fractional remainder of 0.25 the floor is 125.0625 instead of 125 (7 sequences).

Everything else matched: the x3 bonus, Min_Level / Max_Level windows, the unwitnessed quarter award `max(1, P / 4)`, `pointsTo`, the roll-up, reaching and leaving level 5, and the decay amounts. All 98 sequences, step by step, are in `spec-differential-vectors-ai.md`.

## D-22. Cash wallet add: the cash-bonus multiplier and a wrap to zero — MISMATCH (HIGH)

Every cash grant to a player goes through one wallet routine (spec-tables-progression B.1.1 "Team B R3" note: add, clamp to plus or minus 2,000,000,000, floor at 0). 684 cases (cash before, amount, cash multiplier); 363 differ. Two rules are missing from the rebuilt add:

1. **The cash-gain multiplier.** For the **local player** and a **positive** amount, the amount is first scaled by the global cash multiplier that the unlockable type 38 `Cash_Bonus_Modifier` raises (spec-tables-progression 4.4 row 38; 1.0 at a new game). Shipped rows: `Cash_Bonus_1`, `_2`, `_3` add 0.05 each, `Cash_Bonus_4` 0.10 and `Cash_Bonus_Modifier` 0.1, so a player who buys them all earns **35% more cash from every source**. The rebuilt code parses the modifier but never applies it. Rule: `amount = clamp(round_half_away(single(single(single(amount) / 100) x multiplier x 100)), +-2,000,000,000)` (the inner product in double). Negative amounts (purchases, penalties) and other players' grants are not scaled. Example: multiplier 1.05, +$5.00 (500) gives **525**; multiplier 1.35 (all five bought), +$10.00 gives **1,350**.
2. **The sum is 32-bit and wraps.** `new = cash + amount` is computed in signed 32 bits **before** the plus or minus 2,000,000,000 clamp, so a sum above 2,147,483,647 wraps negative, is clamped to -2,000,000,000 and is then floored to **0**: the player loses all their money. Examples: cash 2,000,000,000 ($20,000,000) + 147,483,647 or more -> **0** (rebuilt 2,000,000,000); cash 1,900,000,000 + 300,000,000 -> **0**; cash 1,900,000,000 + 147,483,648 -> 2,000,000,000 (no wrap, sum 2,047,483,648). Below the wrap the clamp to 2,000,000,000 matches. Reaching it needs a single grant of about $1.47 million or more while near the $20 million cap.

Also: in the multiplier path the amount is converted to single precision first, so very large grants lose low digits even at multiplier 1.0 (1,500,000,000 + 147,483,647 gives 1,647,483,648). Rule check: a plain-language model of the routine reproduced 4,000 / 4,000 random emulated cases. All 684 cases: `spec-differential-vectors-cash.md`.

## D-23. Sun light direction over the day — MISMATCH (MEDIUM, visual)

The rebuilt time-of-day lighting uses a stand-in sun direction (fixed azimuth, peak at noon, below the horizon at night). The original's rule (spec-tables-environment 19.2, now confirmed by emulation of the original on every vector of that section's table, plus the orbital-sprite direction 19.3, the 48-slot curve resample with its 47 wrap span, the per-frame curve read and the HDR colour scaling of 19.1):

```
H   = game clock hour (float);  E = minimum elevation in degrees (40 for the sun light, 15 for the sky shader, 1 and 90 for two other users)
t   = H - 7.5;  if t < 0: t += 24;  t = t / 12;  t = 1 - t          (the shipped "reverse" switch is on)
a   = (t - 1) * pi
y   = min(max(|sin a|, sin(E deg)), 1)
v   = normalize(-cos a, y, 0.53 * sin a)        (then rotated about Y by a yaw that is 0 in shipped play)
out = -v                                          (out = the direction the light travels; toward the sun = -out)
```

So the sun never sets for lighting purposes (at night the light still comes from at least E degrees above the horizon, mirrored across the sky), it peaks at 13:30, it is at the horizon line at 7:30 and 19:30, and its path is tilted toward +Z (the 0.53 term). Values for the sun light (E = 40):

| hour | original light direction (out) | original toward-sun (-out) | Team B sun direction |
|---:|---|---|---|
| 0 | (-0.34369, -0.82975, -0.43977) | (0.34369, 0.82975, 0.43977) | (0.22809, -0.76029, -0.60823) |
| 3 | (0.34369, -0.82975, -0.43977) | (-0.34369, 0.82975, 0.43977) | (0.27050, -0.63758, -0.72134) |
| 6 | (0.80786, -0.56206, -0.17735) | (-0.80786, 0.56206, 0.17735) | (0.35112, 0.00000, -0.93633) |
| 7.5 | (0.84121, -0.54071, -0.00000) | (-0.84121, 0.54071, 0.00000) | (0.32045, 0.40877, -0.85453) |
| 9 | (0.80786, -0.56206, 0.17735) | (-0.80786, 0.56206, -0.17735) | (0.27050, 0.63758, -0.72134) |
| 12 | (0.34369, -0.82975, 0.43977) | (-0.34369, 0.82975, -0.43977) | (0.22809, 0.76029, -0.60823) |
| 13.5 | (-0.00000, -0.88357, 0.46829) | (0.00000, 0.88357, -0.46829) | (0.23840, 0.73417, -0.63573) |
| 15 | (-0.34369, -0.82975, 0.43977) | (0.34369, 0.82975, -0.43977) | (0.27050, 0.63758, -0.72134) |
| 18 | (-0.80786, -0.56206, 0.17735) | (0.80786, 0.56206, -0.17735) | (0.35112, 0.00000, -0.93633) |
| 19.5 | (-0.84121, -0.54071, -0.00000) | (0.84121, 0.54071, 0.00000) | (0.32045, -0.40877, -0.85453) |
| 21 | (-0.80786, -0.56206, -0.17735) | (0.80786, 0.56206, 0.17735) | (0.27050, -0.63758, -0.72134) |

## D-24. Lighting: how the weather time-of-day cells merge with the base curves

Every lighting value (each colour is 4 numbers: R, G, B, intensity; scalars likewise) is built in two steps each frame.

1. **Base.** Sample the district time-of-day curve sets at the current hour and sum them with their weights. In shipped play this is the default district set alone (weight 1, see D-23 notes). A debug override record, when switched on, replaces the base for any field it marks present.
2. **Weather merge.** Take the two weather time-of-day cells of the current time segment: the cell of the weather stage being left ("from") and the cell of the stage being entered ("to"), plus the stage transition fraction t (0..1). For each field:
   - F = the from-cell value if that cell marks the field present, else the base;
   - T = the to-cell value if that cell marks the field present, else the base;
   - result = F*(1-t) + T*t.
   - If neither cell marks the field present, the result is exactly the base (no arithmetic).
   - While a script forces the weather, the merge is switched off and the result is the base.
   - The cells of the neighbouring time segment are NOT used for lighting. Absent cells (no weather table loaded) count as "not present".

The cells are a sparse override layer on top of the curves, not a replacement for them. Precision: each product is computed in double and rounded to float32, (1-t) is rounded to float32 first, and the sum is rounded to float32.

| base | from cell | to cell | t | result (all 4 channels) |
|---|---|---|---:|---|
| (9,9,9,9) | (1,2,3,4) | (5,6,7,8) | 0.25 | (2, 3, 4, 5) |
| (9,9,9,9) | (1,2,3,4) | absent | 0.25 | (3, 3.75, 4.5, 5.25) |
| (9,9,9,9) | absent | (5,6,7,8) | 0.25 | (8, 8.25, 8.5, 8.75) |
| (9,9,9,9) | absent | absent | 0.25 | (9, 9, 9, 9) |
| (9,9,9,9) | any | any | any, weather forced by script | (9, 9, 9, 9) |

**Team B difference (structural, snapshot reviewed).** The rebuilt lighting blends four weather cells (two time segments times two stages) and uses no base curve and no presence bits. The original differs in three ways:
- it uses the time-of-day curve sets as the base;
- it reads only the current time segment's from/to cells, with no blend between segments;
- a field a cell does not author falls back to the base, not to zero or a default.

Example: base (9,9,9,9), from cell without that field, to cell (5,6,7,8), t = 0.25. The original gives (8, 8.25, 8.5, 8.75). The rebuilt code has no base input, so it cannot produce this value.

## D-25. Vehicle suspension force per wheel — MISMATCH (HIGH)

Rule (verified by emulation on 12,000 random wheel cases, bit-exact):

- Inputs per wheel:
  - rest length L;
  - spring strength S;
  - two damping values, stored in the order (compression, relaxation);
  - the current suspension length C;
  - the closing speed v;
  - the spring scale k (the "load factor": it scales only the spring term);
  - whether the wheel touches the ground.
- Also needed: the chassis mass M (= 1 / inverse mass; 0 if the inverse mass is 0).
- A wheel not touching the ground gives force 0.
- `spring = (L - C) x S x k` (the difference L - C is rounded to float32 first, then the product).
- The damping is the **compression** value when **v < 0**, otherwise (v >= 0, including 0 and -0) the **relaxation** value.
- `force = (spring - damping x v) x M`, rounded to float32 after the subtraction and again after the multiplication.

So the unknown divisor "X" in the spec is the chassis inverse mass: the force is multiplied by the chassis mass. The spring strength and the damping are therefore per unit of mass; shipped strengths are tens, not tens of thousands.

Team B differences (300 cases, 271 differ):
1. There is no multiplication by the chassis mass, so the magnitude is off by a factor of M.
2. The damper choice is reversed: the rebuilt function uses the compression damping when the speed is positive, the original when it is negative. With M = 1 (no mass effect) 71 of 100 cases still differ for this reason alone.

| M | L | S | damping (comp / relax) | k | C | v | original | Team B |
|---:|---:|---:|---|---:|---:|---:|---:|---:|
| 1 | 0.416666 | 45 | 3.88874 / 1.92427 | 0.594123 | 0.428187 | 1 | -2.2323 | -4.1968 |
| 1 | 0.224375 | 23.187 | 1.93013 / 4 | 1 | 0.579344 | -2.30095 | -3.7895 | 0.9731 |
| 1 | 0.4 | 30 | 3.5 / 0.948351 | 0.566586 | 0.0464535 | -1.70752 | 11.9858 | 7.6288 |
| 1500 | 0.633398 | 46.0377 | 2 / 2.5 | 1 | 0.155242 | -1 | 36019.7734 | 24.5132 |
| 1500 | 0.4 | 45 | 2 / 7.96128 | 1 | 0.185009 | -1 | 17511.9121 | 17.6359 |
| 800 | 0.3 | 30 | 2 / 4 | 1 | 0.782144 | 0 | -11571.4473 | -14.4643 |
| 800 | 0.3 | 69.9246 | 2.05048 / 1.64099 | 1.45409 | 0.33839 | -1 | -1482.2975 | -2.2624 |

## D-26. Reload timer length — MISMATCH (LOW, 1 ms)

Rule (the original's arithmetic, emulated; model 9,640 / 9,640 bit-exact):
- `len` = the reload clip length in seconds, a float32: `frames x 160 / 4800` (it is never negative, and a missing clip counts as 0).
- With an override (`Reload_override_time_sec` > 0): `dur` = override (float32); if `len` > 0, `rate = float32(len / dur)`; otherwise `rate` = 1.0.
- Without an override: `dur = len`, `rate` = 1.0.
- For the player, with reload scalar `s`: `rate = float32(s x rate)` and **`dur = float32(dur / s)`**, both computed in double and then rounded to float32.
- `ms = truncate(dur x 1000)`, with `dur` widened to double. Because `dur` is a float32, a time that should be a whole number of milliseconds often lands just below it and loses 1 ms.

Team B computes `floor(dur x 1000 / s + 0.000001)` in double. In 121 of 1,224 cases the result is 1 ms different, in either direction. One shipped combination differs: the shotgun reload (96 frames) with the first faster-reload upgrade (scalar 1.25) gives 2559 ms in the original and 2560 in Team B. Rates agree to float32 precision. All other shipped combinations match: the six known reload clips x scalars 1, 1.25, 1.5, 1.75 x overrides none, 1.5 s, 2.5 s.

| reload clip frames | override (s) | scalar | original ms | Team B ms |
|---:|---:|---:|---:|---:|
| 96 | none | 1.25 | 2559 | 2560 |
| 44 | 0.1 | 1.25 | 79 | 80 |
| 44 | 3.5 | 1.25 | 2799 | 2800 |
| 71 | 0.7 | 1.75 | 400 | 399 |
| 96 | 1.6 | 1.25 | 1279 | 1280 |
| 96 | 4.9 | 1.75 | 2799 | 2800 |
| 79 | 0.7 | 1.25 | 560 | 559 |
| 79 | 3.3 | 1.5 | 2200 | 2199 |
| 44 | none | 1.0 | 1466 | 1466 |
| 44 | none | 1.75 | 838 | 838 |

## D-27. Combat AI numeric details — downed get-up heal (MISMATCH, MEDIUM) and weapon band (minor)

**Get-up heal.** When a downed NPC gets up, its health is set to a **float**, `float32(float32(max HP) x pct)`. It is then clamped to the range 0 to max HP and stored without rounding to a whole number. pct is 1.0 when the team byte is 0, else 0.25 (shipped values). Team B returns `int(max HP x pct)`, which truncates. Every max HP that is not a multiple of 4 differs on the enemy side. Since health is a float everywhere (the health fraction is health / max HP), the lost fraction changes later damage thresholds.

| max HP | side | original HP after get-up | Team B |
|---:|---|---:|---:|
| 1 | enemy | 0.25 | 0 |
| 3 | enemy | 0.75 | 0 |
| 7 | enemy | 1.75 | 1 |
| 99 | enemy | 24.75 | 24 |
| 101 | enemy | 25.25 | 25 |
| 150 | enemy | 37.5 | 37 |
| 333 | enemy | 83.25 | 83 |
| 1000 | enemy | 250 | 250 |
| 150 | team 0 | 150 | 150 |
| 16,777,217 | team 0 | 16,777,216 (float32) | 16,777,217 |

**Weapon distance band (minor).** The "too close" test is `d2 < float32(a x a)`, where `d2` is the float32 squared distance. The out-point fraction is the float32 value 0.1 widened to double (0.10000000149), not exactly 0.1. The out distance is `float32(a + (b - a) x 0.10000000149)`. These differ from Team B's double maths only in the last bit, right on the band edges.

## D-28. Police chase speed law — MATCH (one edge case)

The original's target-relative speed law in the chase driver update was emulated on 3,033 cases. These were random speeds, accelerations and gaps (including negative gaps), with the flank and overlap-stop flags. All agree with Team B's formula to float32 rounding, except one edge case:
- The far gain is built as `float32(aB x 0.6) x 2`, not `1.2 x aB`, and the "very close" threshold is the float32 value 0.1 (0.10000000149). Neither changes any result beyond the last bit.
- **Gap exactly 0** while inside the standoff with the own speed above the command: the original divides by the gap, so the requested deceleration is +infinity (it is not replaced by aB). Example: own speed 30, target speed 15, aB 8, gap 0 → command 10 in both, deceleration +inf in the original and 8 in Team B. Whatever clamps the deceleration downstream makes this a full brake in both, so the effect is small.
- The brake dead band (`decel < 0.5 x aB` → 0) is not in this routine. Team B applies it inside its function; the spec places it in a caller. No conflict, as long as the caller does not apply it a second time.

## Re-check against the latest Team B source (2026-10-20)

All earlier suites were re-run against Team B's current source.

| suite | cases | differences now | earlier |
|---|---:|---:|---:|
| hashes (rotate-6, both CRC forms) | 4,375 | 0 | 0 |
| respect award, dollars, diversion, multiplier, activity bonus | 17,556 | 0 | D-1..D-4, D-8 differed |
| weapon base damage, pellet factor, assist | 7,957 | 0 | D-9..D-11 differed |
| regen wait, regen ticks | 2,172 | 0 | D-12, D-13 differed |
| float grammar, element readers | 581 | 0 | D-14 differed |
| integer reader | 230 | 1 | 1 (text "0x": the original reads past the end of the text, Team B returns 0) |
| XML parser tree | 140 | 0 | D-16 differed |
| engine torque (D-17) | 1,620 | 10, last-digit only | 1,620 |
| gear shifting, notoriety | 110 | 0 | D-19, D-21 differed |
| steering limiter (D-20) | 15 | 3, last-digit or the sign of zero | 7 |
| cash wallet add (D-22) | 684 | 0 | 363 |

**D-17, the 10 remaining engine differences.** Each one is a 1-ulp float difference caused by the order of operations. The original groups the curve like this, with every intermediate value a float32 unless noted:
- `c = (TorqueFactor - 1) x inv x inv`, where `inv = 1 / (bound_RPM - Opt_RPM)`.
- `T = (c x (d x d) + 1) x Torque`. Here `d x d` is not rounded on its own; the whole expression is rounded once.
- `R = ((Res_bound - Opt_Res) x inv) x d + Opt_Res`, then `x Torque`.
- When the transmission RPM is below Min_RPM: `R = (transmission RPM / Min_RPM) x R`.

Team B computes `(inv x d)^2` and multiplies Torque first, which moves the last bit in about 0.6% of cases.

| engine | rpm | throttle | original | Team B |
|---|---:|---:|---:|---:|
| sp_mdsphere (genki) | 3500 | 1.0 | 14081.632812 | 14081.633789 |
| sp_mdsphere (genki) | 6000 | 0.5 | 5005.102539 | 5005.101562 |
| truck_4dr_pickup07 | 500 | 0.5 | 237.187500 | 237.187485 |
| car_4dr_sports03 | 4807.692383 | 1.0 | 362.511475 | 362.511536 |
| plane_jet01_car_rig | 5076.922852 | 1.0 | 444.440521 | 444.440491 |

## Notes and limits

- Emulated from a cold image: the C runtime is in its default "C" locale, so case folding is ASCII only. The game does contain the C++ library's locale-switching code but no evidence was found that it selects a non-"C" locale; if it ever did, folding of bytes 128-255 in the multiply-33 hash could change.
- Multiply-33 bucket hash: MATCH, 3,684 / 3,684 cases (614 names including bytes 0x80-0xFF, embedded control bytes and a 300-character name; bucket counts 1, 7, 4096, 9000, 0x7FFFFFFF and 0xFFFFFFFF). The rebuilt function was compiled in isolation from the read-only snapshot. Its "unambiguous" guard is not needed: the original lower-cases ASCII only and treats the result as an unsigned byte, which is exactly Team B's reading.
- Not yet compared: store-purchase respect (ships as 0) and streaking.
- Next rounds: physics step formulas, AI predicate checks, save parsers, the rest of the damage sink chain.
