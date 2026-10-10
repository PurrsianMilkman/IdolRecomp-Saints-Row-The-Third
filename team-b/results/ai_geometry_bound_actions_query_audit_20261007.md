# AI Action Routines 7/15/23/37/38/39/52 — Query/Data Audit for Team A

**Purpose.** This project's AI runtime (`sr3airuntime`, merged `a644e9c`) leaves 7 of its 70 action
routines (7, 15, 23, 37, 38, 39, 52) unregistered in `src/ai_actions.cpp`'s `ActionRegistry`
constructor — calling any of them throws `OpenStateError` citing `spec-ai-runtime.md Sec11.3`. These
7 are this project's **own scope choice**: `spec-physics-runtime.md` §8.6 independently disassembled
two of them (7, 52) and confirmed **neither makes any Havok call**, and none of the other five's
`spec-ai-runtime.md` §11.3 text names a Havok address either. So the block is on navmesh data,
vehicle-geometry data, or some other not-yet-exposed query — never on physics/collision per se. This
document pins down, per action, exactly *which* query, cross-checked against every spec section that
already bears on it (not just the AI unit's own citations), and whether the newly-lifted `.czn_pc`
zone-interior work and the landed `spec-zone-data-format.md` §11 change that picture.

**Headline finding.** The 7 actions are not a uniform "waiting on navmesh" bucket. Splitting them by
what they actually need:

| Action | Real blocker | Navmesh? | Vehicle geometry? | Other project gap? |
|---|---|---|---|---|
| 7 melee vehicle | vehicle local-frame bounding box + point-to-box distance formula | no | **yes** | the formula itself (`0x004f2970`) is named but not derived |
| 52 brute car flip | same box/distance pair, plus 4 unresolved vehicle-state predicates | no | **yes** | 4 predicate functions named across 3 specs, meaning undecoded |
| 15 melee stand back | a target-side "linked object" blackboard field (`+0xda8`) this project has no name for | no | no | **yes — a specific AI blackboard field** |
| 23 move rush to melee | the same navmesh-pathfind-point fact action 22 already stubs, plus a vehicle-occupancy check this project **already has** elsewhere | partially (same gap as action 22) | no | wiring gap, not a data gap |
| 37 follow to car | a seat-index chooser and an entry-point getter, both unread; the seat *eligibility* rule itself is now CONFIRMED (spec-player-runtime.md §13.5) | no | partially — seat data exists, entry-point extraction doesn't | the picking function `0x005457f0` itself is unread |
| 38 follow other car | same as 37, plus group-member/vehicle-role resolution | no | partially, same as 37 | same as 37 |
| 39 follow carjack | a vehicle-class candidate search; the class getter is named elsewhere, the selection logic (3 addresses) is not | no | partially | 3 unread helper functions |

No landed `.czn_pc` work bears on any of the 7: `spec-zone-data-format.md` §11 (the new Objects/0x2234
section) is a static-prop placement/property stream, and **zero sections of `spec-zone-data-format.md`
mention navmesh at all** (grepped; confirmed absent). Navmesh itself — the in-memory cell/connectivity
format and the pathfinding/snapping algorithms behind `0x004e91c0`, `0x00539200`, `0x00a12a80`,
`0x00a12180`/`0x00a11e10` — has **no spec section anywhere in this project**, landed or in progress.
`spec-physics-format.md` §2 already confirms Havok AI/navmesh middleware is **not linked** into the
executable (0 occurrences of `hkai*`), so this is Volition's own in-house format, still fully OPEN. The
two actions that touch it here (23 directly, 15 only tangentially) are not closer to unblocked because
of the zone-interior hold being lifted — that hold gated `.czn_pc`'s *object placement* stream, not the
navmesh the AI runtime's pathfinding calls would need.

---

## Action 7 — `melee vehicle` (`0x005525c0`)

**CONFIRMED control flow** (`spec-ai-runtime.md` §11.3 C): `0x00551d90` ("can melee", already modelled
as `ActionWorldFacts::canMeleeGate`, shared with actions 6/15/23); T must resolve to a vehicle; **the
human must be within melee range of the vehicle's bounding box, measured in the vehicle's own frame**
(`0x004aac40` → `0x004f4770`, then the point-to-box distance `0x004f2970`); `+0x24` = the vehicle's
position (trivial, no geometry needed).

**Exact query/data shape needed.** Two things, not one:
1. **The vehicle's own cached local-frame bounding box** (half-extents + the vehicle's world transform,
   to build the local-frame point first). `spec-physics-runtime.md` §8.6 disassembled this action
   directly and found the geometry source is "a plain gameplay vtable slot... likely a cached
   bounding-box getter" on the vehicle object (`[[edi]+0]+0x60`) — **not** one of this project's 39
   Havok-RTTI vtable rows, so it is this project's own cached box, not a live Havok query.
2. **A point-to-box distance function matching `0x004f2970`'s behaviour**: `spec-ai-runtime.md` §11.6
   names it ("a clamp-and-measure point-to-box distance") and `spec-physics-runtime.md` §8.6 confirms
   it is pure vector math with no Havok call hiding behind an early-exit branch, but **no spec derives
   its actual formula** (clamp-per-axis-then-measure vs. a true OBB surface distance, tolerance, etc.).
   This is the exact same function the already-*modelled* `advance` goal test's "vehicle-geometry tail"
   needs (`decision.h`'s `GoalFacts::vehicleGeometryClearTail`, currently a **simplified bool defaulting
   to "clear"**, not real math) — so even where this project already injects a fact for this query, it
   has never actually implemented the math, only stubbed the answer.

**Cross-check against landed vehicle specs.** `spec-vehicle-geometry.md` §4 (row `+0x60`–`+0x7F`,
CONFIRMED-by-disassembly-HIGH-CONFIDENCE) already gives the *per-part* AABB min/max that seeds a
vehicle's bounding-box accumulation, and §11.11 confirms each part additionally gets a real
Havok `hkpBoxShape` built from those half-extents. §4's own prose (line 142) also already notes "three
bounding boxes are computed from root parts... seeded from `+0x60`" at the vehicle level. **So the raw
per-part/per-vehicle AABB data this query would consume is already substantially specified** — what is
*not* yet pinned down is (a) which of those three vehicle-level boxes the AI's vtable getter actually
returns, and (b) the point-to-box formula itself.

**Status: PARTIALLY UNBLOCKED.** The data shape (vehicle local-frame half-extents) is largely already
in `spec-vehicle-geometry.md`; Team A's remaining job is narrow and concrete: (1) disassemble the
vehicle game-object's own vtable slot `+0x60` (or confirm it against the three boxes §4/§11 already
describe) to say which cached box backs melee-vehicle's query, and (2) derive `0x004f2970`'s actual
point-to-box math (it is reused by the already-implemented `advance` goal test too, so solving it once
unblocks both).

---

## Action 52 — `brute car flip` (`0x00526240`)

**CONFIRMED control flow** (`spec-ai-runtime.md` §11.3 G): T in a vehicle that resolves; a game-mode
switch (`0x00614d00`, a byte jump table); the vehicle not in any of the states `0x00ad31a0`, `0x00ad3270`,
`0x00ad3290`, `0x00ad3140`; vehicle `+0x5c` ≥ 0.1; T visible; a clearance probe around the vehicle must
be clear; `+0x08` = the vehicle.

**Exact query/data shape needed**, resolved item by item against what already exists elsewhere in this
project's own landed specs:
1. **`0x00614d00`** — already fully named: `spec-player-runtime.md` §13.2 traced it as "a thin `jmp` into
   `0x006d2910`, a session/mode-kind getter" (solo/co-op/other). **Not a geometry or navmesh query at
   all** — this gate is a session-mode check, already resolved by existing work, not a blocker.
2. **The vehicle `+0x5c` ≥ 0.1 check** — already modelled in this project's own `ActionWorldFacts` as
   `targetVehicleOk` ("action 52/53; also gates 23's riot path, unused here" per `actions.h`'s own
   comment), so the fact-injection shape for this already exists; it is simply unwired because 52 is
   unregistered.
3. **The 4 vehicle-state exclusions (`0x00ad31a0`/`0x00ad3270`/`0x00ad3290`/`0x00ad3140`)** — this is the
   one genuinely open item. These 4 addresses are **named, but not semantically decoded, across three
   different specs**: `spec-player-runtime.md` §8.3 lists all 4 in an 18-entry "type tests" census with
   no meaning assigned to any of them; §13.4 step 5 uses `0x00ad3140` only as "decides which of two
   blocks runs" (role, not meaning); `spec-lua-api-behaviour.md` §9.8/§12.11 confirms `0x00ad31a0` is
   "true only for internal state values `3`/`4`" and ties it to helicopter AI-drive-mode, and separately
   ties `0x00ad3140`'s target field to literal `14` = HIGH-CONFIDENCE "Tank" class — but neither spec
   states what car-flip's own exclusion is actually testing for (likely: already flipped/airborne/
   scripted/occupant-ejecting states, but this is my own inference, not confirmed anywhere).
4. **The "clearance probe around the vehicle"** — `spec-physics-runtime.md` §8.6 disassembled this
   directly: it "resolves entirely to vector math: a local-frame point transform (`0x00525d30`, which
   itself only calls the already-named `0x004f4770`) plus one indirect vtable call on the vehicle game
   object itself (`[[edi]+0]+0x60`... likely a cached bounding-box getter) plus the already-documented
   clamp helper `0x004bc680`." **This is the identical vehicle-box + vtable-getter dependency action 7
   needs** (above) — not a separate ask.
5. **T visible** — already a general-purpose fact this project models for essentially every other
   action (`0x0050cdb0`/`0x004fdac0` family, `spec-ai-runtime.md` §11.6); not a new need.

**Status: PARTIALLY UNBLOCKED.** Once action 7's vehicle-box/point-to-box work lands (item 4, shared),
52's only remaining open item is #3 — decoding what the 4 vehicle-state predicates actually test. That
is a bounded, concrete ask: 4 named addresses (already cross-referenced across player-runtime and
lua-api specs as plain vehicle-type/state predicates, none Havok, none navmesh), just needing their
return-value semantics read out.

---

## Action 15 — `melee stand back` (`0x0053d140`)

**CONFIRMED control flow** (`spec-ai-runtime.md` §11.3 C): T within a run-time radius (`0x013bad60`,
latched — an ordinary tunable, no new query); a human holding a ranged weapon needs `+0xf4` → `+0xe8`
bit `0x40`; **if T is in the state `0x009826e0` (`+0xcc8` = 12 with `+0xcce` 20 or 21): `+0x24` = the
object linked at `+0xda8`** (or T's position plus its facing), `+0x30` = T's position; otherwise it
walks list 5 for an ally on the same T, or a player within `0x013bad40` who is facing T, and uses that
one's position as `+0x24`.

**This is the one action of the seven that is NOT primarily blocked on navmesh or vehicle geometry.**
Working through each piece against what this project already models:
- The `+0xcc8`/`+0xcce` state-field pattern is already read elsewhere (e.g. `h.world.stateCcd`/
  `stateCce` feed `MoveAdvance`'s B test and action 59's `targetStateCce`), so that part of the state
  test is an ordinary, already-patterned read, not a new primitive.
- **"The object linked at `+0xda8`" is the genuinely open item.** This project's own engine model
  already has a documented precedent for a *different* per-object "linked object" slot — `+0x458`,
  confirmed shared between `Human shield`'s begin and `Veh evade`'s begin (`spec-ai-runtime.md` §13.1.6:
  "confirming `+0x458` is a shared general-purpose 'linked object' slot, not mode-specific") — but
  `+0xda8` is a **different offset, on the target rather than the acting human**, and no spec anywhere
  (grepped across every `spec-*.md` in this project) cross-references it, names what it holds, or says
  who writes it. It is not in `ActionWorldFacts` either. This is squarely "a specific AI-blackboard
  field this project doesn't expose yet" — distinct in kind from the other 6 actions' vehicle/navmesh
  gaps.
- The fallback branch ("walks list 5 for an ally... or a player... facing T") is an ordinary
  perception-list walk plus a facing-cone test, both patterns this project already uses for other
  actions (26's `targetWithinFacingCone`, 33/46's list-5/candidate-handle walks) — mechanically
  fact-injectable the same way, not blocked on anything new.

**Status: STILL FULLY WAITING, but on a narrow, specific ask.** Team A's job here is not navmesh or
vehicle geometry at all: it is to identify what object the target's `+0xda8` field links to (a held
prop? a riot shield? a linked cover object? — state `0x009826e0`/`+0xcc8=12,+0xcce∈{20,21}` is the
contextual clue, likely a specific held-object or interaction state) and who writes it. Once that one
field is named, the rest of action 15 is implementable with patterns this project already has.

---

## Action 23 — `move rush to melee` (`0x0053de30`)

**CONFIRMED control flow** (`spec-ai-runtime.md` §11.3 C): the same preamble as action 22 `move rush`
(which **is** implemented); T not in a vehicle; distance between the melee reach (≥ 3.0) and the second
range; riot-shield exclusions (refuse if T is a player with an armed `+0x277c` timer, or another shield
holder in list 5 is already doing 23 on the same T); accepts when the point from `0x004e91c0` is a
valid (non-NaN) position. **Writes nothing.**

**Every single gate this action needs already has a modelled fact or an existing primitive elsewhere
in this project** — this is the most "already-seamed" of the 7:
- **Melee-band distance test**: `ActionWorldFacts::meleeRangeA/meleeRangeB` already exist, shared across
  the "6/7/15/23" family comment block, and `WeaponBandTest` (the helper action 6's own implementation
  already calls) is the exact function needed.
- **"T not in a vehicle"**: this project **already models the identical underlying primitive**, just at
  the goal level rather than the action level — `decision.h`'s `GoalFacts::targetInVehicleState3` ("T's
  `+0x16d4`==3", used by the `advance` goal's own step 7) is cross-confirmed by
  `spec-player-runtime.md` §13.1 step 2 to be `0x009b9160`, the engine's generic "seated/in a vehicle"
  predicate. The same fact, exposed to `ActionWorldFacts` instead of only `GoalFacts`, answers this
  gate directly. **This is not something Team A needs to spec — it is a wiring gap inside this
  project's own code**, not an external data dependency.
- **Riot-shield / "one shield rusher per target"**: `ActionWorldFacts::noGroupMemberDoingSameAction`
  already exists for exactly this purpose ("actions 23 (riot), 50" per its own comment).
- **The pathfind-point validity test (`0x004e91c0`)**: already modelled as `ActionWorldFacts::
  pathfindSucceeded`/`precomputedPoint`, and already *used* by the implemented action 22. Action 23
  needs nothing new here beyond reusing the identical fact.

**Status: PARTIALLY UNBLOCKED — in fact the readiest of the 7 for wiring, but the underlying real
primitive it defers to is the one piece still fully OPEN project-wide.** Every CONFIRMED gate in 23's
own control flow can be implemented today with facts this project's seam already has a name and a slot
for (meleeRangeA/B, noGroupMemberDoingSameAction, pathfindSucceeded/precomputedPoint, plus a
`targetInVehicle`-shaped bool this project would just need to lift from `GoalFacts` into
`ActionWorldFacts`). Nothing here is blocked on a NEW spec from Team A. What is still blocked — for 22 as
much as for 23, and project-wide — is that `pathfindSucceeded`/`precomputedPoint` are host-supplied
placeholders standing in for the real `0x004e91c0` navmesh pathfind query, whose underlying navmesh
cell/connectivity format has **zero spec coverage anywhere in this project** (confirmed: `navmesh`
does not appear in `spec-zone-data-format.md` at all, and no other spec file defines an in-memory
navmesh format). So 23 is "implementable today at the same fidelity as 22" but "real" only once Team A
specs navmesh itself — a project-wide dependency, not something specific to 23.

---

## Actions 37/38/39 — `follow to car` / `follow other car` / `follow carjack`

These three share one family and one newly-relevant cross-reference: `spec-player-runtime.md`'s U6.2
unit (§13, landed independently of this task, reviewed 2026-10-01) traced the **player-side** vehicle
entry/seat-choice chain in detail and explicitly confirmed it is the **same chain the AI side calls**
(§13.0 item 7, §13.7's last bullet: "the AI side reuses this exact chain below the submitter"). That
gives three of these actions' seat-eligibility logic a real, CONFIRMED spec for the first time — but it
stops one layer short of what the AI routines themselves call.

### Action 37 — `follow to car` (`0x005487e0`)

**CONFIRMED control flow**: `0x00548590` (a force-flag/state gate, not geometry); refused by
`always_follow_in_alternate_vehicle` (`+0x2ba` bit 0, an ordinary `.xtbl`-sourced force flag, already a
known category per `spec-ai-behavior-format.md`); the group leader's vehicle within 30 and not more
than 5.0 above the human (`0x00ad4810`); **a seat from `0x005457f0` → `+0x3c`**; **the seat's entry
point (`0x00b0eac0`), transformed to world space (`0x004e44b0`, `0x004f4770`) → `+0x24`**; the vehicle
→ `+0x08`.

**Exact query/data shape needed**, now split into a resolved half and an open half:
- **Seat *eligibility* (does any seat qualify at all)**: `spec-player-runtime.md` §13.5 just confirmed
  `0x00b0eb90` ("seat scoring") in full — the per-seat `Seat_Info` record layout (vehicle `+0xbf4` →
  `+0xa7c` → `+0x44` + `0x24·seat`, flag bits for FLAG_A–D, disabled bit, door/entry-point-configured
  bit) is now CONFIRMED-by-full-listing, not HYPOTHESIS. **`0x005457f0` is explicitly named as one of
  `0x00b0eb90`'s three call sites** (§13.5's own closing line) — so the eligibility rule this action's
  seat pick is built on is already specified; it was simply specified by a different unit (player
  runtime) for a different reason (player vehicle entry), not yet connected to this AI action by
  anyone.
- **Which *one* seat `0x005457f0` picks**: `0x005457f0`'s own body is explicitly flagged as **unread**
  by both passes that have touched it (`spec-player-runtime.md` §13.5: "not traced by either pass — new
  OPEN item"). `0x00b0eb90` itself only answers "does a matching seat exist", not which index — per
  §13.5 step 3, "nothing here writes the winning seat index anywhere the caller can read." So the actual
  seat-index selection among several eligible seats is still fully open.
- **The seat's entry point (`0x00b0eac0`)**: called *inside* `0x00b0eb90` itself (§13.5 step 3:
  "`0x00b0eac0(vehicle, seat, -1, 0, 0, &out1, &out2)` and requires it to return `1`"), but its own
  internals — what `out1`/`out2` actually contain, what coordinate frame they're in — are not traced by
  that unit either. This is the raw per-seat geometry lookup action 37 needs for `+0x24` and it remains
  unread.
- **`spec-vehicle-data.md`/`spec-tables-vehicle-world.md` cross-check**: per-seat data is already
  extensively specified at the `.xtbl`/in-memory-record level — `Climb_To_Entry_Offset` vec3
  (`spec-vehicle-data.md` row `+0x8A8`–`+0x8B4`), `Seat_Specific_Data` (8×`0x10` bytes), the shared
  8-seat resolver `FUN_00AC1BA0`, and `vehicle_interaction_info.xtbl`'s own `Seat_Info` sub-records
  (`spec-tables-vehicle-world.md` §3.2, CONFIRMED 2026-10-01). **None of this is yet shown to be the
  same data `0x00b0eac0` reads** — it is adjacent, plausible raw material, not a confirmed match.

**Status: PARTIALLY UNBLOCKED.** The eligibility *filter* is now solid (§13.5, landed independently of
this audit). What is still open is narrow and already named by `spec-player-runtime.md` itself as an
OPEN item: read `0x005457f0` (to see how it picks among `0x00b0eb90`'s eligible seats) and `0x00b0eac0`
(to see what it actually returns). Both are small, already-located functions, not a new spec category.

### Action 38 — `follow other car` (`0x00548e80`)

Same mechanism as 37 (so the same status applies to the seat/entry-point half), plus one additional,
independent resolution step: "for a group member's vehicle that is not the leader's... a member in
`Hijack` (18) offers its own committed block's handle (`+0x458`); a member in `Passenger` (25) or `Veh
trans` (41) offers its vehicle." This reuses the already-cross-confirmed `+0x458` "linked object" slot
(`spec-ai-runtime.md` §13.1.6 — see action 15's section above) and ordinary mode-state reads (current
mode, committed block), **not a new query at all** — this half of 38 is already fully modelable with
patterns this project has. **Status: same as 37 — partially unblocked, blocked only on `0x005457f0`/
`0x00b0eac0`.**

### Action 39 — `follow carjack` (`0x00549080`)

**CONFIRMED control flow**: `0x00548590` (the same state gate as 37); **a candidate vehicle, chosen by
vehicle class (`0x00ad2f80`, 0…5, through a jump table) or by `0x0051cc80`/`0x0051ce60`/`0x0051cd70`**
→ `+0x08`; sets `+0x56` bit 5, clears `+0x3c` and `+0x50`.

**Exact query/data shape needed**:
- **`0x00ad2f80`, the vehicle-class getter**: partially cross-referenced elsewhere. It reads vehicle
  `+0xbf4`→`+0x2c` (the same field `0x00ad31a0`'s "3/4 = helicopter" test reads, per
  `spec-lua-api-behaviour.md` §17.28), and `spec-player-runtime.md` §13.4 step 4 independently confirms
  it "must equal 5" for the ordinary driver-class gate. So values 3, 4 and 5 each have at least a
  partial, HIGH-confidence meaning from other units' work; **values 0, 1, 2 and the full 0…5 enum this
  action's own jump table switches on are not enumerated anywhere.**
- **`0x0051cc80`/`0x0051ce60`/`0x0051cd70`**: these three addresses appear **nowhere else in any landed
  spec** (grepped across every `spec-*.md` in this project) — no role, no size, no call contract beyond
  this one citation. This is the one piece of the whole 7-action set with **zero existing cross-spec
  material to build on.**

**Status: STILL FULLY WAITING.** Unlike 37/38, there is no adjacent unit's work to lean on here for the
candidate-selection half. Team A's job is concrete: (1) enumerate `0x00ad2f80`'s full 0…5 class range
(partial material exists at 3/4/5, from unrelated units — worth reusing rather than re-deriving), and
(2) read the three unnamed functions `0x0051cc80`/`0x0051ce60`/`0x0051cd70` from scratch.

---

## Judgment calls and residual uncertainty

- **I did not re-derive any address myself.** Every claim above either restates `spec-ai-runtime.md`
  §11.3/§11.6's own CONFIRMED/HIGH text, or cross-references another already-landed spec section
  (`spec-physics-runtime.md` §8.6, `spec-vehicle-geometry.md` §4/§11.11, `spec-vehicle-data.md` §7.3/
  §7.4, `spec-tables-vehicle-world.md` §3, `spec-player-runtime.md` §13) that happens to bear on the
  same native address. Where a spec left a question open (e.g. the 4 vehicle-state predicates for
  action 52, `0x005457f0`/`0x00b0eac0` for 37/38, the three unnamed functions for 39), I reported it as
  open rather than guessing at a meaning.
- **The `targetInVehicleState3` / `0x009b9160` finding for action 23 is the one place I'm drawing an
  inference across two different parts of this project's own code** (a `GoalFacts` field used by the
  goal layer, cross-confirmed against a `spec-player-runtime.md` citation of the same native function)
  rather than quoting a single spec passage directly. I'm confident in it — the offset (`+0x16d4`==3)
  and the "seated" behaviour match exactly — but flagging it as a cross-reference I assembled, not a
  single source's own claim.
- **Action 52's "clearance probe" vector-math reading leans on `spec-physics-runtime.md` §8.6's own
  disassembly**, which is solid (CONFIRMED by that unit's clean-room self-scan), but that unit
  explicitly disassembled only 7 and 52 among the seven — I did not independently verify that reading,
  only restated it.
- **I could not find any spec, in this project or elsewhere in scope, that names what state
  `0x009826e0`/`+0xcc8=12,+0xcce∈{20,21}` actually represents**, beyond its bare citation in action 15's
  own line. My inference that `+0xda8`'s linked object is probably a held prop/riot-shield/interaction
  object is a plausibility guess from the surrounding context, not a confirmed reading — flagged as
  such in that action's section and should not be forwarded to Team A as settled.
- **Action 39's vehicle-class range (0…5)**: I reported 3/4/5 as having partial meaning from unrelated
  units' work (helicopter modes, driver-class gate) — this is a reasonable reuse, but nobody has
  confirmed those are the *same* semantic axis action 39's jump table switches on, only that they read
  the same underlying field. Flagging this as a plausible but unconfirmed reuse, not a settled fact.

## Review recommendation

This is a documentation/cross-reference synthesis over already-landed material, not new reverse
engineering, new code, or a safety-relevant change — a **quick read-through by Team A is enough**, not
full adversarial review. The one thing worth a second pair of eyes before Team A acts on it: the
`targetInVehicleState3`/`0x009b9160` cross-reference for action 23 (the one inference I assembled
myself rather than restated), and the action-39 vehicle-class-range reuse call-out above.

## Files read (this project's own, already-produced material only)

- `include/sr3airuntime/actions.h`, `include/sr3airuntime/decision.h`, `src/ai_actions.cpp`
  (this project's own source — confirms which 7 actions are unregistered and what facts already exist)
- `spec-ai-runtime.md` §11.3, §11.6 (the 7 actions' own CONFIRMED control flow and helper citations)
- `spec-physics-runtime.md` §6–§8, especially §8.6 ("The AI-action connection")
- `spec-vehicle-geometry.md` §4, §11.11 (per-part/vehicle bounding-box and Havok box-shape data)
- `spec-vehicle-data.md` §7.3, §7.4 (seat tables, `Climb_To_Entry_Offset`)
- `spec-tables-vehicle-world.md` §3 (`vehicle_interaction_info.xtbl` seat records)
- `spec-player-runtime.md` §13 (U6.2 — vehicle entry/seat-choice chain, AI-shared)
- `spec-zone-data-format.md` (grepped in full for `navmesh` — zero hits; §11 read for scope)
- `spec-physics-format.md` §2 (Havok-AI/navmesh middleware linkage — confirmed absent)
- `spec-ai-behavior-format.md`, `spec-lua-api-behaviour.md`, `spec-tables-traffic-ai.md` (navmesh string
  survey, force-flag category confirmation)

No leaked SR4/Re-Elected or Havok source, and no community format-doc/mod-tool site, was consulted at
any point.
