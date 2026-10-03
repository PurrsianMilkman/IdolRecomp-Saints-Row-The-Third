# Ranking tranche 22 — 25 previously-unspecced Lua-bound names (Team A, 2026-10-03)

Job definition: `D:\Project Crreish\TEAM A\ghidra\jobs\ranking\tranche-22.json`. The job file's own title states **names
476-500 of the 554 unspecced names, Team B call-count order**; this note uses that range and exactly the 25 names the job
lists. Run locally, read-only, on a private copy of the Ghidra project (`tools\gp_t22`, deleted afterwards), with the
job's own arguments: `CrreishDump.java <out>/lua lua depth:1 maxfuncs:15 maxinsn:500 <25 names>`. **All 25 names
resolved.** Every name string occurs exactly once in `.rdata`, and for all 25 the handler is the code pointer stored in
the slot right after the name (insn offset +1) — CONFIRMED — dump `index.txt`. No name in this tranche is registered by
a pointer-before-name one-name registrar, and no candidate landed on the `lua_setfield` primitive.

Follow-up runs on the same private copy, cited by name below:

- "follow-up dump": depth-0 `func` run (`maxinsn:900`) on 20 callees (listed in section L);
- "second follow-up dump": depth-0 `func` run on the Lua index resolver `0x00dfdc60`, the play-mode apply routine
  `0x00704a50` and the flashpoint stop routine `0x008d9970`;
- "ptrs run": `ptrs` mode (8 dwords) on `0x012f47a8`, `0x013010d0`, `0x012a2f08`, `0x012a2f10`, `0x012a2d70`;
- "xref run": `xref` mode on `0x00706a40`, `0x0250ab68`, `0x0250ab69`, `0x0250ab6c`, `0x0231758c`, `0x02317588`,
  `0x02317590`, `0x027043ac`, `0x027043ba`, `0x012f45cc`, `0x011697c8`.

Call counts are Team B's (`D:\Crreish-sync\for-team-b\team-b\tools\lua_reconciliation_called_and_registered_1181.tsv`):
**every name in this tranche is 1 call site in 1 script.** Team B tags the 11 `game_*` names `ui` and the other 14
`gameplay`; section A shows the registrars agree exactly (11 in the UI helper registrar `0x00845aa0`, 14 in the gameplay
registrar `0x00a20840`).

**Already partly covered elsewhere (cited, re-verified, not re-derived):** tranche 12 section A already lists
`game_is_german_build`, `game_is_demo` and `game_is_debug_reloading_level` among the aliases of the constant-false stub
`0x00a3c670`, and `game_event_tracking_interface_exit` among the aliases of the shared no-op stub `0x007c9f50`, but none
of the four has a body entry; this note gives them one (B.1, B.5). `game_cancel_mission` reads the in-progress type
through `0x006ced00`, which `spec-lua-api-behaviour.md` §27.25 (`game_get_in_progress_type`) already documents; it is
cited, not re-derived. `fade_get_percent` reads the fade-state dword whose encoding §26.24 already confirms.
`follower_is_unconscious` uses the "downed" predicate `0x009715d0` that §7.13 (`human_is_downed`) already confirms.

Labels: **CONFIRMED** = read in the listing of a dump made for this note; **HIGH CONFIDENCE** = follows from a dumped call
or reference, but the callee body was not dumped or a meaning is inferred from strong usage; **HYPOTHESIS** = plausible,
not settled; **OPEN** = not settled (collected in section N).

**Shared conventions, cited not re-described:** every entry opens with the `lua_gettop` prologue (`0x00dfde50`) and
reads arguments with negative stack indices counted from the bottom (`-N` = arg 1, `1-N` = arg 2, …). Primitives as in the
tranche 09/13/16/18/20 front matter: `0x00dfe210` `lua_tolstring` (non-string reads as null), `0x00dfe1e0`
`lua_toboolean`, `0x00dfe040` `lua_type` (0 = nil, 4 = string, 5 = table; −1 for the "none" sentinel), `0x00dfe160`
`lua_tonumber` (non-number reads as 0), `0x00ea2596` truncating float-to-int, `0x00dfe590` `lua_pushboolean`,
`0x00dfe3a0` `lua_pushnumber`; `0x00dfe420` pushes a C string (null pushes nil). "Nil-gated optional argument" = the
tranche 13 idiom; an **unconditional** read has no presence check (see M.1 for what a missing argument really reads).
Resolvers: the generic character resolver `0x00a28150` (null-safe, liveness-checked, tranche 20), the special-name
resolver `0x00a3bd20` / `0x00a3be60` (tranche 20 B.6), the per-kind named-object lookups on `0x02442750` of tranche 16/18's
front matter (`0x005982e0` kind row `+0xb` bit `0x02`, the "5th resolver" of §7.5 / tranche 03 T3.4.2; `0x00734e90` kind
row `+0x6` bit `0x02`, §10.5); liveness `0x00853b10` (true = null, flagged `+0x33` bit `0x04`, or class byte `0xff`).
Session getter `0x0087ba20`; the standard record trio (`0x0086f5f0(opcode)` open, `0x00881110`/`0x00881040` write,
`0x0086f1b0(session, 0, 0)` broadcast commit, `0x0086eb20` close); **with no session the `0x0086f1b0` commit returns
without acting** (`interp_tabs.md`, Q-section on the commit, and `spec-lua-api-behaviour.md` §8.27/§26.28: single player
has no session object at all). Single gate `0x008addb0(object, 0)` = "this machine has authority over the object". The
per-character combat-action ("CAE") record, its 70-row action table `0x012e1c60`, the staging writer `0x004f4b90` and
the committing writer `0x004f4d00` are tranche 03 T3.4 (action 0 = `fire`, 4 = `throw grenade`). Constant-1 stubs whose
results are discarded (`0x00d34d90`, `0x00d218f0`) are the tranche 12 family and are omitted from the bodies below.

---

## A. Registrars — where the 25 names live

Two registrars. CONFIRMED — `index.txt`, cross-checked against `tools/lua_game845aa0_full.txt` and
`tools/lua_gameplay_names_only_1015.txt`:

| registrar | names in this tranche |
|---|---|
| UI helper `0x00845aa0` (113 rows; `spec-lua-bindings.md` §13.5) | 11: `game_is_going_to_main_menu`, `game_is_german_build`, `game_is_demo`, `game_is_debug_reloading_level`, `game_get_pending_game_play_mode`, `game_get_game_play_mode`, `game_get_coop_ping`, `game_format_int_to_string`, `game_event_tracking_interface_exit`, `game_cancel_mission`, `game_audio_set_rtpc` |
| gameplay `0x00a20840` | 14: `force_throw_do`, `force_throw_char_do`, `force_fire_target_do`, `force_fire_do`, `follower_is_unconscious`, `flashpoint_start_mission`, `flashlights_enable`, `flashlights_disable`, `fade_get_percent`, `dlc3_m03_punch_unload`, `dlc3_m03_punch_load`, `dlc2_m02_clapboards_set`, `dlc2_m02_clapboards_reset`, `dlc2_m02_clapboards_get` |

- **Pairing direction in `0x00845aa0`.** Because several neighbours share handlers, the +1 reading was checked against two
  rows whose handlers are already specced in the same table: `game_audio_stop` → `0x00843ac0` (§11.7) sits one pair
  before `game_audio_set_rtpc`, and `game_get_in_progress_type` → `0x008447b0` (§27.25) one pair before
  `game_cancel_mission`. Both are name-then-function, so `game_is_going_to_main_menu` → `0x00842750` and
  `game_is_debug_reloading_level` → `0x00a3c670` (not the other way round). CONFIRMED.
- `game_is_german_build` and `game_is_demo` have `0x00a3c670` on **both** sides, so their handler does not depend on the
  pairing direction. CONFIRMED.
- Gameplay neighbours (names file order): `flashpoints_enable` / `flashpoint_start_mission` / `flashpoint_mission_status`;
  `force_fire_do` / `force_fire_pull_alt_trigger` / `force_fire_target_do`; `force_throw_do` / `force_throw_char_do` /
  `force_throw_from_vehicle`; `dlc3_m03_fake_tag_punch_hit` / `dlc3_m03_punch_load` / `dlc3_m03_punch_unload`. Only the
  names in this tranche were read.

---

## B. Game-state and UI queries — 8 functions (UI helper registrar)

### B.1 `game_is_german_build`, `game_is_demo`, `game_is_debug_reloading_level` → `0x00a3c670`

**Arguments:** none read. **Return:** exactly **1 boolean, always false**. CONFIRMED — the four-instruction body
(prologue, push false, return 1), tranche 12 B.1's constant-false stub.

So in this executable the German-content switch, the demo switch and the debug level-reload flag all read as off,
unconditionally. A reimplementation must answer false for all three to match; scripts that branch on
`game_is_german_build` always take the non-German path here. CONFIRMED.

### B.2 `game_is_going_to_main_menu` → `0x00842750`

**Arguments:** none read. **Return:** exactly 1 boolean. CONFIRMED — listing.

**Body (CONFIRMED — listing, follow-up dump):** the answer comes from the **game-state stack** (`interp_nnlt.md` §4.6:
stack `0x01503b50`, top index `0x012f4a80`; not the game-play mode of B.3):
1. `0x00706b90()` returns the **projected** top state — the top after the still-queued push/pop requests are applied —
   cached in `0x01503b90` and recomputed only when the "changed" byte `0x012f4a84` is 1 (then cleared). **With an empty
   request queue it returns 0**, not the current top. The projection is `0x00706b00`, which copies the 16-dword stack
   into a local buffer and replays the request queue (`0x012f4a88`, count `0x012f4a90`): push → index + 1 and store;
   pop → index − 1 when positive.
2. Projected state **2 → true**.
3. Otherwise the **current** top (`0x00706ab0`) is read twice: **5 or 6 → false**; anything else → **true**.

So the binding answers **false only while state 5 or 6 is on top and no switch to state 2 is queued**; every other
state reads as "going to the main menu". HYPOTHESIS: state 2 is the front end and 5/6 are the in-game states
(`interp_nnlt.md` reads mode 5 as the loading screen; no registration naming the states was read — the ten callers of
the state registrar `0x00706a40` are listed by the xref run, not opened).

**Latent defect (CONFIRMED structure, reachability OPEN):** `0x00706b00`'s push replay has **no bound**: the real pump
refuses pushes beyond 15 entries and pushes of a state already on the stack (`interp_nnlt.md` §4.6), the projection does
neither, and writes into a 16-dword stack buffer of its caller `0x00706b90` (`SUB ESP,0x44`). A queue holding enough
pushes would write past that buffer. When the queued requests pop the stack empty, the projection reads the index slot
itself and returns −1 — harmless here (it is not 2).

### B.3 `game_get_game_play_mode` → `0x00841f70` and B.4 `game_get_pending_game_play_mode` → `0x00841fc0`

**Arguments:** none read. **Return:** exactly 1 string. CONFIRMED — listings.

**Body (CONFIRMED — listings, ptrs run):** the current game-play mode is the dword `0x012f45c8` (getter `0x007044f0`;
file value **2**), the pending one `0x012f45cc` (getter `0x00704960`; file value **−1**). −1 pushes the literal
**`"None"`** (`0x01124a88`); any other value `m` pushes the name at `0x012f47a8[m]` via `0x00704950` (no bound check).
The name table, read from the file image: **0 `"Activity"`, 1 `"Diversion"`, 2 `"Normal"`, 3 `"Mission"`**. The
per-mode record table at `0x012f45d8` (stride `0x74`) ends exactly where the name table starts, and the "reset" routine
`0x00704440` loops over exactly four records, so there are **four** modes; the strings after index 3 (`"MULTI_MODE_1"`,
`"MULTI_MODE_4"` twice, `"MULTI_MODE_5"`) are not reachable through these getters (HIGH CONFIDENCE: another table).

Mode mechanics (follow-up dumps): `0x00704500(m, arg)` requests a change — ignored when `m` is already current,
otherwise pending = `m` and `0x014ff760` = `arg`; `0x00704520` cancels a request (pending = −1); `0x00704a50` applies
it (current mode's "leave" callback at record `+0xc`, current = pending, pending = −1, the new mode's "enter" callback
at record `+0x8`, plus an opcode-`0x34` record — not traced); `0x00704440` forces mode 2 and clears the request.

**Return-shape note (CONFIRMED):** the result is an **untranslated English identifier**, never nil. The current mode is
never `"None"` in practice (nothing writes −1 to `0x012f45c8`); the pending mode is `"None"` whenever no change is
queued. Safe (the indices are internal).

### B.5 `game_event_tracking_interface_exit` → `0x007c9f50`

**Arguments:** none read. **Return:** 0 values. **Body:** the shared no-op stub (spec §6.1). CONFIRMED. Its sibling
`game_event_tracking_interface_enter` is bound to a real function (`0x008424c0`, not read), so the pair is asymmetric:
whatever "enter" does, "exit" does not undo it (consequence OPEN).

### B.6 `game_get_coop_ping` → `0x008446e0`

**Arguments:** none read. **Return:** exactly 1 number. CONFIRMED — listing.

**Body (CONFIRMED — listing):** `0x008b7100()` — the first machine in the session's member list that is not the local
one (tranche 17; walks session `+0x54`, next pointer `+0xb28c`, skipping session `+0x5c`), or null when there is no
session or no other machine. Null → **0**. Otherwise the dword at machine record **`+0xb200`** is pushed as an
**unsigned** 32-bit value (the `FILD` result gets 2^32 added when the dword is negative). HYPOTHESIS: `+0xb200` is the
measured round-trip time (units OPEN). In single player there is no session (§8.27/§26.28), so this always answers 0
there. Safe.

### B.7 `game_format_int_to_string` → `0x00841c90`

**Arguments:** 1 number (unconditional). **Return:** exactly 1 string. CONFIRMED — listing.

**Body (CONFIRMED — listing, ptrs run):**
1. The Lua number is stored as a **single-precision float** (`FSTP float` at `0x00841cbb`), widened back to double,
   then **floored** with the SSE "add and subtract 2^52 with the input's sign, then subtract 1.0 if the result went up"
   idiom (constants read from the file image: sign mask `0x8000000000000000` at `0x012a2f08`, 2^52 at `0x012a2f10`,
   1.0 at `0x012a2d70`), narrowed to float again and converted with truncation (`CVTTSS2SI`).
2. `0x00da74a0(buffer, 0x80, value)` formats it: `sprintf("%d")` into a 32-byte local, then, if the grouped length fits
   (`length + groups < 0x80`), copies it with a **comma every three digits** from the right (the first group is
   `digits mod 3`, or 3, characters long, plus the minus sign). The separator is always `','`; no locale is consulted.
3. The result is pushed as a string.

**Precision / range notes (CONFIRMED arithmetic):**
- Because of the float round-trip, integers above **16,777,216** (2^24) are rounded to the nearest representable float
  before formatting — e.g. 16,777,217 prints as "16,777,216", and amounts near the $20,000,000 cash cap
  (tranche 20 F.2) can print off by one or more.
- Rounding is **floor**, not truncation: −1.5 prints "−2", 1.9 prints "1" (compare `get_world_income_dollars`, which
  truncates toward zero).
- Values outside the 32-bit range, and NaN, convert to the integer-indefinite value and print
  **"-2,147,483,648"**.
- The "does not fit" branch of `0x00da74a0` returns without writing the buffer, which would then push uninitialised
  stack bytes; with this binding's 128-byte buffer and at most 14 output characters it cannot be taken (latent only).
- A call with no argument reads stack index 0 (M.1): the slot just above the arguments, i.e. stale content, not a clean 0.

---

## C. `game_cancel_mission` → `0x00842a20` (UI helper registrar)

**Arguments:** none read. **Return:** 0 values. CONFIRMED — listing.

**Body (CONFIRMED — listing):** reads the in-progress type `0x006ced00()` (§27.25: the active mission's `+0xa0` type when
the mission pointer `0x014c8460` is set and the phase `0x014c7a14` ≠ 8; else **4** when the cat-and-mouse/diversion
singleton `0x014c2d10` exists; else, in phase 6/7, the `+0xa0` type of the pending record `0x014c8328`; else −1).
**Type 4 → `0x006a3990`; anything else → `0x006dbcc0`.**

### C.1 Type 4 — `0x006a3990` (cancel the diversion immediately)

CONFIRMED — listing (depth-1 dump):
1. If the singleton `0x014c2d10` exists and the virtual `+0x8` of its `+0x14` sub-object does not return −1, the
   singleton's own virtual `+0x44` is called (HYPOTHESIS: abort the running diversion).
2. `0x00704500(2, 0)` — requests game-play mode 2, `"Normal"` (B.3).
3. If `0x007e1f40()` (true when `0x007b3c50` or `0x007b4b20` on the UI object `0x012fced8` reports screen 9 or 10;
   HYPOTHESIS: a full-screen menu is up) → `0x007b4990(-1)` on `0x012fced8` (HYPOTHESIS: close it).
4. Localises `"DIVERSION_CANCELED"` / `"(LOCALIZE) Diversion canceled"` (`0x0084a1b0`) into `0x012f0014`, then writes
   singleton byte `+0x8` = 0, byte `+0x9` = 1, float `+0xc` = 0.0, byte `+0x8` = 0 again, and shows the text through
   `0x007ff3e0(text, 0, [0x012ffd14])` (HYPOTHESIS: a HUD message).

No confirmation dialog is shown for a diversion; it is cancelled at once.

**Crash shape (CONFIRMED shape, reachability OPEN):** only step 1 is guarded by a null test. The writes of step 4
re-load `0x014c2d10` and store through it **unconditionally** (`0x006a39ed`-`0x006a3a10`). The singleton is cleared by
`0x006a4440` (§27.1, Q8.1). If the virtual `+0x44` of step 1 reaches that clear synchronously, step 4 writes to
addresses `0x8`/`0x9`/`0xc`. From this binding the singleton exists on entry (type 4 is only returned when it does), so
the question is only what `+0x44` does.

### C.2 Any other type — `0x006dbcc0` (open a "cancel?" confirmation dialog)

CONFIRMED — listing (depth-1 dump):
1. Clears byte `0x0140e6dc`.
2. Unless co-op is active (`0x00867830`, the `coop_is_active` predicate of §3.1) or `0x007e1f40()` is true, calls
   `0x007074a0(14, 1)` (HIGH CONFIDENCE: takes a game-pause reference with reason 14 — it raises the counter
   `0x01503eb0`, sets the per-reason byte `0x01503e00[14]`, and on the first reference runs the freeze calls).
3. Reads **`[0x014c8460] + 0xa0` without a null test** (`0x006dbcfa`) and switches on it:
   - **0 (mission):** failure text `"MISSION_FAILURE_CANCEL_TEXT"` / `"You cancelled the mission."` localised into
     `0x014c7a08`, the tag copied (`strncpy`, 127 bytes) to `0x014c7988` and NUL-terminated at `0x014c7a07`, its name
     hash (`0x00d9e8b0`) stored in `0x014c7984`; then a dialog titled `"MISSION_CANCEL_TITLE"` (`"CANCEL MISSION?"`) with
     body `"MISSION_CANCEL_TEXT"`.
   - **1 (activity):** the same with the `"ACTIVITY_FAILURE_CANCEL_TEXT"` / `"ACT_CANCEL_TITLE"` / `"ACT_CANCEL_TEXT"`
     strings — except that when `0x006d2910()` (the mission `+0x164` sub-object's type, tranche 20 C.3) is **5**, the
     dialog uses `"COMPLETION_QUIT_ZOMBIE"` / `"ACT_ZOMBIE_TRIGGER_WARNING"` instead (HYPOTHESIS: the zombie activity).
   - **3 (stronghold):** `"STRONGHOLD_FAILURE_CANCEL_TEXT"`, `"STRONG_CANCEL_TITLE"` (`"CANCEL STRONGHOLD?"`),
     `"STRONG_CANCEL_TEXT"`.
   - **Any other value:** no dialog — falls straight to the store described below with a **null** dialog pointer.
4. The dialog is opened with `0x007c3de0(title, body, 0x006dae00, 2, 0, 0)` (result callback `0x006dae00`), and the
   binding then writes **dialog `+0x118` = 1** and copies **dialog `+0x12c` into `0x014c833c`** — with no null test.

So for missions, activities and strongholds `game_cancel_mission` only **asks**; the cancel itself (with the stored
failure text) is HYPOTHESIS: done by the dialog's result callback `0x006dae00` when the player confirms (not read).

**Crash 1 — null mission pointer. CONFIRMED structure (`0x006dbcfa`).** `0x006dbcc0` is reached for every type other
than 4, including **−1 ("nothing in progress")** and the phase-6/7 case in which the type came from the *pending* record
`0x014c8328` while `0x014c8460` is null. In both, `[0x014c8460]` is 0 and the read of `[0 + 0xa0]` faults. Trigger: a
script (or a UI page) calls `game_cancel_mission` while no mission, activity or diversion is running.

**Crash 2 — unhandled type. CONFIRMED structure (`0x006dbefb`-`0x006dbf01`).** For a mission type other than 0, 1, 3
the default branch stores 1 to **address `0x118`** and reads **address `0x12c`** (the dialog register was zeroed at
`0x006dbd00` and never set). Which types besides 0/1/3/4 occur in shipped content (e.g. 2) is OPEN.

**Crash 3 — dialog pool exhausted. CONFIRMED structure (`0x006dbd9f`, `0x006dbe58`, `0x006dbee6`).** `0x007c3de0`
returns null when `0x007c3310` finds no free slot — **at most four dialogs can be open at once** (§27.11, Q7.2). The
store to dialog `+0x118` is unconditional, so a fifth concurrent dialog — e.g. `game_cancel_mission` called repeatedly
before the player answers, since each call opens a new dialog — writes to address `0x118`. Also note that every call
takes another pause reference in step 2 before the dialog exists.

**Fix template:** test `0x014c8460` and the dialog pointer before use, and treat unknown types like −1 (do nothing).

---

## D. `game_audio_set_rtpc` → `0x00843af0` (UI helper registrar)

**Arguments:** 1 number = playing-instance id (unconditional, truncated); 2 RTPC = a **string** (when `lua_type` is 4,
resolved through the Wwise name resolver `0x00462960` → `0x0046fd00`, `AK::SoundEngine::GetIDFromString` per §2's method
note) **or a number** (truncated); 3 number = value (unconditional; read as float). **Return:** 0 values. CONFIRMED —
listing.

**Body (CONFIRMED — listing):** nothing happens unless **both** the instance id and the RTPC id are non-zero (so the
string `"none"`, which the resolver maps to 0 per `spec-tables-traffic-ai.md`, is a no-op). Otherwise
`0x0045f470(rtpcId, value, instanceId)` (depth-1 dump):
- audio not initialised (byte `0x031728c2` clear) → returns −1;
- otherwise under the audio lock (`0x00d9f620`/`0x00d9f630` on `0x031f0260`): resolves the instance id with
  `0x004613a0` and maps it with `0x0045c870` — the same two steps `game_audio_stop` (§11.7) uses; an unknown instance →
  returns −100 (ignored by the binding);
- calls `0x0045c230(this = instance data, rtpcId, value, 0.009)` (the third value is the float `0x3c1374bc` at
  `0x012a2ff4`; HYPOTHESIS: an interpolation time); a result of 0 sets bit `0x10` of the instance's word `+0x98`
  (HYPOTHESIS: "RTPC pending" retry flag), 1 is success, any other value is returned (and ignored).

HIGH CONFIDENCE that arg 1 is the id `game_audio_play`-family calls return, because `game_audio_stop` (§11.7) consumes
the same id through the same two lookups. The RTPC is applied **per playing instance**, not globally. No crash shape (the
instance lookup fails cleanly). With only two arguments, arg 3 is read from stack index 0 — stale content (M.1).

---

## E. Forced combat actions — 4 functions (gameplay registrar)

All four resolve arg 1 (character name, unconditional) through `0x00a28150` and drive the tranche 03 CAE record.

### E.1 `force_fire_do` → `0x00a4a780` and E.2 `force_fire_target_do` → `0x00a4a850`

**Arguments:** 1 string = character; 2 string = target (unconditional); 3 nil-gated boolean, default **false** (read only
when more than two arguments are present). **Return:** exactly 1 boolean. CONFIRMED — listings.

The two handlers are identical except for the **target resolver**: `force_fire_do` uses `0x005982e0` (kind row `+0xb`
bit `0x02` — the navpoint-like "5th resolver"), `force_fire_target_do` uses `0x00734e90` (kind row `+0x6` bit `0x02`,
§10.5). In both the target must be alive (`0x00853b10`). CONFIRMED.

**Body (CONFIRMED — listings, depth-1 dump of `0x0050cb90`):** if the character and the target both resolve,
`0x0050cb90(character, target, flag)`:
1. writes the target's handle pair (`+0x8`/`+0xc`) into the **staged** action block at character `+0x4b0`/`+0x4b4`;
2. sets bit 2 (`0x04`) of character byte `+0x4fd` to the flag (arg 3);
3. `0x004f4d00(character, 0, 0)` — **commits CAE action 0, `fire`** (tranche 03 T3.4.2: on the authority machine the
   staged block is copied to the committed copy at `+0x450` immediately; otherwise it is forwarded with `0x004f4830`).

Then pushes **true**; if either name fails to resolve, pushes **false** and changes nothing.

**Logic notes (CONFIRMED structure):**
- Unlike `ai_suggest_action` / the throw bindings (E.3/E.4), `0x0050cb90` does **not** reset the staged block from the
  `0x012e14e0` template first, so whatever else an earlier suggestion left in the staged block (e.g. a target position
  at `+0x4c0`) is committed along with the new target. Consequence HYPOTHESIS.
- There is no "busy" test (tranche 03's `+0x44c` bit `0x08`): a running action is overridden.
- `true` means "both names resolved and the request was issued", not that the character fired.
- The meaning of `+0x4fd` bit 2 (arg 3) is OPEN.

No crash shape (both lookups are checked).

### E.3 `force_throw_do` → `0x00a4a920`

**Arguments:** 1 string = character; 2 string = target (unconditional, `0x005982e0`, must be alive). **Return:** exactly
1 boolean. CONFIRMED — listing.

**Body (CONFIRMED — listing, depth-1 dump):** false unless the character and target resolve **and**
`0x005fe490(this = character + 0x1b78)` returns non-null. `0x005fe490` (depth-1 dump) first asks `0x005fe3e0(0)` for an
item and accepts it when `0x00b81600(item + 0x19c)` is true; otherwise it resolves the handle at `+0x88`/`+0x8c` of the
same sub-object (kind row `+0x9` bit `0x80`, alive). HYPOTHESIS: "the character has a throwable item equipped or held".
Then `0x004f4b90(character, 4, target handle, force = 1, token = 0)` — **stages CAE action 4, `throw grenade`**, exactly
as `ai_suggest_action` does (tranche 03 T3.4.1: resets the staged block from the template, writes action and target,
allocates a request token through `0x006f62b0(character, 3, 0)`, forwards to the owner with `0x004f4830(…, 3000)` when
this machine lacks authority). Pushes **true**.

So the throw is only **staged** (a suggestion the AI commits later, HYPOTHESIS per tranche 03), whereas `force_fire_*`
commits at once. No crash shape.

### E.4 `force_throw_char_do` → `0x00a4a6d0`

**Arguments:** 1 string = character (unconditional); 2 string = target character (unconditional). **Return:** exactly 1
boolean. CONFIRMED — listing.

**Body (CONFIRMED — listing, follow-up dump of `0x00a3bd20`):**
1. character = `0x00a28150(arg 1)`;
2. target = `0x00a3be60(arg 2, character + 0x40)` — `0x00a3bd20` first (a live named object via `0x00a27e20`; else
   **`"#CLOSEST_PLAYER#"` or `"#PLAYER#"` = the player closest to the given position**, tranche 20 B.6), falling back to
   `0x00a28150`;
3. false unless both resolve and `0x005fe490` (E.3) finds an item;
4. `0x009659f0(character, target)` (depth-1 dump): when this machine has authority, stores the target's handle in
   character `+0x1330`/`+0x1334`, and if it changed restamps `+0x13a8` and (for class row `+0xa` bit `0x04` characters)
   runs the retargeting calls `0x00600720`, `0x004dedb0`, `0x00b6cc70`; without authority it forwards the request
   (`0x004dedb0`, or AI event `0x4d` through `0x004f5dd0` when clearing). HIGH CONFIDENCE: "make the target the
   character's combat target";
5. `0x004f4b90(character, 4, target handle, 1, 0)` — stages `throw grenade` at the target (as E.3); pushes true.

**Crash — CONFIRMED (`0x00a4a707` → `0x00a3be05`).** Step 2 computes `character + 0x40` **before** the character is
null-checked (the `LEA` at `0x00a4a707` happens right after `0x00a28150` returns, the test only at `0x00a4a716`). When
arg 1 does not resolve — an unknown name, or a character that is already dead or despawned (`0x00a28150` rejects dead
objects) — the position pointer is **`0x40`**. If arg 2 is `"#PLAYER#"` or `"#CLOSEST_PLAYER#"`, `0x00a3bd20` measures
every player's squared distance to that "position" with `0x00da1330`, which reads **address `0x40`**. Any other
target name is harmless (the position is not touched). Fix: return false before resolving the target when the
character is null — the order the sibling `force_throw_do` already uses (it never passes a derived pointer).

---

## F. `follower_is_unconscious` → `0x00a49870` (gameplay registrar)

**Arguments:** 1 string = character (unconditional, `0x00a28150`). **Return:** exactly 1 boolean. CONFIRMED — listing.

**Body (CONFIRMED — listing):** resolved → `0x009715d0(character)`, i.e. the state field `+0xcc8` equals **6** — the
"downed" value `human_is_downed` (§7.13) reads. **Not resolved → true.**

**Return-shape note (CONFIRMED):** the two "downed" queries disagree on the unresolved case and on the resolver:
`human_is_downed` (§7.13) uses `0x00a281a0` (accepts `"#PLAYER#"`) and answers **false**; `follower_is_unconscious`
uses `0x00a28150` and answers **true**. Because `0x00a28150` rejects dead characters, a **dead** follower — and any
misspelt name — reads as unconscious. HYPOTHESIS: deliberate, so that follower scripts treat a missing follower as
"needs reviving / out of action". A reimplementation must keep `true` for the unresolved case. Safe.

---

## G. `flashpoint_start_mission` → `0x00a4a010` (gameplay registrar)

**Arguments:** 1 number = flashpoint id (unconditional, truncated). **Return:** 0 values. CONFIRMED — listing.

**Body (CONFIRMED — listing, follow-up dumps):**
1. `0x008d9910(id)` — the local start:
   - `0x006a6ff0()` sets the dwords `0x012f0228` and `0x012f022c` to 1 (meaning OPEN; `0x012f0228` also takes 2, 3, 4
     elsewhere — a small state machine);
   - byte **`0x0250ab68` = 1** ("a flashpoint mission is running", HIGH CONFIDENCE: the stop routine `0x008d9970`
     clears it and `0x0250ab6c`) and dword **`0x0250ab6c` = id**;
   - `0x008d7e90(id)` searches the world's flashpoint list (world `0x03171a64`, index array `+0x2f8`, count `+0x300`,
     objects via `+0x58`) for one whose dword `+0x320` equals the id — the **last** match wins; id **0** matches any
     flashpoint with a non-zero `+0x320`;
   - found → byte `0x0250ab69` = its byte `+0x325`; then (its `+0x320` being non-zero) bytes `+0x324` and `+0x325`
     are cleared and `0x00805240(its handle, 0, 0)` updates the matching entry of the list at `0x022bfeac` — sets bit 1
     and clears bit 2 of that entry's byte `+0x3c` (HYPOTHESIS: hides the flashpoint's map marker);
   - always tail-calls `0x008d98a0`: **every** flashpoint whose `+0x31c` is non-zero gets `0x008d9450` (this = it) and
     `+0x31c` = 10 (HYPOTHESIS: suspends the other active flashpoints, state 10).
2. Broadcasts an opcode-`0x43` record with the 8-bit sub-code **`0x2a`** and the 4-byte id to the session
   (`0x0086f1b0`), closed with `0x0086eb20`. The receive handler is `0x00a49ff0` (its only reference is the data word at
   `0x011697c8`, HIGH CONFIDENCE the opcode-`0x43` sub-handler table): it reads 4 bytes and calls the same
   `0x008d9910` — so both machines start the same flashpoint. In single player the commit does nothing (no session).

**Edges (CONFIRMED structure):** an id that matches nothing still sets `0x0250ab68`/`0x0250ab6c` and still runs the
`0x008d98a0` sweep (and is still broadcast); there is no "already running" test. No crash shape (every list walk is
bounded by its count; the found record is null-checked).

---

## H. `flashlights_enable` → `0x00a497e0` and `flashlights_disable` → `0x00a497c0` (gameplay registrar)

**Arguments:** none read. **Return:** 0 values. CONFIRMED — listings.

**Body (CONFIRMED — listings):** both call the one-line setter `0x009ba8a0(enable)`, which stores **`!enable`** in the
byte **`0x0262e690`** ("flashlights disabled"): `flashlights_enable` → 0, `flashlights_disable` → 1. (The global is
`0x0262e690`; `0x009ba8a0` is a function, not the global — WALLS.md setter-vs-global rule checked.)

Readers and writers (follow-up dumps): the only reader is `0x009c0170` at `0x009c02c8`, which skips its whole
flashlight branch when the byte is set (the branch also needs class row `+0xa` bit `0x02` on the character — the bit
tranche 11 reads as "player" — and character `+0x1d97` bit `0x10` clear; HIGH CONFIDENCE: the player-character
flashlight update); the only other writer is the
reset `0x009bbb80` (called from `0x00708330`), which clears it together with `0x0262e698`-`0x0262e69a` and two
`0x0130ae70`/`0x0130ae74` handles. So the switch is global (all characters), local (no record is sent), and returns to
"enabled" whenever `0x009bbb80` runs (HYPOTHESIS: on a world/mission reset). Safe.

---

## I. `fade_get_percent` → `0x00a49a20` (gameplay registrar)

**Arguments:** none read. **Return:** exactly 1 number. CONFIRMED — listing.

**Body (CONFIRMED — listing):** pushes `0x0059f9c0()`: **0.0 when the fade state `0x012e6aa4` is 2, otherwise 1.0**.
With §26.24's encoding (0 fading in, 1 fading out, 2 fully faded in, 3 fully faded out): **0.0 only when the screen is
fully visible; 1.0 while fading in, while fading out, and when fully faded out.**

**Return-shape note (CONFIRMED):** despite the name, the result is never a fraction — there is no intermediate value
and no ×100 scale. A script polling it during a fade-in sees 1.0 until the very end. A reimplementation that returns a
real percentage (0-100) or a progress fraction would change script behaviour. Safe.

---

## J. DLC mission helpers — 5 functions (gameplay registrar)

### J.1 `dlc3_m03_punch_load` → `0x00a47b70` and J.2 `dlc3_m03_punch_unload` → `0x00a47cd0`

**Arguments:** 1 nil-gated number = mask, default **3** (absent or nil → 3). **Return:** 0 values. CONFIRMED —
listings. The two handlers are identical except for the local routine and the payload byte.

**Body (CONFIRMED — listings, depth-1 dumps):**
- mask bit 0 → the local routine: load `0x008409b0` / unload `0x00840a50`;
- mask bit 1 → an opcode-`0x43` record, 8-bit sub-code **`0x2f`**, then one byte (**1** = load, **0** = unload), broadcast
  to the session. The receive handler `0x00a47450` (referenced only from the data word `0x011697dc` of the same table as
  G) reads the byte and runs `0x008409b0` or `0x00840a50`.

`0x008409b0` (load): `0x007b1cb0("clones_m3", 0)` — the UI-document loader (`interp_mnao.md` / `interp_jfue.md`: finds or
creates the named document, returns its id or −1; mode 0 here, mode 1 for `"screen_fade"`/`"tutorial"`) — stores the id
in **`0x013010d4`** (file value −1), sets byte **`0x0231758c`** = 1 and dword `0x02317590` = 0. The name is the pointer
at `0x013010d0` (ptrs run). `0x00840a50` (unload): id −1 → just clears `0x0231758c`; otherwise, if `0x0231758c` is set,
clears it, unloads with `0x007b1420(id)`, sets the id to −1 and `0x02317588` = 0. The per-frame user of these globals is
`0x00840a90` (xref run; not read). `0x00840a50` is also called by `0x006d9470` (HIGH CONFIDENCE: mission cleanup).

**Logic defects (CONFIRMED structure):**
- **Double load leaks a document.** `0x008409b0` overwrites `0x013010d4` without checking it, so a second load on the
  same machine — two script calls, or (HYPOTHESIS) a local call plus the peer's broadcast when both co-op machines run
  the mission script with the default mask 3 — replaces the id; the first `"clones_m3"` document can then never be
  unloaded through this path.
- The "loaded" byte is set even when the loader returned −1.

HYPOTHESIS: `"clones_m3"` is the punch-meter screen of the DLC3 mission 3 (its Lua hook `clones_m3_pow` is listed in
`spec-lua-bindings.md`). No crash shape.

### J.3 `dlc2_m02_clapboards_reset` → `0x00a47610`

**Arguments:** 1 number = count (unconditional, truncated). **Return:** 0 values. CONFIRMED — listing.

**Body:** count = arg 1, **clamped to at most 10** (no lower clamp), stored in **`0x0130cb20`** (file value **−1**); if
the count is positive, the first `count` bytes of the 10-byte flag array **`0x027043b0`** are zeroed (`memset`).
CONFIRMED.

### J.4 `dlc2_m02_clapboards_set` → `0x00a47660`

**Arguments:** 1 number = clapboard number, **1-based** (unconditional, truncated). **Return:** 0 values. CONFIRMED —
listing.

**Body:** index = arg − 1; if count > 0 and **index < count**, flag `0x027043b0[index]` = 1. It then scans the flags
`0 … count−1` for the first zero and **discards the result** (both exits return 0 values) — dead code, HYPOTHESIS: an
"all clapboards found" answer that was dropped. CONFIRMED.

**Crash / memory-safety — CONFIRMED structure (`0x00a47688`-`0x00a4768c`):** the index is compared with the count as a
**signed** value and has **no lower bound**. Any arg ≤ 0 gives a negative index and stores the byte **1** at
`0x027043b0 + index` — below the array. Arg 0 hits `0x027043af`; the most negative index (−2^31) wraps the 32-bit sum
to `0x827043b0`, so the reachable targets are every address in `0x00000000`-`0x027043af` and `0x827043b0`-`0xffffffff`
— which includes the image's whole code section and most of its data. A script-controlled one-byte write.
Only blocked while the count is ≤ 0 (before the first reset, since the file value is −1). Fix: require `1 ≤ arg ≤ count`.

### J.5 `dlc2_m02_clapboards_get` → `0x00a476c0`

**Arguments:** 1 number = clapboard number, 1-based (unconditional, truncated). **Return:** **1 boolean, or 0 values**.
CONFIRMED — listing.

**Body:** same bound as J.4 (`count > 0` and `index < count`, signed, no lower bound); in range → pushes the flag byte as a
boolean; otherwise returns **nothing** (the caller sees nil, not false).

**Crash shape — CONFIRMED structure (`0x00a476ee`):** the same missing lower bound turns arg ≤ 0 into a read of a byte
below the array; a large negative arg reads an arbitrary address and faults when it is unmapped. Fix as J.4.

**Clapboard state notes (CONFIRMED):** before the first `dlc2_m02_clapboards_reset` (count −1) both `set` and `get` do
nothing — `get` returns no value. Numbers above 10 are always out of range. `reset` with a count lower than before only
clears the first `count` flags; flags beyond it survive but are unreachable until a larger reset clears them.

---

## L. Method notes for the follow-up runs

- Follow-up dump (depth 0, `maxinsn:900`): `0x00a3bd20 0x008d7e90 0x006a6ff0 0x008d98a0 0x00805240 0x00706b00 0x00704440
  0x00704500 0x00704520 0x007c3de0 0x009bbb80 0x009c0170 0x00a49ff0 0x00a47450 0x004f4d00 0x007e1f40 0x00867830
  0x007074a0 0x006a3900 0x0084a280`. `0x009c0170` was read only around its `0x0262e690` test (`0x009c0269`-`0x009c0338`);
  `0x004f4d00`, `0x006a3900` and `0x0084a280` only to confirm the tranche 03 / §27.1 / tranche 12 readings.
- Second follow-up dump (depth 0): `0x00dfdc60` (Lua index resolver, M.1), `0x00704a50` (apply the pending play mode),
  `0x008d9970` (flashpoint stop; only its writes to `0x0250ab68`/`0x0250ab6c` are cited).
- Ptrs run: the play-mode name table `0x012f47a8`; `0x013010d0` (the `"clones_m3"` name pointer and the id slot
  `0x013010d4`, file value −1); the three SSE constants of B.7.
- Xref run: as listed in the header. `0x027043ac` and `0x027043ba` (just outside the clapboard array) have no
  instruction references, so the bytes the J.4 overflow reaches first are not named variables.
- The 25 handlers were taken from the `{name, function}` slot pairs (insn offset +1); for `0x00845aa0` the direction was
  checked against two already-specced rows (section A). None needed the pointer-before-name correction.
- Primary dumps at depth 1 (`maxfuncs:15`, `maxinsn:500`) supplied every callee body described as "depth-1 dump":
  `0x00706b90`, `0x00706ab0`, `0x007044f0`, `0x00704960`, `0x00704950`, `0x008b7100`, `0x00da74a0`, `0x006ced00`,
  `0x006a3990`, `0x006dbcc0`, `0x0045f470`, `0x00462960`, `0x0050cb90`, `0x005fe490`, `0x004f4b90`, `0x00a3be60`,
  `0x009659f0`, `0x009715d0`, `0x008d9910`, `0x009ba8a0`, `0x0059f9c0`, `0x008409b0`, `0x00840a50`, `0x00ea23b0` (the
  CRT `memset`).

---

## M. Cross-function observations

1. **What a missing argument really reads — CONFIRMED (second follow-up dump of `0x00dfdc60`).** The resolver maps a
   positive index to `base + 16·(index − 1)` and returns the shared nil object `0x01251b00` when that is at or past the
   top; an index from −9999 to 0 maps to `top + 16·index` with **no check at all**; −10000, −10001, −10002 are the
   registry, environment and globals pseudo-indices; lower values are upvalues. Combined with the count-from-bottom
   convention (arg `k` of a call with `n` arguments is read at index `k − n − 1`):
   - `k = n + 1` (the first missing argument) reads **index 0 — the slot at the top of the stack, stale content**, not
     nil;
   - `k = n + 2` reads **index 1 — the first argument again**; `k = n + 3` reads argument 2 when `n ≥ 2`, else nil.
   This settles `interp_lgdz.md`'s missing-argument HYPOTHESIS (its two cases were right; the aliasing case is new). A
   reimplementation that treats every missing argument as nil differs from the shipped engine for unconditional reads.
   Nil-gated reads (E.1 arg 3, J.1) are safe because they test the count first.
2. **Three crash classes in this tranche, each with a sibling that shows the fix:**
   - *derived pointer before the null test* — `force_throw_char_do` (E.4) computes `character + 0x40` before testing
     the character; `force_throw_do` (E.3) does not need the position and never makes the mistake;
   - *unchecked "current" pointers* — `game_cancel_mission`'s non-diversion branch dereferences the mission pointer and
     the new dialog without tests (C.2), although `0x006ced00`, called just before, already distinguishes "no mission"
     (−1) and the dialog opener is documented to fail when four dialogs are open;
   - *signed index with only an upper bound* — `dlc2_m02_clapboards_set`/`_get` (J.4/J.5); `game_get_coop_ping` and the
     play-mode getters use internal indices only and are safe.
3. **Two different "mode" systems.** The *game-state stack* (`0x01503b50`, states such as 2/5/6/8/10; B.2) and the
   *game-play mode* (`0x012f45c8`, four named modes `"Activity"`/`"Diversion"`/`"Normal"`/`"Mission"`; B.3/B.4) are
   unrelated globals with unrelated numbering. Cancelling a diversion requests play mode 2 = `"Normal"` (C.1); that 2 is
   not game state 2.
4. **The opcode-`0x43` sub-handler table.** The receive handlers found here sit in consecutive data words: sub-code
   `0x2a` (flashpoint start) → `0x00a49ff0` at `0x011697c8`, sub-code `0x2f` (punch load/unload) → `0x00a47450` at
   `0x011697dc`. The distance `0x14` = 5 dwords matches `0x2f − 0x2a` = 5, so the table is HIGH CONFIDENCE indexed by
   sub-code × 4 from `0x01169720`. A future note can look up any opcode-`0x43` sub-code's receiver there (not verified
   beyond these two entries).
5. **Fire commits, throw stages.** `force_fire_*` commit CAE action 0 at once through `0x004f4d00` without resetting the
   staged block; `force_throw_*` stage action 4 through `0x004f4b90` with force = 1, like `ai_suggest_action`
   (tranche 03). The two families therefore differ in timing (immediate versus "when the AI next commits", HYPOTHESIS)
   and in what stale staged data can leak into the committed action (E.1).
6. **Return values that are not what the name suggests (CONFIRMED):** `fade_get_percent` is a 0/1 step, never a
   percentage (I); `follower_is_unconscious` is true for a dead or unknown follower (F); `game_get_*_play_mode` return
   untranslated English identifiers and `"None"` (B.3); `game_is_going_to_main_menu` is true in every state except 5/6
   (B.2); `game_format_int_to_string` floors through a single-precision float (B.7); `dlc2_m02_clapboards_get` returns
   no value, not false, when out of range (J.5).
7. **Four more constant/no-op bindings.** `game_is_german_build`, `game_is_demo`, `game_is_debug_reloading_level`
   (constant false, `0x00a3c670`) and `game_event_tracking_interface_exit` (no-op `0x007c9f50`). Tranche 12's alias
   list already counted them (the constant-false stub answers at least 9 names); this note only adds their body entries
   and confirms the pairing.
8. **No session claims in this tranche.** `flashpoint_start_mission` and `dlc3_m03_punch_load`/`_unload` broadcast an
   opcode-`0x43` record and `game_get_coop_ping` reads the session; `spec-lua-api-behaviour.md` §8.27/§26.28 already
   establish that single player has no session object, so the broadcasts simply do nothing there and the ping is 0. No
   inference about sessions is drawn from these bodies.

---

## N. OPEN

- A: none (all 25 handlers confirmed).
- B.2: the meaning of game states 2, 5 and 6 (open the ten registrations of `0x00706a40` listed by the xref run); the
  capacity of the request queue `0x012f4a94` versus `0x00706b00`'s unbounded replay into a 16-dword buffer.
- B.3: what the four play modes' enter/leave callbacks (records at `0x012f45d8`, stride `0x74`) do; the opcode-`0x34`
  record sent by `0x00704a50`.
- B.5: what `game_event_tracking_interface_enter` (`0x008424c0`) does and whether anything else ends it.
- B.6: the meaning and units of machine record `+0xb200`.
- C.1: whether the singleton's virtual `+0x44` clears `0x014c2d10` before the unconditional writes; what `0x007b4990(-1)`
  and `0x007ff3e0` do.
- C.2: which mission types other than 0, 1, 3, 4 exist in shipped content (type 2 in particular); the dialog result
  callback `0x006dae00` (does "yes" fail the mission with the stored text, and is the pause reference released on both
  answers?).
- D: the role of the constant 0.009 passed to `0x0045c230`, and of bit `0x10` of the instance's `+0x98`.
- E.1: the meaning of character `+0x4fd` bit 2 (arg 3 of `force_fire_*`).
- E.3/E.4: what `0x005fe3e0` and `0x00b81600` test (is it "a throwable item", or any held item?).
- G: the flashpoint record fields `+0x31c`, `+0x320`, `+0x324`, `+0x325`; `0x008d9450`; the marker list `0x022bfeac` bits
  1/2; `0x012f0228`/`0x012f022c`.
- H: who calls `0x00708330` (and so when flashlights are re-enabled automatically).
- J.1/J.2: the per-frame user `0x00840a90` and the meaning of `0x02317588`/`0x02317590`.
- M.4: confirm the opcode-`0x43` sub-handler table base `0x01169720` by reading an entry with a known sub-code.

---

## O. Direct-answer table

| # | name | registrar | handler | status |
|---|---|---|---|---|
| 1 | `game_is_going_to_main_menu` | UI helper `0x00845aa0` | `0x00842750` | resolved — false only while game state 5/6 is on top and no switch to state 2 is queued (B.2) |
| 2 | `game_is_german_build` | UI helper | `0x00a3c670` | resolved — constant false (B.1) |
| 3 | `game_is_demo` | UI helper | `0x00a3c670` | resolved — constant false (B.1) |
| 4 | `game_is_debug_reloading_level` | UI helper | `0x00a3c670` | resolved — constant false (B.1) |
| 5 | `game_get_pending_game_play_mode` | UI helper | `0x00841fc0` | resolved — `"Activity"`/`"Diversion"`/`"Normal"`/`"Mission"`, or `"None"` when no change is queued (B.4) |
| 6 | `game_get_game_play_mode` | UI helper | `0x00841f70` | resolved — current play mode name; file default `"Normal"` (B.3) |
| 7 | `game_get_coop_ping` | UI helper | `0x008446e0` | resolved — remote machine `+0xb200` as unsigned number; 0 without a session (B.6) |
| 8 | `game_format_int_to_string` | UI helper | `0x00841c90` | resolved — comma-grouped integer; **floor through a float** (B.7) |
| 9 | `game_event_tracking_interface_exit` | UI helper | `0x007c9f50` | resolved — shared no-op stub (B.5) |
| 10 | `game_cancel_mission` | UI helper | `0x00842a20` | resolved — diversion cancelled at once; else confirmation dialog; **three crash paths** (C) |
| 11 | `game_audio_set_rtpc` | UI helper | `0x00843af0` | resolved — set a Wwise RTPC (name or id) on one playing instance (D) |
| 12 | `force_throw_do` | gameplay `0x00a20840` | `0x00a4a920` | resolved — stage CAE `throw grenade` at a navpoint-like target; needs an equipped item (E.3) |
| 13 | `force_throw_char_do` | gameplay | `0x00a4a6d0` | resolved — set combat target + stage `throw grenade`; **null-position crash with `"#PLAYER#"`** (E.4) |
| 14 | `force_fire_target_do` | gameplay | `0x00a4a850` | resolved — commit CAE `fire` at an object (kind `+0x6` bit `0x02`) (E.1) |
| 15 | `force_fire_do` | gameplay | `0x00a4a780` | resolved — commit CAE `fire` at a navpoint-like target (E.1) |
| 16 | `follower_is_unconscious` | gameplay | `0x00a49870` | resolved — state `+0xcc8` == 6; **true when unresolved** (F) |
| 17 | `flashpoint_start_mission` | gameplay | `0x00a4a010` | resolved — start flashpoint by id locally + opcode `0x43`/`0x2a` broadcast (G) |
| 18 | `flashlights_enable` | gameplay | `0x00a497e0` | resolved — global byte `0x0262e690` = 0 (H) |
| 19 | `flashlights_disable` | gameplay | `0x00a497c0` | resolved — global byte `0x0262e690` = 1 (H) |
| 20 | `fade_get_percent` | gameplay | `0x00a49a20` | resolved — 0.0 when fully faded in, else 1.0; never a fraction (I) |
| 21 | `dlc3_m03_punch_unload` | gameplay | `0x00a47cd0` | resolved — unload `"clones_m3"` UI document; mask bits local/broadcast (J.2) |
| 22 | `dlc3_m03_punch_load` | gameplay | `0x00a47b70` | resolved — load `"clones_m3"`; **double load leaks** (J.1) |
| 23 | `dlc2_m02_clapboards_set` | gameplay | `0x00a47660` | resolved — 1-based flag set; **no lower bound → arbitrary byte write** (J.4) |
| 24 | `dlc2_m02_clapboards_reset` | gameplay | `0x00a47610` | resolved — count ≤ 10, clear flags (J.3) |
| 25 | `dlc2_m02_clapboards_get` | gameplay | `0x00a476c0` | resolved — flag as boolean, or no value; **no lower bound → arbitrary read** (J.5) |

---

## P. Crash-shaped and logic defects (summary)

| # | where | defect | status |
|---|---|---|---|
| 1 | J.4 `0x00a47688`-`0x00a4768c` | `dlc2_m02_clapboards_set` with arg ≤ 0: signed index, no lower bound → **script-controlled one-byte write** below `0x027043b0` (any address in `0`-`0x027043af` or `0x827043b0`-`0xffffffff`) | CONFIRMED structure |
| 2 | C.2 `0x006dbcfa` | `game_cancel_mission` with nothing in progress (type −1) or in phase 6/7 → read of `[0 + 0xa0]` (mission pointer `0x014c8460` null) | CONFIRMED structure |
| 3 | C.2 `0x006dbd9f` / `0x006dbe58` / `0x006dbee6` | `game_cancel_mission` when four dialogs are already open → null dialog, write to address `0x118` | CONFIRMED structure |
| 4 | C.2 `0x006dbefb` | `game_cancel_mission` for a mission type other than 0/1/3/4 → write to address `0x118`, read of `0x12c` | CONFIRMED structure, reachability OPEN (which types exist) |
| 5 | E.4 `0x00a4a707` → `0x00da1330` | `force_throw_char_do` with an unresolved/dead character and target `"#PLAYER#"`/`"#CLOSEST_PLAYER#"` → distance from position `0x40` | CONFIRMED |
| 6 | J.5 `0x00a476ee` | `dlc2_m02_clapboards_get` with arg ≤ 0 → arbitrary byte read below the array | CONFIRMED structure |
| 7 | C.1 `0x006a39ed`-`0x006a3a10` | diversion cancel writes through `0x014c2d10` unconditionally after a call that may clear it | CONFIRMED shape, reachability OPEN |
| 8 | B.2 `0x00706b00` | play-state projection replays queued pushes into a 16-dword buffer without the real pump's limits | CONFIRMED structure, reachability OPEN |
| 9 | M.1 `0x00dfdc60` | missing arguments read the stale top slot (first missing) or alias earlier arguments (later ones), not nil | CONFIRMED |
| 10 | J.1 | `dlc3_m03_punch_load` twice → first UI document's id overwritten, never unloaded | CONFIRMED structure |
| 11 | C.2 | each `game_cancel_mission` call takes another game-pause reference and opens another dialog | CONFIRMED structure, release path OPEN |
| 12 | E.1 | `force_fire_*` commit without resetting the staged block (stale staged fields leak) and without a busy test | CONFIRMED structure, consequence HYPOTHESIS |
| 13 | B.7 | `game_format_int_to_string` rounds through a float (wrong digits above 2^24), floors negatives, prints out-of-range as "-2,147,483,648" | CONFIRMED |
| 14 | F | `follower_is_unconscious` true for a dead or unknown follower (opposite of `human_is_downed`) | CONFIRMED |
| 15 | I | `fade_get_percent` returns only 0.0 or 1.0 | CONFIRMED |
| 16 | G | `flashpoint_start_mission` with an unknown id still marks a flashpoint as running and suspends every active flashpoint | CONFIRMED structure |
| 17 | J.5 | `dlc2_m02_clapboards_get` returns no value (nil) out of range and before the first reset | CONFIRMED |
| 18 | B.1 / B.5 | four names are constant-false / no-op stubs | CONFIRMED |

---

## Q. Clean-room check

- Addresses are plain hex throughout; no Ghidra auto-names for functions, globals or labels, no decompiler variable
  names, and no pasted pseudocode. Library routines are named only where Ghidra identified them as standard C runtime
  (`sprintf`, `strncpy`, `strncmp`, `memset`) or as the public Lua 5.1 API by established project convention
  (`lua_gettop`, `lua_tolstring`, `lua_type`, …), plus the public Wwise entry point already named in §2; short game strings
  (localisation tags, play-mode names, a UI document name, special name tokens) are quoted as data.
- Self-check run on the finished file, as the last step before reporting, with the pattern
  `\b(iVar[0-9]+|uVar[0-9]+|piVar[0-9]+|puVar[0-9]+|pvVar[0-9]+|fVar[0-9]+|sVar[0-9]+|cVar[0-9]+|bVar[0-9]+|pcVar[0-9]+|pbVar[0-9]+|local_[0-9a-fA-F]+|uStack_?[0-9a-fA-F]+|auStack_?[0-9a-fA-F]+|unaff_[A-Za-z0-9]+|extraout_[A-Za-z0-9]+|undefined[0-9]+|param_[0-9]+|in_[A-Z]{2,4}|LAB_[0-9a-fA-F]+|FUN_[0-9a-fA-F]+|DAT_[0-9a-fA-F]+)\b`
  (`grep -cP`, and the same pattern through Python `re` line by line): **0 hits**. The pattern was first checked against
  16 controls: 12 positives (auto-named locals and parameters, a register input, a stack array, an `unaff_` register, a
  type name, and the `LAB_`/`FUN_`/`DAT_` label forms), all matched, and 4 negatives ("left undefined", "in ECX", a bare
  `0x…` address, ordinary prose), none matched. The run was repeated after this section was added, with the same result.
- No spec file was edited. The private Ghidra copy `tools\gp_t22` was deleted after the dumps; raw dumps stayed in the
  session scratchpad.
