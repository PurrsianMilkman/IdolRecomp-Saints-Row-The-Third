# Ranking tranche 25 — the last 4 previously-unspecced Lua-bound names (Team A, 2026-10-03)

Job definition: `D:\Project Crreish\TEAM A\ghidra\jobs\ranking\tranche-25.json`. The job file's own title states **names
551-554 of the 554 unspecced names, Team B call-count order**; it lists exactly **four** names, not 25, and this note
uses that range and exactly those four names. This tranche closes the 01-25 ranking backlog (554 names). Run locally,
read-only, on a private copy of the Ghidra project (`tools\gp_t25`, deleted afterwards), with the job's own arguments:
`CrreishDump.java <out>/lua lua depth:1 maxfuncs:15 maxinsn:500 <4 names>`. **All 4 names resolved.** Every name string
occurs exactly once in `.rdata`, and for all four the handler is the code pointer stored in the slot right after the
name (insn offset +1) — CONFIRMED — dump `index.txt`, plus the range run of both registrars (section A). For
`Screen_fade_transition_complete` the `lua` mode also offered `0x00dfe830` (`lua_setfield`) as a second candidate; the
range run shows that this is the registrar's loop, not a pointer-before-name registrar (A).

Follow-up runs on the same private copy, cited by name below:

- "follow-up dump": depth-0 `func` run (`maxinsn:900`) on 16 callees (listed in section L);
- "range run": `range` mode on the two registrar bodies `0x005a01d0`-`0x005a0268` and `0x00a20840`-`0x00a209b0`.

Call counts are Team B's (`D:\Crreish-sync\for-team-b\team-b\tools\lua_reconciliation_called_and_registered_1181.tsv`,
rows 1179-1182): **every name in this tranche is 1 call site in 1 script.** Team B tags `Screen_fade_transition_complete`
`ui` and the other three `gameplay`; section A shows the registrars agree.

**Already partly covered elsewhere (cited, re-verified, not re-derived):**
- `Screen_fade_transition_complete` already has a CONFIRMED body description inside `spec-lua-api-behaviour.md` §26.24
  (the screen-fade state machine), but no entry of its own under its name; this note re-reads its full listing, agrees
  with §26.24 on every step, and adds one re-entrancy observation (E).
- `airplane_fly_to_do` shares its argument shape, target resolution and point builder `0x00a3b860` with
  `vehicle_pathfind_to_do` (tranche 03 T3.5.10); the follow-up dump of that sibling (`0x00a680c0`) is used for a
  side-by-side comparison (B.3).
- `add_object_indicator_to_closest_npc` reaches the object-indicator adder `0x008f96f0` that §10.6
  (`object_indicator_add_do`) already reaches, and the group resolver `0x005eab60` and member walk of §18.14
  (`group_members_alive`).
- `action_play_custom_do` uses `0x00957c80` (blend out the current animation layers over 0.2) and the player
  "script mode" setter `0x009e3200` exactly as tranche 05 C.2 and tranche 14 B.2 describe; both are cited.

Labels: **CONFIRMED** = read in the listing of a dump made for this note; **HIGH CONFIDENCE** = follows from a dumped call
or reference, but the callee body was not dumped or a meaning is inferred from strong usage; **HYPOTHESIS** = plausible,
not settled; **OPEN** = not settled (collected in section N).

**Shared conventions, cited not re-described:** every entry opens with the `lua_gettop` prologue (`0x00dfde50`) and
reads arguments with negative stack indices counted from the bottom (`-N` = arg 1, `1-N` = arg 2, …). Primitives as in the
tranche 09/13/16/18/20/22 front matter: `0x00dfe210` `lua_tolstring` (non-string, non-number reads as null),
`0x00dfe160` `lua_tonumber` (non-number reads as 0), `0x00dfe040` `lua_type` (0 = nil), `0x00ea2596` truncating
float-to-int, `0x00dfe590` `lua_pushboolean`, `0x00dfe3a0` `lua_pushnumber`, `0x00dfe420` pushes a C string (null pushes
nil), `0x00dfe4f0` `lua_pushcclosure`, `0x00dfe830` `lua_setfield`. "Nil-gated optional argument" = the tranche 13 idiom
(the count is tested before the slot is read); an **unconditional** read has no presence check, and under this
project's count-from-bottom convention a missing argument is **not** nil: the first missing argument reads the stale
slot at the top of the stack, a later one can re-read an earlier real argument (tranche 22 M.1, from a dump of the
index resolver `0x00dfdc60`). Resolvers: generic character resolvers `0x00a281a0` (full chain, accepts `#PLAYER#`) and
`0x00a28150` (`#FOLLOWER#` + plain, rejects dead); vehicle resolver `0x00a281e0`; the per-kind named-object lookups on the
world object `0x02442750` (`0x005982e0` kind row `+0xb` bit `0x02`, the navpoint-like "5th resolver"; `0x0062a1f0` kind
row `+0x8` bit `0x02`, the path object of tranche 03 T3.5.10; `0x005eab60` kind row `+0xa` bit `0x20`, the script-group
resolver of §6.6/§18.14); liveness `0x00853b10` (true = dead/unusable); "is dead" `0x0096f4f0` (§3.5: null, death state
`+0xcc8` = 5, or `+0xe3` bit `0x01`). Single gate `0x008addb0(object, 0)` = "this machine has authority over the object".
The standard record trio (`0x0086f5f0(opcode)` open, `0x00881110`/`0x00881040` write, `0x0086f1b0` broadcast commit —
no-op without a session — and `0x0086f110` single-target commit, `0x0086eb20` close). The script request-id allocator
`0x006f62b0(object, kind, callback)`, its cancel `0x006f6680` and its supersede rule (a new request for the same object
and kind releases the old record) are `interp_teleport_coop.md`'s "Allocate"/"Cancel" bullets. Name hash `0x00d9e8b0`
and PRNG `0x00dab660` as in the shared list.

---

## A. Registrars — where the 4 names live

Two registrars. CONFIRMED — `index.txt` and the range run:

| registrar | names in this tranche |
|---|---|
| gameplay `0x00a20840` | 3: `airplane_fly_to_do`, `add_object_indicator_to_closest_npc`, `action_play_custom_do` |
| screen-fade UI natives `0x005a01d0` (§26.24) | 1: `Screen_fade_transition_complete` |

- **Pairing direction in `0x00a20840`.** The range run shows the table built on the stack as `{name, function}` pairs
  starting with a name at frame `+0xc` (`"action_nodes_enable"` → `0x00a3c020`). The neighbours confirm it against
  already-specced rows: `"action_play_directional_stumble_do"` → `0x00a3f7d0` (tranche 05 C.2) follows
  `action_play_custom_do` → `0x00a3f630`; `"action_sequence_end"` → `0x00a3c270` (tranche 05 C.3) precedes
  `airplane_fly_to_do` → `0x00a402a0`; `"action_play_is_finished"` → `0x00a3dcc0` (§7.21) follows
  `add_object_indicator_to_closest_npc` → `0x00a400f0`. Name-then-function. CONFIRMED. (The next row after
  `airplane_fly_to_do` is `"airplane_land_do"` → `0x00a404d0`; not part of this tranche.)
- **`0x005a01d0`** (range run, full body): if the Lua state argument is null it returns at once; otherwise it builds four
  `{name, function}` pairs on the stack — `"Screen_fade_transition_complete"` → `0x005a0110`, `"sfx_faded_out"` →
  `0x0059fb30`, `"sfx_faded_in"` → `0x0059fb60`, `"sfx_use_load_images"` → `0x0059fb90` — and loops four times:
  `lua_pushcclosure(L, function, 0)` then `lua_setfield(L, -10002, name)`, i.e. each native becomes a **global** of that
  Lua state. Name-then-function; the `lua_setfield` candidate in `index.txt` is the loop's own call, not a handler.
  §26.24 already identifies the state as the UI Lua state. CONFIRMED.
- No pointer-before-name registrar is involved in this tranche.

---

## B. `airplane_fly_to_do` → `0x00a402a0` (gameplay registrar)

**Arguments:** 1 string = vehicle (unconditional, `0x00a281e0`); 2 number = speed (unconditional, stored as a
single-precision float, no range check); 3 string = target name (unconditional). **Return:** exactly **1 boolean** on
every path. CONFIRMED — listing.

### B.1 Body (CONFIRMED — listing, depth-1 dump, follow-up dump)

1. vehicle = `0x00a281e0(arg 1)`; driver = `0x00ab5070(vehicle, 0)` (the seat-0 occupant, tranche 03; null-safe).
   **False** unless the vehicle and a driver exist **and** `0x00ad3160(vehicle)` is true — the dword `+0x2c` of the
   vehicle's sub-record `+0xbf4` equals **2** (HYPOTHESIS: "this vehicle is an airplane").
2. The length of arg 3 is measured with an inlined `strlen` (B.4 crash 1). **Empty string → no target object →** the
   point builder (step 4) gets a null path, returns 0 → **false**.
3. Non-empty: the name is looked up first as a **path** (`0x0062a1f0`, must be alive), else as a **navpoint-like object**
   (`0x005982e0`, must be alive). Neither → `0x0059f8a0` (pushes the byte the code has just zeroed) → **false**.
4. **Path:** `0x00a3b860(…, points, vehicle x/y/z, path, skip 0, include-start 1, reverse 0)` (depth-1 dump): point 0 is
   the vehicle's own position, then the path's nodes in order (`0x00930e90(i)` returns node `i` of the `+0x33c`-count
   array of 12-byte points at path `+0x3c`, or null past the end), **capped at 25 points in total** (the 400-byte
   buffer of the binding holds exactly 25 16-byte points, so the cap is exact — no overflow). A count ≤ 0 → false.
   **Navpoint:** point 0 = the target's position (`0x00da38e0` copies three floats from object `+0x40`), **count 1**.
5. id = `0x006f62b0(vehicle, 2, 0)` — a kind-2 script request (superseding any older kind-2 request on this vehicle).
6. `0x00b67ab0(vehicle, points, count, speed, id)`; its boolean result is pushed.

### B.2 The flight request `0x00b67ab0` (CONFIRMED — depth-1 dump, follow-up dump)

- **Rejects** unless `2 ≤ count ≤ 25` and `0x00ad3160(vehicle)`; returns false.
- **No authority** (`0x008addb0(vehicle, 0)` false): opcode-`0x42` record — two 8-bit values `0x15` and `0`
  (`0x00898cb0`; HYPOTHESIS: sub-code 0x15), the vehicle's network handle (`0x0050f790`), the count byte, points
  **1 … count−1** (point 0, the start, is not sent; `0x004d6a80` per point), the 4-byte speed and the 2-byte id —
  committed **single-target** to the vehicle's owner (`0x008ae020` → `0x0086f110`). Returns **true** at once.
- **Authority:** if the flight state `+0x320` is already **3**, `0x00b672a0` stops the current flight (clears `+0xf8`
  bit 0 and `+0xf9`, resets two sub-objects at `+0x11a0`/`+0xc68`, and if `+0xf8` bit 2 is set cancels the stored
  request id `+0x30c` through `0x006f6680` and clears it). Then waypoint count byte `+0x19c` = count − 1, index byte
  `+0x19d` = 0, points 1 … count−1 copied as 12-byte triples into `+0x1a0` (at most 24 × 12 bytes), a clock stamp at
  `+0x31c` (`0x00d9e140(0)`), the current position copied to `+0x310`-`+0x318`. `0x00b67810` then plans the route
  (needs a waypoint left; builds a route through the next one or two waypoints with `0x00b446f0`, picks the route node
  nearest the vehicle, hands it to `0x00b67530`; HIGH CONFIDENCE "plan the first flight leg"):
  - success → `+0x308` = speed; a non-zero id is stored in `+0x30c` with `+0xf8` bit 2 set; `0x00b66e90(vehicle, 3, 0)`
    switches the flight state machine to **state 3** (leave callback of the old state from the table `0x01187ba0`,
    enter callback of state 3 from `0x01187ba8`, stride 5 dwords) → **true**;
  - failure → `0x006f6680(id)` cancels the request → **false**.

So `airplane_fly_to_do` is "fly this crewed airplane along a named path (or, by intent, to a named point) at this
speed"; `true` means the flight was set up locally or forwarded to the owner, not that it arrived. HIGH CONFIDENCE: the
`_do` completion is reported through the kind-2 request id, as for the other vehicle "`_do`" requests (completion path
OPEN).

### B.3 Logic defect — the navpoint form can never fly (CONFIRMED structure)

The navpoint branch (`0x00a40483`-`0x00a404a3`) stores **one** point (the target) and sets the count to **1**;
`0x00b67ab0` rejects every count below 2 (`LEA EAX,[EBX-2]` / `CMP EAX,0x17` / `JA`, unsigned). So
`airplane_fly_to_do(plane, speed, "<navpoint>")` **always returns false and the plane does not move.** The sibling
`vehicle_pathfind_to_do` (follow-up dump of `0x00a680c0`, same shape and callees) builds **two** points in the same
branch — the vehicle's own position, then the target — and sets the count to 2 (`0x00a6826f`-`0x00a682a1`), which is
almost certainly what was intended here. Only the path form works.

Side effects of the dead branch: the request id of step 5 is allocated **before** the rejection and is never completed
or cancelled (the sibling releases it with `0x006f6360(vehicle, 2)` when its request fails; this binding has no such
release on any failure path). Because allocation supersedes, the call also **releases the record of any kind-2 request
already pending on that vehicle** — e.g. a path flight started earlier — while that flight keeps flying with an id whose
record is gone. Consequence for a script that waits on the request: HYPOTHESIS, it never sees completion (the waiting
mechanism is OPEN).

### B.4 Crash shapes and edges

- **Crash 1 — non-string target, CONFIRMED (`0x00a40349`-`0x00a40355`, test only at `0x00a4035b`).** The `strlen` loop
  reads arg 3 **before** the null test, exactly as in `vehicle_pathfind_to_do` (tranche 03). When arg 3 is nil, a
  boolean or a table, `lua_tolstring` returns null and the loop reads address 0 — reached whenever arg 1 names a living
  crewed airplane (step 1 passes). Absent arg 3 reads the stale top slot (front matter), so a two-argument call crashes
  whenever that slot is not a string or number. The later null test is dead code. Fix: test for null first (treat as
  "no target").
- **Missing arguments.** All three reads are unconditional. With a single argument, arg 2 reads the stale top slot and
  arg 3 re-reads arg 1 (the vehicle's own name), which normally resolves as neither path nor navpoint → false.
- **Silent truncation.** A path longer than 24 nodes is cut to its first 24 (plus the start point) without notice.
- `0x00ad3160` dereferences the vehicle's `+0xbf4` sub-record without a test (whether every vehicle has one is OPEN).
- No session claim is drawn: the forward-to-owner branch is ordinary single-gate behaviour.

---

## C. `add_object_indicator_to_closest_npc` → `0x00a400f0` (gameplay registrar)

**Arguments:** 1 string = reference character (unconditional, `0x00a28150`); 2 string = script group (unconditional,
`0x005eab60`); 3 number = indicator type (unconditional, truncated); 4 number = indicator value (unconditional,
truncated; HYPOTHESIS: a colour/style word — `0x008f9280` stores it at record `+0x30` and passes its bit 0 on); 5
**nil-gated** number = flags, **default 3** (read only with five or more arguments and a non-nil fifth). **Return:**
exactly **1 string**: the chosen member's name, or **`""` (the empty string, `0x0129a0e3`, first byte 0)** when nothing
was chosen — never nil, never a boolean. CONFIRMED — listing.

### C.1 Body (CONFIRMED — listing, depth-1 dump)

1. reference = `0x00a28150(arg 1)` (may be null — not an error).
2. group = `0x005eab60(arg 2)`; a null name, an unknown group or a dead/unusable group (`0x00853b10`) → return `""`.
3. Walk the group's circular member list (first member at group `+0x40`, next pointer at member `+0x9c`, stop when the
   walk returns to the first member or reaches null — the walk of §18.14). For each member, its virtual `+0x70` gives
   the character; members whose character is null or dead (`0x0096f4f0`) are skipped.
   - **Reference unresolved:** the **first** eligible member is taken at once (no distance test).
   - **Reference resolved:** the squared distance `0x00d9fc80(reference + 0x40, character + 0x40)` is computed and the
     member with the **strictly smallest** value is kept (start value `FLT_MAX`, `0x012a2ea0`; ties keep the earlier
     member).
4. Nothing chosen → `""`. Otherwise `0x008f96f0(character handle (+0x8/+0xc), arg 3, arg 4, flags, 100.0)` (the float
   `0x0111645c`, the §10.6 default) and the member's name pointer (member `+0x18`) is pushed as a string (null would push
   nil).

### C.2 The indicator adder `0x008f96f0` (CONFIRMED — depth-1 dump; follow-up dump of `0x008f9280`)

A 16-bit indicator id is taken from the counter `0x01308570` (post-increment). **Flags bit 0** → local add through
`0x008f9280(handle, type, value, 100.0)`: it sets up the `"object_indicator"` UI document once (C.3), consults the
local player (`0x009da4e0` → `0x00545120`) and the object (`0x008f4ef0`, `0x008f8810`), allocates an indicator record
(types 7 and 8 are handled specially: type 8 creates four UI elements and is downgraded to 0 when a type-8 indicator
already exists (`0x025f38c0`); type 7 gets an extra step `0x008f7ec0` with a local value raised by 0.8 — HIGH CONFIDENCE
from the listing, meaning of the types OPEN) and stores handle, type, value and the float; a failed local add
returns at once with nothing broadcast. On success the record gets the id (`+0x24`) and the flags (`+0x2c`). **Flags
bit 1** → an opcode-`0x40` record (8-bit sub-code 2, the object's network id or raw handle, type, id, value, float)
broadcast with `0x0086f1b0` and closed. **The binding discards the returned id**, and does not call `0x008f50d0` as §10.6
does, so — unlike `object_indicator_add_do` — it gives the script no success flag; HYPOTHESIS: the returned member name
is what a script later passes to an object-indicator remove call.

### C.3 Crash shape in the shared UI set-up (CONFIRMED structure, reachability OPEN)

Both `0x008f96f0` (`0x008f994d`-`0x008f9957`) and `0x008f9280` run the same one-time set-up while byte `0x025f38d8` is
clear: find the UI document (`0x007b14f0(id 0x0130856c)`), and if the stored element handle `0x025f38d4` is not valid
(`0x00e251a0`: low 16 bits < 32, matching serial and a live byte in the 32-entry table `0x02a5afc0`) create one with
`0x00e25040("object_indicator")` and read **`+0x14` of its result without a null test**. `0x00e25040` (follow-up dump)
takes its record from a free list (`0x02a5af94`) and returns **null when the free list is empty** — and, in that case,
already reads address 8 itself if its second list (`0x02a5afa4`) is non-empty. So the first indicator added while the
32-record pool is exhausted faults. The byte `0x025f38d8` is cleared again by `0x008f4fd0` and `0x008f7f60` (not read),
so the set-up can run more than once. This sits in shared code also reached by §10.6.

### C.4 Logic notes (CONFIRMED structure)

- "Closest" is **horizontal**: `0x00d9fc80` uses only the first and third coordinates (x and z); height is ignored, so a
  member on another floor directly above or below counts as closest.
- The reference character is **not excluded**: if it belongs to the group it is at distance 0 and is chosen itself.
- An unresolved, dead or misspelt reference character silently changes the meaning to "the first living member".
- Missing arguments: args 1-4 are unconditional (a three-argument call reads the stale top slot as arg 4); arg 5 is
  properly nil-gated.
- No session claim: the broadcast takes its no-session path in single player, per §8.27/§26.28.

---

## D. `action_play_custom_do` → `0x00a3f630` (gameplay registrar)

**Arguments:** 1 string = character (unconditional, `0x00a281a0`); 2 string = animation name (unconditional).
**Return:** exactly **1 number: −1, 0 or 1.** CONFIRMED — listing.

### D.1 Body (CONFIRMED — listing, depth-1 dump, follow-up dump)

1. **Name:** arg 2 goes through `0x00da86f0(buffer, 64, arg 2)` — copy with the **last extension removed** (a `.` with no
   `/` or `\` after it; truncated to 63 characters) — and then `0x00da8790(buffer2, 64, buffer, ".anim")`, which strips an
   extension **again** and appends `".anim"` if the result fits in 63 characters (otherwise it returns with the stripped
   name and no extension).
2. character = `0x00a281a0(arg 1)`. If the character is non-null and dead (`0x0096f4f0`) → push **−1**.
3. Otherwise `0x00957c80(character, 0.2, 0, 0, 0)` — blends out the character's current animation layers over 0.2
   (tranche 05 C.2; null-safe; with both flags 0 no opcode-`0x41` record is sent).
4. anim = `0x004bec80(buffer2)`: name hash (`0x00d9e8b0`) looked up by `0x004bec10` in a 15,000-slot open-addressing table
   (`0x030d37a0`, probing linearly), returning the animation index or **−1** when unknown (follow-up dump).
5. `0x004b1ef0(character[+0xd24][+0x10], anim, 0x1d4c, 0x200040, 0.0, 1.0, −1.0)` plays it (depth-1 dump): returns −1 at
   once when the animation player's `+0x20` is −1; otherwise claims a slot in the player's slot array (`+0x4c`, count
   `+0x48`, stride `0x84`) — the first inactive slot, or one already playing the same index — growing the array by one
   with `0x004b11a0` when none is free; marks it active, stores index, `−1`, the value `0x1d4c` (7500; HYPOTHESIS:
   priority/slot id); for an index inside the table (`0 ≤ anim < [0x03171be8]`) it applies the animation record's flags
   from the `0x44`-byte records at `0x02ef2178` (optional random start delay through the PRNG, blend options) and starts
   playback with `0x004bce30`, whose handle is stored at slot `+4`; flag `0x200000` copies slot `+8`/`+4` to the player's
   `+0x60`/`+0x64`. It returns slot `+4`.
6. Return value ≠ −1 → result **1**; and if the character's class row `+0xa` bit `0x02` (the "player-like" bit of §7.21)
   is set, the cutscene state `0x0153b520` is not 10-13 (`0x007205d0`), and character `+0x1d99` bit `0x08` is clear →
   `0x009e3200(character, 2, 0)` sets player script mode **2** (`+0x2888`, `+0x28ac` bit 1; locally with authority, else
   an opcode-`0x46` `"player"`/`"script_mode_params"` record to the owner — tranche 05 C.2, tranche 14 B.2). Return value
   −1 → result **0**.
7. The result is pushed as a number.

So `action_play_custom_do` interrupts whatever the character is animating and starts the named `.anim` on it, returning
immediately (it does not wait, and takes no request id). `action_stop_custom` (§21.28) and `action_play_is_finished`
(§7.21) clear the player's script mode again with `0x009e3200(…, 0, 0)`. Note that §7.21 polls action-slot id `0x1da1`,
while this binding plays with `0x1d4c`; whether `action_play_is_finished` sees custom animations is OPEN.

### D.2 Crash — unresolved character, CONFIRMED (`0x00a3f6a9`-`0x00a3f6ab` → `0x00a3f706`)

The dead test is skipped when the character is **null** (`TEST ESI,ESI` / `JZ` jumps to the play path, not to the exit).
`0x00957c80` tolerates null, but the binding then reads **`[0 + 0xd24]`** to find the animation player → access
violation. Trigger: any arg 1 the resolver rejects — a misspelt or despawned name, a character the chain does not accept,
nil or a non-string. Same family as tranche 05 C.2's `action_play_directional_stumble_do`, whose unresolved character
also reaches null reads. Fix: return −1 (or false) when the character is null. Also OPEN: whether a living character's
`+0xd24` can be null (`0x00957c80` only walks it when object `+0x33` bit `0x08` is set; the binding reads it
unconditionally).

### D.3 Logic defects (CONFIRMED structure)

- **Unknown animation reads as success.** An unknown name gives index −1. `0x004b1ef0` does not reject it: it claims a
  slot (marked active, index −1), skips the start-up, and returns the slot's `+4` **left over from earlier use**. A slot
  freshly added by `0x004b11a0` is zero-filled (only `+0` and `+0x6c` are set to −1), so it returns 0 — not −1 — and the
  binding reports **1** and (for players) sets script mode 2 although nothing plays. HIGH CONFIDENCE for the fresh-slot
  value; a reused slot returns whatever handle it held. The −1 slot stays marked active until something clears it.
- **Non-string arg 2 builds the name from uninitialised stack.** `0x00da86f0` writes nothing when its source is null, so
  the 64-byte buffer is left as it was; `0x00da8790` then searches it for a `.` and copies up to 63 bytes from it. No
  write overflow (both copies are bounded and terminated), but the hashed name is stale garbage (consequence HYPOTHESIS:
  usually an unknown name, i.e. the previous bullet). A one-argument call reads arg 2 from the stale top slot.
- **Double extension stripping:** `"a.b.c"` becomes `"a.anim"`; any extension the script supplies is replaced.
- **Return shape:** the failure value **0 is truthy in Lua**; only −1 vs 1 vs 0 distinguish the cases. A dead character
  returns −1 without touching anything.

---

## E. `Screen_fade_transition_complete` → `0x005a0110` (screen-fade UI natives registrar)

**Arguments:** none read (the prologue's count is discarded). **Return:** 0 values. CONFIRMED — listing.

**Body (CONFIRMED — listing; agrees with §26.24 step for step, so only summarised):** the fade state `0x012e6aa4`
flips **0 → 2** (fade-in finished) or **1 → 3** (fade-out finished; then the "show the loading logo at" stamp
`0x012e6aac` = now + 1000 ms via `0x00d9e3c0`, which adds to the clock `0x01320d9c` and wraps at 1,800,000,000, and the
stamps `0x012e6ab0`/`0x012e6ab4`/`0x012e6ab8` = −1); states 2 and 3 are left alone. The in-flight callback `0x013effcc`,
if set, is called with the **target** `0x012e6aa8`. If the state now equals the target and the deferred slot
`0x013effd0` is set: same function as the (re-read) in-flight slot → both cleared, return; otherwise the deferred
function is called with the target and its slot cleared. Finally the in-flight slot is cleared (both, when equal). The
native never starts a transition; parked requests are replayed by the per-frame routine `0x0059fe70` (§26.24).

**New observation — re-entrant request loses its callback (CONFIRMED structure, reachability OPEN).** The final store
`0x005a01bd` clears the in-flight slot **unconditionally**, after the callback ran. If the in-flight (or deferred)
callback itself requests the opposite fade — e.g. a fade-out completion handler that asks for a fade-in — the request
helper (`0x0059fc40`, §26.24) starts the transition and stores **its** callback in `0x013effcc`; the native then
overwrites it with 0, so the new transition completes later with no callback. (A same-direction request inside the
callback is answered immediately by the helper and is not affected.) Which C callbacks request fades is OPEN (§26.10's
quit path passes one).

**Other notes (CONFIRMED):** it is a **global** of the UI Lua state (A), so any UI script can call it, and an early call
ends the current fade at once; a call while no transition runs only re-calls a non-null in-flight callback (normally
none) and changes nothing else. No crash shape (the two callback pointers are tested before each call).

---

## L. Method notes for the follow-up runs

- Follow-up dump (depth 0, `maxinsn:900`): `0x00da38e0 0x0059f8a0 0x00930e90 0x00b67810 0x00b66e90 0x00b672a0 0x00e25040
  0x00e251a0 0x007b14f0 0x008f9280 0x004bec10 0x004b11a0 0x00a404d0 0x00a680c0 0x00898cb0 0x008ae020`. `0x00b67810`,
  `0x008f9280` and `0x00e25040` were read for their control flow only (described at that level above); `0x00a404d0`
  (`airplane_land_do`), `0x00898cb0` and `0x008ae020` were dumped but are cited only by role.
- Range run: `0x005a01d0`-`0x005a0268` (whole registrar) and `0x00a20840`-`0x00a209b0` (the head of the gameplay
  registrar, 21 pairs).
- Primary dumps at depth 1 (`maxfuncs:15`, `maxinsn:500`) supplied every callee body described as "depth-1 dump":
  `0x00a281e0`, `0x00ab5070`, `0x00ad3160`, `0x0062a1f0`, `0x005982e0`, `0x00a3b860`, `0x006f62b0`, `0x00b67ab0`,
  `0x00a28150`, `0x005eab60`, `0x0096f4f0`, `0x00d9fc80`, `0x008f96f0`, `0x00da86f0`, `0x00da8790`, `0x00a281a0`,
  `0x00957c80`, `0x004bec80`, `0x004b1ef0`, `0x007205d0`, `0x009e3200`, `0x00d9e3c0`, and the Lua primitives.
- The four handlers were taken from the `{name, function}` slot pairs (insn offset +1), with the direction checked
  against already-specced neighbours (section A). None needed the pointer-before-name correction.

---

## M. Cross-function observations

1. **Two more "derived use before the null test" crashes, both with a sibling that does it right or has the same bug.**
   `action_play_custom_do` (D.2) reads through an unresolved character because its dead test skips the null case — the
   same family as `action_play_directional_stumble_do` (tranche 05 C.2) in the same registrar row block;
   `airplane_fly_to_do` (B.4) measures its target string before testing it — a copy of `vehicle_pathfind_to_do`'s
   (tranche 03) identical bug. These three neighbouring handlers look written from one template.
2. **Copy-and-modify drift.** `airplane_fly_to_do` and `vehicle_pathfind_to_do` share the resolution and point-building
   code, but the airplane copy dropped the start point in the navpoint branch (making it unusable, B.3), dropped the
   request-id release on failure, and changed the path-branch test from `count > 1` to `count > 0` (harmless only
   because `0x00b67ab0` re-checks).
3. **Kind-2 request ids are shared across vehicle movement bindings.** `airplane_fly_to_do`, `vehicle_pathfind_to_do`
   (tranche 03) and the drive-to bindings of tranches 14/20 all allocate `0x006f62b0(vehicle, 2, 0)`; by the supersede
   rule any of them releases the pending request of any other on the same vehicle.
4. **Returns that are not what the name suggests (CONFIRMED):** `add_object_indicator_to_closest_npc` returns a **name
   string** (`""` on failure), not a boolean or an id (C); `action_play_custom_do` returns **−1/0/1**, with 0 truthy, and
   reports 1 for unknown animations (D.3); `airplane_fly_to_do` returns false for every navpoint target (B.3).
5. **Missing-argument exposure in this tranche:** unconditional reads — `airplane_fly_to_do` args 1-3,
   `add_object_indicator_to_closest_npc` args 1-4, `action_play_custom_do` args 1-2. The only nil-gated read is
   `add_object_indicator_to_closest_npc` arg 5. In two cases the stale read feeds a crash (B.4 crash 1 when arg 3 is
   absent, D.2 when arg 1 is absent).
6. **No session claims in this tranche.** `add_object_indicator_to_closest_npc` broadcasts an opcode-`0x40` record that
   takes its no-session path in single player, per §8.27/§26.28; `airplane_fly_to_do` and `action_play_custom_do`
   forward to an owner only through the single authority gate. No inference about sessions is drawn from these bodies.
7. **Backlog closed.** With these four, all 554 previously-unspecced names of the 01-25 ranking backlog have an
   interpretation note.

---

## N. OPEN

- A: none (all 4 handlers confirmed).
- B: the meaning of `0x00ad3160`'s vehicle-type value 2 and whether every vehicle has the `+0xbf4` sub-record; the
  opcode-`0x42` receiver; how the kind-2 request is completed on arrival (and how a script waits on it); `0x00b446f0`'s
  route builder.
- C: the meaning of indicator types (arg 3; special cases 7 and 8) and of the value word (arg 4); the opcode-`0x40`
  receiver; when `0x008f4fd0`/`0x008f7f60` clear the set-up latch `0x025f38d8`; whether the 32-record pool behind
  `0x00e25040` can be exhausted in practice.
- D: whether `+0xd24` can be null for a living character; the meaning of slot value `0x1d4c` versus §7.21's `0x1da1`
  (does `action_play_is_finished` work for custom animations?); character `+0x1d99` bit `0x08`; when an active slot with
  index −1 is released.
- E: which C callbacks passed to the fade helpers request another fade (reachability of the lost-callback case).

---

## O. Direct-answer table

| # | name | registrar | handler | status |
|---|---|---|---|---|
| 551 | `airplane_fly_to_do` | gameplay `0x00a20840` | `0x00a402a0` | resolved — fly a crewed airplane along a named path at a speed (max 25 points); **navpoint form always false (B.3)**; **null-target crash (B.4)** |
| 552 | `add_object_indicator_to_closest_npc` | gameplay | `0x00a400f0` | resolved — HUD object indicator on the horizontally closest living member of a script group; returns its name or `""` (C) |
| 553 | `action_play_custom_do` | gameplay | `0x00a3f630` | resolved — blend out, play `<name>.anim`, number −1/0/1; **null-character crash (D.2)**; unknown animation reads as 1 (D.3) |
| 554 | `Screen_fade_transition_complete` | screen-fade UI natives `0x005a01d0` | `0x005a0110` | resolved — completes the running screen fade (§26.24, re-verified); re-entrant request loses its callback (E) |

---

## P. Crash-shaped and logic defects (summary)

| # | where | defect | status |
|---|---|---|---|
| 1 | D.2 `0x00a3f6a9` → `0x00a3f706` | `action_play_custom_do` with an unresolved character (misspelt, despawned, rejected, nil) → read of `[0 + 0xd24]` | CONFIRMED |
| 2 | B.4 `0x00a40349`-`0x00a40355` | `airplane_fly_to_do` with a crewed airplane and a nil/boolean/table (or absent, stale non-string) target → `strlen` of null | CONFIRMED |
| 3 | C.3 `0x008f9957` (and `0x008f9280`; inside `0x00e25040` too) | object-indicator UI set-up when the 32-record pool's free list is empty → null result read at `+0x14` (or `+0x8` inside the pool routine) | CONFIRMED structure, reachability OPEN |
| 4 | B.3 `0x00a40483`-`0x00a404a3` | `airplane_fly_to_do` navpoint target builds 1 point; the flight request needs ≥ 2 → always false, plane never moves | CONFIRMED |
| 5 | B.3 | navpoint failure (and every post-allocation failure) leaves a kind-2 request pending forever and releases the vehicle's previous kind-2 request | CONFIRMED structure, waiting consequence HYPOTHESIS |
| 6 | D.3 `0x004b1ef0` | unknown animation → slot claimed with index −1, stale slot value returned → binding reports 1 and sets player script mode 2 | CONFIRMED structure, value HIGH CONFIDENCE |
| 7 | D.3 | non-string animation name → name built from an uninitialised 64-byte stack buffer (bounded, no overflow) | CONFIRMED structure |
| 8 | E `0x005a01bd` | a fade requested from inside the completion callback has its callback cleared by the native | CONFIRMED structure, reachability OPEN |
| 9 | C.4 | "closest" ignores height, can pick the reference character itself, and becomes "first living member" when the reference does not resolve | CONFIRMED |
| 10 | D.3 | `action_play_custom_do` failure value 0 is truthy; extension stripped twice | CONFIRMED |
| 11 | B.4 | path longer than 24 nodes silently truncated | CONFIRMED |
| 12 | M.5 | unconditional argument reads (9 slots across three bindings) read stale stack content when absent | CONFIRMED (tranche 22 M.1 semantics) |

---

## Q. Clean-room check

- Addresses are plain hex throughout; no Ghidra auto-names for functions, globals or labels, no decompiler variable
  names, and no pasted pseudocode. Library routines are named only where Ghidra identified them as standard C runtime
  (`strlen` as an inlined loop, `strncpy`, `strrchr`, `memset`) or as the public Lua 5.1 API by established project
  convention (`lua_gettop`, `lua_tolstring`, `lua_pushcclosure`, `lua_setfield`, …); a handful of single machine
  instructions are quoted only to pin a comparison. Short game strings (registered names, `".anim"`,
  `"object_indicator"`, `"player"`, `"script_mode_params"`) are quoted as data.
- Self-check run on the finished file, as the last step before reporting, with the pattern
  `\b(iVar[0-9]+|uVar[0-9]+|piVar[0-9]+|puVar[0-9]+|pvVar[0-9]+|fVar[0-9]+|sVar[0-9]+|cVar[0-9]+|bVar[0-9]+|pcVar[0-9]+|pbVar[0-9]+|local_[0-9a-fA-F]+|uStack_?[0-9a-fA-F]+|auStack_?[0-9a-fA-F]+|unaff_[A-Za-z0-9]+|extraout_[A-Za-z0-9]+|undefined[0-9]+|param_[0-9]+|in_[A-Z]{2,4}|LAB_[0-9a-fA-F]+|FUN_[0-9a-fA-F]+|DAT_[0-9a-fA-F]+)\b`
  (Python `re`, line by line, over the whole file including this section): **0 hits.** The pattern was first run
  against 18 controls: 13 positives (auto-named locals and parameters, a register input, two stack forms, a type name,
  the `LAB_`/`FUN_`/`DAT_` label forms, an `extraout_` register, an `unaff_` register) and 5 negatives ("left undefined",
  "in ECX", a bare `0x…` address, two ordinary phrases). All 5 negatives stayed silent and 12 of the 13 positives
  matched; **the one miss is the control `unaff_` + `FS_OFFSET`**: its register part contains an underscore, which
  `[A-Za-z0-9]+` cannot cross, and the trailing `\b` then fails between two word characters. So the pattern does not
  catch auto-names of that shape. To cover the gap, a plain substring scan for `unaff_`, `extraout_`, `_OFFSET`,
  `Stack_`, `Var`, `param_`, `local_`, `FUN_`, `DAT_`, `LAB_` and `undefined` was run as well: every occurrence is inside
  this clean-room section (the quoted pattern and this paragraph, which name the forms searched for); none occurs in
  sections A-P. The pattern scan was run again after this paragraph was written, with the same result (0 hits).
- No spec file was edited. The private Ghidra copy `tools\gp_t25` was deleted after the dumps; raw dumps stayed in the
  session scratchpad.
