# The game clock (`0x014ff338`): initial state, per-frame advance, and what `set_time_of_day` really writes

Team A, 2026-10-03. Investigative pass against the real executable (Ghidra 12.1.3 headless, `-readOnly -noanalysis`,
`ghidra/CrreishDump.java` in `xref`, `func`, `calls`, `range` and `ptrs` modes), run against three disposable private
copies of the project (`tools/gp_clock`, `gp_clock2`, `gp_clock3`, all deleted afterwards). Raw dumps stayed in the
session scratchpad and are not committed; everything below is described in my own words. No `.czn_pc` or other zone
data was touched; this is global runtime state only.

**The question (Team B mission blocker):** `dlc3_m01`, `m19` and two other missions stop on the hour byte `0x014ff33c`
being OPEN. Three things were asked: (1) the clock's state at game start and where it comes from, (2) who advances it
each frame and at what rate, (3) exactly what `set_time_of_day` (`0x00a5e370`) writes, and whether it touches anything
besides the clock.

**Short answers.**
1. The clock is a full calendar clock (year, month, day, weekday, a 30-day moon counter, and the time of day as a 32-bit
   fixed-point fraction of a day), not just an hour and a minute. At engine start-up it is set to **13 May 2004,
   10:17:33** (month index 4, 0-based), with the time scale at **40.0**. **A new game rewrites only the time: 10:00:00**,
   keeping whatever date the clock already holds, and zeroes the play-time float. Then the world-start routine may
   **snap the time to a random time-of-day key** (only when a co-op session object exists and the local machine is
   its host; whether single player has one is still OPEN in the spec). A loaded save restores all 20 bytes from the save
   header, after that random snap.
2. **`0x00702a50`** (the steady per-frame gameplay routine) advances the clock once per unpaused frame, by
   **frame time × time scale** game seconds. The frame time is `0x0132a0ac` (1/30 s by default); the time scale is
   `0x014ff34c` (**40.0** by default, so a game day lasts 36 real minutes). The same call adds the **unscaled** frame
   time to the play-time float. **The lighting does not follow the clock per frame in the shipped game**: the two
   mode flags that would make it follow (`0x014032e6`, `0x014032e7`) are never set anywhere in the image.
3. `set_time_of_day` advances the clock forward by the delta, then calls `0x005a2440`. With both mode flags clear,
   which is always the case in the shipped game, `0x005a2440` **snaps the clock to the nearest time-of-day key of the
   active TOD definition** and sets the lighting time to that key. **So after `set_time_of_day(h, m)` the hour byte
   holds the nearest key's hour, which is not necessarily `h`.** The same call also resets the seconds and the moon
   counter, recomputes the weekday, truncates the play time to whole seconds, and raises a "lighting dirty" flag. It
   does not touch the weather state machine. The listener list it walks is always empty.

---

## 0. The clock object: layout **[CONFIRMED — disassembly]**

The struct is 20 bytes at `0x014ff338` (zero-fill `.data`), which matches the save header's 20-byte copy at
`0x08C`–`0x09F`. The fields are fixed by the setter `0x006fef30`, the day stepper `0x006fee90`, the time recompute
`0x006fe990` and the readers below:

| Offset | Address | Width | Meaning |
|---|---|---|---|
| +0x00 | `0x014ff338` | u16 | year (base year 2004 in every date computation) |
| +0x02 | `0x014ff33a` | u8 | **month, 0-based** (0 = January). It indexes the 12-byte days-per-month table `0x0113e69c` (31,28,31,…) and wraps at 12. Leap years add one day to index 1: divisible by 4, and either not by 100 or by 400 |
| +0x03 | `0x014ff33b` | u8 | day of month, 1-based; reset to 1 on month rollover |
| +0x04 | `0x014ff33c` | u8 | **hour** 0–23 |
| +0x05 | `0x014ff33d` | u8 | **minute** 0–59 |
| +0x06 | `0x014ff33e` | u8 | second 0–59 |
| +0x07 | `0x014ff33f` | u8 | weekday, 0 = Monday … 6 = Sunday. Names come from the pointer table at `0x0113e6a8`. The setter computes it as (days since 1 Jan 2004 + 3) mod 7; 1 Jan 2004 was a Thursday, which is 3 |
| +0x08 | `0x014ff340` | u8 | moon-cycle day 0–29: incremented once per day, wraps to 0 after 29, **reset to 0 by every full set** through `0x006fef30` |
| +0x09–+0x0B | | | not referenced by any clock routine (padding) |
| +0x0C | `0x014ff344` | u32 | **time of day as a fixed-point fraction of a day, 2^32 units per day** (one unit ≈ 20 µs). This is the authoritative time: the hour, minute and second bytes are derived from it |
| +0x10 | `0x014ff348` | f32 | **play time in real seconds**, a separate accumulator (§2.3) |

Next to the struct, outside the 20 bytes:

| Address | Meaning |
|---|---|
| `0x014ff34c` | f32 **time scale** (game seconds per real second); not saved |
| `0x014ff330`, `0x014ff334` | the two-slot "listener" array walked after a clock jump, with count `0x014ff354` (§3.3) |
| `0x014ff350` | a byte set to 1 by `0x006fe620`, never read |
| `0x014ff358`–`0x014ff657` | a queue of 24-byte game-clock timestamps, count `0x014ff658`, drained by `0x006ff670` (§2.4) |

**How the fields relate.** `0x006fef30(year, month, day, hour, minute, second)` stores the six fields. It builds `+0x0C`
from hour/24, minute/1440 and second/86400, each scaled by 2^32 and rounded. It zeroes the moon counter and computes the
weekday. `0x006fe990` goes the other way: it rebuilds the hour, minute and second bytes from `+0x0C` with floor
divisions. The constants are 24.0 (`0x0111e4b8`), 1440.0 (`0x0111e460`), 86400.0 (`0x0113e690`), 3600.0 (`0x01115358`),
60.0 (`0x0111e478`), 2^32 (`0x0113e6c8`) and 2^-32 (`0x0111e4c0`). **The clock has a day counter** in the form of a
calendar date. There is no plain "days elapsed" integer. A 30-day moon counter feeds a moon-phase output (§3.2).

**Save-format cross-check.** `spec-save-format.md` §6.2/§6.5 reads byte 4 as the hour and byte 5 as the minute; that is
consistent with this layout. The layout adds: bytes 0–1 are the year, byte 2 the 0-based month, byte 3 the day, byte 6
the second, byte 7 the weekday, byte 8 the moon day, bytes 12–15 the fixed-point time. **The "play time" float at
`+0x10` is a different accumulator from the time of day** (§2.3). The save writer `0x00b9a380` copies the 20 bytes
into the header at `+0x8C` with the generic 20-byte copy `0x0086bbd0`. **[CONFIRMED — disassembly.]**

---

## 1. Initial state at game start

### 1.1 Engine start-up default **[CONFIRMED — disassembly]**

`0x006fe150` is called exactly once, from the start-up routine `0x005d25f0` (at `0x005d33cb`). It calls
`0x006fef30(2004, 4, 13, 10, 17, 33)` on the clock and sets the time scale `0x014ff34c` to the constant at `0x012a2e70`,
which is **40.0**. The clock therefore boots at **2004, month index 4 (May), day 13, 10:17:33, weekday 3 (Thursday),
moon day 0, play time 0.0** (zero-fill). The same literal date and time also serves as the **epoch for the co-op time
sync** (§1.5).

### 1.2 New game **[CONFIRMED — disassembly]**

The new-game routine `0x007ae950` (main-menu "new game"; horde start reaches it too):
1. copies the current 20-byte clock into a stack record (`0x0086bbd0`);
2. overwrites the record's hour, minute and second bytes with **10, 0, 0**. The record's year, month and day stay as
   they are (the "3-byte record with 10, 0, 0" in `interp_ranking_tranche_11.md` §C.1 is this hour/minute/second
   triple);
3. `0x006fe180(record)`: calls `0x0101ba90` (not read, OPEN), then `0x006fef30` with the record's six date/time fields.
   That rebuilds the fixed-point time for 10:00:00, recomputes the weekday and zeroes the moon counter;
4. `0x006fe1d0(0)`: sets the play-time float to 0.0.

So a new game starts at **10:00:00 on the date the clock already holds**: 13 May 2004 after a fresh boot, but the
date is not reset when a new game follows an earlier session. The time scale is not touched here (still 40.0 unless
something changed it).

### 1.3 World start: possible random snap to a time-of-day key **[CONFIRMED — disassembly for the code; the single-player branch depends on an OPEN spec item]**

The world-start routine `0x00708330` runs after the level is up, for both new games and loaded saves (see
`interp_delay_native.md`, `spec-save-format.md` §11.1 load-path table, row "3a"). It calls `0x00b9d620` at `0x007085c2`. That
function runs the per-frame lighting evaluator `0x00bb6690` once. Then, **only if a co-op session object exists and the
local machine is its host** (`0x0087ba20()` non-null and its `+0x5c` equals `+0x58`), it calls **`0x005a27a0`, the
"random time of day" routine**:

- It does nothing if either mode flag `0x014032e6`/`0x014032e7` is set (never, §2.2).
- It reads the active TOD definition (`0x00b9eda0`: the mission-override object `0x006e1d30()+0x2f8` when
  `0x006e1d60()` says an override is active, else `0x0290FBB0`). It takes the key count `n` at `+0x5754` and draws
  `r = 0x00dab660(0, n+1)`. That routine is a uniform pick from an 8192-entry pregenerated random table, inclusive of
  both bounds.
- index = r − 2, clamped to 0 if negative. **Key 0 is therefore three times as likely as any other key**: probability
  3/(n+2) against 1/(n+2) each.
- It calls `0x005a22d0(index)`, which snaps clock and lighting to that key (§3.2).

**Order matters for loaded saves:** this random snap runs at `0x007085c2`. The save-apply gate (`0x00b94b90`, at
`0x00708690`, when the game-flow byte `+0xD4` has bit 2) runs later in the same routine. It dispatches to the header
handler `0x00b9b530` (a computed call from `0x00b98c00`; HIGH CONFIDENCE that this is the path), which copies save
`+0x8C`–`+0x9B` into a record. If its "full" flag is set, it replaces the record's play time with the integer seconds
at save `+0xA0` converted to float, so the fractional part is dropped. Otherwise it keeps the current in-memory play
time. Then it installs the record with `0x006fe1c0` (a 20-byte straight copy, **no validation of any field**). The
clock after a load is therefore exactly the saved one. **[CONFIRMED — disassembly for `0x00b9b530`.]** Nothing in that
path re-keys the lighting to the restored hour. It calls `0x00a00e00` (an hour-vs-19 reader, not read), so the lighting
may keep the random key. **[HYPOTHESIS — not traced to the first frame.]**

### 1.4 Other one-shot sources that set the time later **[CONFIRMED — disassembly for each call]**

- **Respawn:** `0x007f0400` (fires `"Player_Respawned"`) calls `0x005a27a0` (random key) when co-op is not active
  (`0x00867830` false), horde mode is off (`0x006e44f0` false) **and no mission or activity is running**
  (`0x006cf0a0()` = −1; it returns the current mission object's `+0xa0` type, or −1). There is a third caller of the
  random routine, `0x0061c030`, gated by the host check and horde mode.
- **Mission TOD override:** `0x006e1e70` applies or removes a mission's time-of-day override. When applying, on the
  authority (or during a cutscene), and if the float `0x012f2df8` is > 0, it converts that float from decimal hours to
  HHMM and calls `0x006fe480(HHMM)`, which jumps forward to that time and snaps (§3.4). `0x012f2df8` is −1.0 in the
  image and is written only by the override-file reader `0x006e2150`. **This is the likely way a mission such as
  `dlc3_m01`/`m19` gets a defined hour.** HIGH CONFIDENCE for the mechanism. Which missions carry a start hour is OPEN
  (it lives in their override data).

### 1.5 Co-op client **[CONFIRMED — disassembly]**

The host (`0x006fe650`, called every frame from `0x00702a50`, rate-limited to once per 1000 ms by the timer at
`0x012f43a8`, and only when the local machine is the host) sends a 4-byte "seconds since the epoch 2004-05-13 10:17:33"
(`0x006feba0`) and an 8-byte value carrying the current key index (`0x005a18d0` = `0x014032e0`). The client handler
`0x006fe7b0` advances its own clock **forward only** when the host is ahead. It then applies the key index with
`0x005a2390`, which sets the lighting time only and does not snap the clock.

### 1.6 What to tell Team B for the "hour byte OPEN" stop

- **Fresh new game, no co-op session object:** hour 10, minute 0, second 0; date 13 May 2004 (after boot); time scale
  40.0; play time 0.0.
- **Fresh new game with a host session object** (the single-player case if SP keeps a one-member session — still OPEN
  in `spec-lua-api-behaviour.md`): the hour and minute of a randomly chosen key of the active TOD definition, seconds 0.
  The real key list is not available from the executable.
- **Loaded save:** bytes `0x090`/`0x091` of the save header (hour/minute), exactly as stored.
- **Inside a mission with a TOD override hour:** that hour, then snapped to the nearest key (§3.4).
- In every case the clock then **runs at 40 game seconds per real second**, so a host that freezes the hour byte will
  diverge from the original within 90 real seconds (one game hour).

---

## 2. Who advances the clock each frame, and at what rate

### 2.1 The driver **[CONFIRMED — disassembly]**

`0x00702a50` is the steady per-frame gameplay routine (`interp_mm_p_01_zscene_promotion.md` established it runs every
frame via `0x00703c00`). When the game is not paused (`0x00707490()` false), it calls, at `0x00702b11`,
**`0x006fe240(frame_time, 0, 1, 0)`**, with `frame_time` taken from `0x0132a0ac` (real seconds, 1/30 by default; clamped
and divided by the global time-scale divisor `0x0132a0bc` = 1.0, see `interp_delay_native.md`). Immediately after, it
calls `0x006fe650` (the co-op sync sender, §1.5). This is the **only per-frame caller** of the clock-advance primitives.
The other callers of `0x006fe240` are one-shot jumps: `set_time_of_day`, the debug handler at `0x006f84d0`, and
`0x007e5210` (advances by N×60 s with flags 1,0,0; caller `0x007e58a0`, not identified).

`0x006fe240(amount, skip_accumulate, quiet, ignore_zero_scale)` does the following:
1. **When `skip_accumulate` is 0** (the per-frame call): adds `amount` to the play-time float `0x014ff348`, computing in
   double and storing back as float. It calls `0x006fe0b0(amount)`, which bumps several statistics counters by the real
   time (`0x00710cb0` with ids 0, 0x0d–0x11, 0x31 when co-op, and 0x38 or 0x39 depending on a player byte `+0xa41`), and
   **it uses the time scale `0x014ff34c` as the multiplier**. When it is 1 (`set_time_of_day`), the multiplier is 1.0.
2. If not in a cutscene (`0x00722000` false) and `ignore_zero_scale` is 0 and the scale is within 0.001 of 0 (test
   `0x00dad830`), the scaled amount is forced to exactly 0. Otherwise scaled amount = multiplier × amount.
3. A scaled amount > 0 goes to **`0x006ff1f0`, the forward primitive**; ≤ 0 goes to **`0x006ff360`, the backward
   primitive**, with the sign flipped.
4. **When `quiet` is 0**, every entry of the listener array (`0x014ff330`, count `0x014ff354`) is called with the scaled
   amount. **When `quiet` is 1** (the per-frame call), the scaled amount is just stored in `0x012f43a4`. Its image value
   is 1.3333 = 40/30, i.e. "last frame's game-time step"; it is never read back by direct address.
5. Tail-call into `0x006ff670` (drain due timestamps, §2.4).

**Rate:** game seconds per frame = `0x0132a0ac` × `0x014ff34c`. The default is **(1/30) × 40 = 1.333 game s per frame
at 30 fps, i.e. 40× real time**: one game minute per 1.5 real seconds, one game hour per 90 real seconds, one game day
per 36 real minutes. The rate scales with real frame time, so it is frame-rate independent; it is not a fixed per-frame
step. **[CONFIRMED — disassembly + constants `0x012a2e70` = 40.0, `0x0132a0ac` = 1/30.]**

### 2.2 Forward/backward primitives **[CONFIRMED — disassembly; resolves the HIGH CONFIDENCE/OPEN in §32.1]**

- **`0x006ff1f0(seconds)`, the forward primitive** (called on the clock object): whole days = floor(seconds/86400)
  (`0x00ea4e80` is the CRT floor). The leftover fraction of a day is scaled by 2^32, rounded (`0x00dad930` adds 0.5 and
  truncates) and added to `+0x0C`. `0x006fe990` then rebuilds hour, minute and second. **If the 32-bit add wrapped
  (midnight crossed), one more day is added.** Finally `0x006fee90(days)` steps the date that many times. Each step:
  day +1, moon counter +1 (wraps after 29), weekday +1 (wraps after 6), month rollover via the days table with the leap
  rule, year rollover at month 12.
- **`0x006ff360(seconds)`, the backward primitive:** the mirror image. It subtracts from `+0x0C`, adds one day to the
  count when the subtraction wraps, and steps the date back with `0x006ff2b0` (not read). The only per-frame route into
  it is a scaled amount ≤ 0, i.e. a zero time scale. Then it is called with 0, which leaves the clock unchanged apart
  from the byte recompute.

### 2.3 The two accumulators are different **[CONFIRMED — disassembly]**

- **Time of day** (`+0x00`–`+0x0C`): advanced by **scaled** game seconds (×40 by default), wraps daily into the date.
- **Play time** (`+0x10`, `0x014ff348`): advanced by **unscaled real seconds**, only on the per-frame path
  (`skip_accumulate` = 0). It is never advanced by jumps such as `set_time_of_day`. It is written as a whole value only
  by `0x006fe1d0(int)` (new game: 0; the snap in §3.2: its own value truncated to whole seconds) and by the save
  restore. The save list shows `trunc(float)/60` minutes (§6.2 of the save spec); `0x006fe200` (play time / 60.0,
  truncated) has no callers.

So the desk-review hypothesis of "~0.017 s per frame" is exactly the unscaled frame time at 60 fps. It is a **real-time
play counter**, **not** the day/hour/minute clock, even though the two share the 20-byte block.

### 2.4 Time-scale writers **[CONFIRMED — disassembly for each site]**

| Site | Writes |
|---|---|
| `0x006fe150` (start-up) | 40.0 (`0x012a2e70`) |
| `0x006fe380(value)`, called from `0x006d9470` | `0x006d9470` passes 40.0. That function starts with the string `"modal"` and is called from `0x006d9f70` and `0x0061c030`: HYPOTHESIS, an end-of-mission or modal-exit cleanup |
| undefined code at `0x006d944e` | 0.0, through `0x006fe380` |
| `0x00726980` (cutscene setup, from `0x0072bff0`) | copies the scale, the full 20-byte clock and the play time into the current scene entry (`0x0153b528` `+0x3680`–`+0x3694`), then **sets the scale to 0: the clock is frozen during cutscenes**. The restore path reads them back through the entry pointer and was not found by address (OPEN) |
| `0x00837c70` | `0x01300d40`, a `.data` float = 40.0 with no other reference |
| `0x006f7ba0` / `0x006f7f80` | 0.0 / 40.0. Unnamed handlers whose addresses sit in a table built by `0x006f8630`, next to `0x006f84d0` "advance to hour N". HIGH CONFIDENCE these are debug/console commands |

### 2.5 The lighting does not track the clock per frame **[CONFIRMED — disassembly + full-image scans]**

This answers `spec-tables-environment.md` §16 open item 1 for the time-of-day side. The per-frame routine calls
`0x005a23f0` at `0x00702d36` and `0x00bb6690` at `0x00702d3b`:

- `0x005a23f0`: if byte `0x014032e6` is set, it pushes the exact clock hour/minute/second into the lighting time
  (`0x00b9ec10`), a "continuous" mode. Else, if `0x014032e7` is set, it runs `0x005a2040`, a "stepped" mode: at the
  default scale 40.0, once the clock passes the midpoint between two keys, it cross-fades the lighting time from the
  previous key to the next over **30,000 ms** (`0x012e6adc`), using the timer `0x012e6ae0` and the last target
  `0x012e6ad8`. At any other scale it snaps to the nearest key. Either way it then runs `0x005a13f0`, which recomputes
  the fade weights of the 12-byte time-window table at `0x01403260` (start, end, ramp; table identity OPEN) from the
  lighting time.
- **Neither flag is ever written.** No instruction names `0x014032e6` or `0x014032e7` as a store destination. A raw
  scan of every initialized block for the two addresses as data finds only the five or six read sites. Nothing takes
  the address of the surrounding block (`0x014032e0`–`0x014032e7`) either, and both bytes are zero-fill. **In the
  shipped game both modes are off**, so per frame only the window weights are recomputed, from an unchanged lighting
  time.
- `0x00bb6690` runs the big lighting evaluator `0x00ba8540`, which reads the lighting time `0x01311090` about 330 times
  and is the run-time consumer that blends the TOD definition's lighting by time. It also advances a separate sky
  animation clock `0x0290fb20` by frame time / 3600 × `0x013110ac` (wrapping at `0x011296a8`). It only **wraps** the
  lighting time to 0 when it reaches that limit; it never advances it.

**Consequence:** the **lighting time `0x01311090`** (decimal hours: hour + (60·minute + second)/3600, written by
`0x00b9ec10`) moves only at discrete events: world start / respawn random snaps, `set_time_of_day`, `0x006fe480` jumps
(missions, activities, cutscenes), the co-op key sync and the debug handler. **The clock itself keeps running at 40×.**
Hour-based game logic reads the running clock, not the lighting time. Examples: the readers comparing the hour with 5
and 19 (`0x0072cb75`, `0x00a00e00`, `0x00a037fa`, `0x00a05041`/`0x00a0511d`), and the HHMM window tests
`0x005a2510`/`0x005a2640` against windows at `0x013f00e0`–`0x013f00fc` through `0x00b9e910`. So the two can disagree
(e.g. a "night" hour under day lighting). The other writer of `0x01311090`, `0x00b9ec90`, was not read (OPEN).
**[CONFIRMED — disassembly for the writer census; HIGH CONFIDENCE that this matches the shipped game's stepped-at-events
day/night behaviour.]**

### 2.6 The game-clock timestamp queue **[CONFIRMED — disassembly for the drain; HYPOTHESIS for the payload]**

`0x006ff670` runs after every advance, the per-frame one included. It walks the 24-byte entries at `0x014ff358` (count
`0x014ff658`; room for 32). For each entry whose stored date/time has been reached (`0x006fe8d0`: same year, month and
day, and the stored `+0x0C` ≤ the clock's), it calls `0x00a1f990(entry+0x14 value, 0)` and `0x00a1fc60(...)`, then
removes the entry. Entries are added by `0x006ff620` and cleared by `0x006ff5e0` (not read). The callee region
(`0x00a1f8xx`) is the script-thread area: HYPOTHESIS, a "wait until game time" thread resume. Lua natives at `0x00a4ae30`
and `0x00a56ae0` build future timestamps from the clock with `0x006ff1f0` (a `"seconds"` field appears in the first),
and `0x006ff440`/`0x006ff4c0`/`0x006ff4e0` create and test such stamps.

---

## 3. What `set_time_of_day` (`0x00a5e370`) writes, exactly

### 3.1 Step 1, the forward jump **[CONFIRMED — disassembly]**

As already documented: the delta from the current hour/minute bytes, made non-negative by adding 24 h, converted to
seconds, then **`0x006fe240(seconds, 1, 0, 1)`**. With these flags:
- **play time is not touched**, and no statistics are bumped (`skip_accumulate` = 1);
- the multiplier is 1.0 regardless of the time scale (it works during a cutscene freeze too);
- `0x006ff1f0` advances the clock (so **crossing midnight steps the date, weekday and moon counter**);
- the listener array is walked (`quiet` = 0), but it is always empty (§3.3);
- due timestamps are drained (`0x006ff670`), so game-time waits that the jump passes over fire now.

### 3.2 Step 2, the key snap (`0x005a2440` → `0x005a1d10` → `0x005a22d0`) **[CONFIRMED — disassembly; resolves the §32.1 HYPOTHESIS, with a correction]**

`0x005a2440` does nothing if either mode flag is set; in the shipped game it always proceeds:

1. **`0x005a1d10`, the bracketing-key finder**, on the clock. It returns five outputs:
   - (a) the time of day as a 0–1 fraction (`+0x0C` × 2^-32);
   - (b) a **moon phase**, (hour/24 + moon day + minute/1440) / 29.0 (`0x0111e4c8` = 29.0);
   - (c) the index of the first key whose time is later than (a);
   - (d) the index before it, wrapping to the last key;
   - (e) the blend factor between (d) and (c), with day wrap-around (+1.0) handled, clamped to [0, 1].

   Keys come from the active TOD definition (§1.3): `+0x574c` points to an array of **integer HHMM times**, `+0x5754`
   holds the count. Each key is converted as (HHMM/100·60 + HHMM mod 100)/1440, the same convention as the TOD tables
   in `spec-tables-environment.md` §3.1/§9.2.
2. `0x005a2440` **picks the nearer key**: the previous key (d) if the blend factor is below 0.5 (`0x0126d2cc`), else the
   next key (c).
3. **`0x005a22d0(index)`**, the "apply key" routine:
   - `0x00b9d3d0`: sets byte `0x01310ec8` = 1. It is a dirty flag consumed and cleared by `0x00b9d300`, which runs every
     frame at the end of `0x00702a50` (HIGH CONFIDENCE it triggers the lighting re-evaluation);
   - reads key `index` as HHMM and calls `0x00b9ec10(hour, minute, 0)`: **lighting time `0x01311090` = hour +
     minute/60**;
   - stores `index` in **`0x014032e0`** (current key index; read by the co-op sender and `0x005a18d0`'s six callers);
   - reads the play time and truncates it to an integer (x87 truncate mode);
   - **rebuilds the clock** with `0x006fef30(current year, current month, current day, key hour, key minute, 0)` on a
     stack record, installs it with the 20-byte copy `0x006fe1c0`, then writes back the truncated play time
     (`0x006fe1d0`).

**Net effect of `set_time_of_day(h, m)`:** after the forward jump to h:m, the clock is **re-set to the nearest key's
HH:MM:00 on the same calendar date**. That can move it **backwards** by up to half a key interval, or forward to the
next key, possibly past midnight on the jumped-to date. The re-set fires no listeners and does not drain the timestamp
queue. It also **zeroes the moon counter**, recomputes the weekday, sets the seconds to 0, and drops the fractional part
of the play time. Only if `h:m` is itself a key time (or the definition's keys are dense) does the hour byte end up
equal to `h`. **The existing §32.1 text ("re-key lighting/weather") must be corrected: the routine re-keys the clock
itself, not only the lighting.**

### 3.3 The "listener list" at `0x014ff330` **[CONFIRMED — disassembly + scans; disproves the §32.1 HYPOTHESIS in practice]**

Four routines walk it (`0x006fe240`, `0x006fe390`, `0x006fe480`, `0x006fe7b0`). **No instruction anywhere writes
`0x014ff330`, `0x014ff334` or the count `0x014ff354`, and no data in the image points at them.** All three are zero-fill.
The count is therefore always 0 and the walk never calls anything. The array has room for only two pointers before the
clock struct begins. A host can ignore it.

### 3.4 Things `set_time_of_day` does not do **[CONFIRMED — disassembly, within the call tree read]**

- **No weather change:** no call into the weather/wind state machine (`0x005a9640`–`0x005aa580`), and no write to
  `0x0140e5xx`.
- **No day/night object pass:** the sibling `0x006fe480` ("advance to HHMM", used by missions, activities and cutscenes:
  callers `0x006e1e70`, `0x0063d490`, `0x006509a0`, `0x00667c40`, `0x0072c980`, `0x00bc6ad0`, `0x00bcbd30`, plus
  undefined code at `0x006e6dc3`, `0x006fe63f`, `0x00738420`) additionally pushes the exact clock time into the lighting
  before snapping. It also tests "is it day" (`0x00ba2840`: lighting time within the TOD definition's
  `+0x5744`..`+0x5748` window, confirming the `day_begin`/`day_end` HYPOTHESIS of `spec-tables-environment.md` §15.5).
  When that goes from day to night, it walks the world object list (`0x03171a64`) and, for objects with bit 3 of
  `+0x33`, a non-null `+0x138` with bit 7 of `+0x88`, and bit 0 of `+0x3b` set, clears that bit and calls
  `0x008cc930(2)`. HYPOTHESIS: switches lights on. **`set_time_of_day` does none of this.**
- **No statistics, no play-time advance, no network message.** Clients are corrected by the host's periodic sync
  instead (§1.5).

---

## 4. Cross-function observations

- **There are four clock-setting entry points**, all forward-only except the snap:
  - `set_time_of_day` (hour, minute; delta wrapped by +24 h);
  - `0x006fe390(h, m, s)` (from undefined code at `0x0061d9e7`). It computes its delta in **8-bit arithmetic**:
    (h − cur + 23)·3600 + (m − cur + 59)·60 + (s − cur + 60), minus 86400 when above 86400, and applies no snap;
  - `0x006fe480(HHMM or HH)`: a value above 99 means HHMM, otherwise only the hour moves. It is followed by the exact
    lighting push, the snap and the day/night pass;
  - the debug "advance to hour" at `0x006f84d0`, with flags (1, 0, 0), so it does not ignore a zero scale.
- **The co-op epoch and the boot default are the same literal** (2004, 4, 13, 10, 17, 33), which is pushed in three
  places (`0x006fe150`, `0x006fe650`, `0x006fe7b0`).
- **The key-index global `0x014032e0` and the "applied" byte `0x014032e4`** are reset by `0x005a18f0` (from
  `0x00707c80`) and set by `0x005a22d0`/`0x005a2390`. `0x014032e5` is a separate enable for the window test
  `0x005a2510`, set by `0x007218b0`.
- **The cutscene freeze:** `0x00726980` sets the scale to 0 and snapshots the clock, and `0x00722000` (the
  "cutscene running" test, which reads `0x0153b520`/`0x0153b528`) shows up inside `0x006fe240`/`0x006fe390`/`0x006fe480`.
  A host that advances the clock during cutscenes will drift from the original.
- **`0x00dab660` is a table RNG** (8192 pregenerated values, cursor `0x013214d0`). The random key at world start and
  respawn is reproducible only if that table and cursor are reproduced.

---

## 5. Crash-shaped and correctness hazards noticed

1. **Empty key list (crash-shaped).** If the active TOD definition has 0 keys (`+0x5754` = 0):
   - `0x005a27a0` still computes index 0 and `0x005a22d0` reads `keys[0]` through the `+0x574c` pointer, which may be
     null;
   - `0x005a1d10` skips its search loop and then reads `keys[count − 1]` = `keys[−1]`;
   - `0x005a2040` reads `keys[count − 1]` too.

   No count check anywhere. This is reachable with a malformed or empty mission override definition.
   **[CONFIRMED — disassembly for the missing checks; reachability HYPOTHESIS.]**
2. **Unvalidated save restore.** `0x00b9b530` → `0x006fe1c0` copies the 20 saved bytes verbatim. A month byte ≥ 12 makes
   the day stepper `0x006fee90` index past the 12-byte days table (into the weekday-name pointers): garbage month
   lengths rather than a crash. An hour ≥ 24 persists until the next advance recomputes the bytes from `+0x0C`.
   **[CONFIRMED — disassembly.]**
3. **Play-time float stalls.** The per-frame add is done in double but stored as float32. At 60 fps (step 0.0167 s)
   the step falls below half the float spacing once play time reaches 2^19 s (about 146 h). At 30 fps (step 0.0333 s)
   this happens at 2^20 s (about 291 h). Past that point the play time stops advancing; shortly before it, each add
   rounds up and the counter runs fast. The save-list minutes and `+0xA0` inherit this. Not a crash.
   **[CONFIRMED — arithmetic on the disassembled store; not observed in the 16 real saves, whose largest is about
   185,000 s.]**
4. **Moon phase can exceed 1.0:** the moon day runs 0–29 but is divided by 29.0. Cosmetic. **[CONFIRMED.]**
5. **Backward snap without queue drain:** the snap in §3.2 can move the clock back after `0x006ff670` already ran,
   so a timestamp that fired during the jump stays fired while the clock reads an earlier time.
   **[CONFIRMED — order of calls.]**
6. **Listener array overlaps nothing only because it is never filled:** it has two slots directly before the clock
   struct, and any third registration would overwrite the year/month/day. It is unreachable in this build.
   **[CONFIRMED — no writers.]**
7. **Timestamp queue capacity 32**: the bound check in the adder `0x006ff620` was not read. **[OPEN.]**

---

## 6. OPEN

- **Whether single player has a host session object.** This decides whether a fresh new game keeps 10:00:00 or gets a
  random key at world start (§1.3). It is the same open item as `spec-lua-api-behaviour.md` (`game_get_is_host` in
  single player).
- **The actual key times** (`+0x574c` HHMM list) of the base TOD definition at `0x0290FBB0` and of each mission override:
  they are loaded data, not in the executable. Team B needs them to predict the snapped hour.
- **Which missions set a start hour** through `0x012f2df8` (written by the override reader `0x006e2150`), and whether
  `dlc3_m01`/`m19` are among them.
- `0x0101ba90` (called at the start of the new-game time set), `0x006ff2b0` (the backward date step), `0x00b9ec90`
  (second writer of the lighting time), the cutscene clock restore path, `0x006ff620`/`0x006ff5e0` (queue add/clear),
  `0x00a1f990`/`0x00a1fc60` (queue payload), the identity of the `0x01403260` window table, and the purpose of
  `0x007e5210` (an N-minute advance).
- Whether the lighting is re-keyed after a save restore (§1.3).

---

## 7. Clean-room self-check

Pattern given by the task, run against this file:
`\b(iVar[0-9]+|uVar[0-9]+|piVar[0-9]+|puVar[0-9]+|pvVar[0-9]+|fVar[0-9]+|sVar[0-9]+|cVar[0-9]+|bVar[0-9]+|pcVar[0-9]+|pbVar[0-9]+|local_[0-9a-fA-F]+|uStack_?[0-9a-fA-F]+|auStack_?[0-9a-fA-F]+|unaff_[A-Za-z0-9]+|extraout_[A-Za-z0-9]+|undefined[0-9]+|param_[0-9]+|in_[A-Z]{2,4}|LAB_[0-9a-fA-F]+|FUN_[0-9a-fA-F]+|DAT_[0-9a-fA-F]+)\b`
— result recorded below after the run. The text uses raw addresses, register names and my own descriptive names
only; no pseudocode was copied.

Result: 0 hits (checked with ripgrep after writing).
