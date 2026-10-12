# Saints Row: The Third — Vehicle Damage, Destruction, Fire/Smoke, and Explosion Runtime (`SaintsRowTheThird.exe`)

**Scope.** The runtime (CODE) half of vehicle damage/destruction, as distinct from
`spec-vehicle-data.md` (DATA side: `vehicle.xtbl`'s `Max_Hitpoints`/`Player_Damage_Multiplier` fields).
Extends `spec-weapons-runtime.md` §2.4/§4.2's own partially-traced `0x00a9d6c0` to a full, instruction-
level, end-to-end trace. Cross-references `spec-hud-runtime.md` §2 (vehicle HP fields/accessors),
`spec-lua-api-behaviour.md` (several already-documented vehicle-damage Lua natives), and
`spec-tables-vehicle-world.md`/`spec-tables-weapons-combat.md` §10.1 (the `explosions.xtbl` system this
file confirms vehicles reuse).

**Method.** Ghidra-free (`pefile`+`capstone`, shared server under contention). Addresses as plain hex;
`FUN_`/`DAT_` never used as if real identifiers. No leaked Volition/Havok/Audiokinetic/RAD material.

**Confidence key:** **CONFIRMED** (full disassembly), **HIGH** (solid structural inference), **HYPOTHESIS**
(plausible, materially uncertain), **OPEN** (not resolved — a gap, not a guess).

## 1. The generic damage sink link — fully closed

**`0x00a9d6c0`** (full 418-instruction body) is the occupant kill/eject/ignite/fling loop over a vehicle's
8 occupant slots. Per occupant, after resolving it through four accessor calls (`0x00ab5070`/
`0x00ab5150`/`0x00ab51a0` ×2) and an authority gate (`0x008addb0`), one of three outcomes:

- **Clean eject**: if the occupant is a "valid target" (`0x0091f5e0`) and has an "eject cleanly" flag
  set (`+0x1c98` bit `0x40`), calls the exit-request function `0x00af3cd0` (`spec-player-runtime.md`
  §6.3) with zeroed parameters — no damage, no fling.
- **Kill (± fling)**: otherwise, if two further eligibility gates pass (`0x00ad3820`, `0x0092aa20`) and
  (unless a "fling prevented" predicate `0x009b9bf0` trips) computes a swept position delta via
  `0x00da38e0`, draws a PRNG value (`0x00dab6a0`), and calls `0x00af3cd0` again with the computed
  position/velocity — a randomized "explosion fling" impulse. Either way, the occupant is then ignited
  via **`0x0097cf80`** (the exact routine shared verbatim between `character_ignite` and
  `character_damage`'s own incendiary path) and killed: a damage-request record is zero-initialized via
  `0x0096f3b0` (a second, address-distinct sibling of the already-documented `0x0067b050` helper — same
  role, different address, OPEN why two exist), then **`0x0096fa20` is called with the literal `-1.0`**
  and flag `0x2000`. The three static call sites (`0x00a9d999`/`0x00a9da61`/`0x00a9db41`) are exactly the
  three `spec-weapons-runtime.md` §4.2 already named.
- **Already-dead/ineligible**: a small cleanup call, `0x00853ea0(0,0,occupant)`.

A shared instigator-handle pair, globals `0x11818d0`/`0x11818d4`, feeds all three `0x0096fa20` calls when
the occupant's own cached attacker fields are absent — the same globals reappear inside `0x00a9e1d0`
(§2) as the destruction trigger's own instigator-resolution fallback, a real cross-function link.

**`0x00a9d6c0` has exactly one caller in the whole binary**: `0x00a9e2f9`, inside `0x00a9e1d0` (§2) —
confirmed by a whole-`.text` direct-call-site scan. The generic damage sink is reached by vehicle
occupants through exactly one trigger path in the entire game, not merely "known to be called three
times."

## 2. The real destruction trigger, `0x00a9e1d0` (newly identified)

Not previously named in any spec, review note, or scratch file. Found by tracing `0x00a9d6c0`'s own sole
caller backward. Full body read (662 instructions to a clean `ret`+`int3` boundary).

**Entry gates**, any failure jumps straight to the epilogue (one-shot, re-entrancy-guarded): (1) vehicle
pointer non-null; (2) a readiness byte on a per-vehicle sub-object (`[[+0x1e00]+0x10+0x60] == 1`); (3)
`+0xbd0 == 1` ("has sub-components," already documented); (4) `+0x16c8` bit `0x1` **clear** (not already
mid-destruction).

**Body, in execution order:**
1. `0x00af9030` (OPEN identity), then **sets** `+0x16c8` bit `0x1` (the re-entrancy latch).
2. Two further notify/state calls, `0x008cc930(edi, 0x40)` and `0x00a7f850(edi)` (OPEN identity).
3. **`0x00a85ec0(edi)`** — the already-documented per-vehicle visual-pose driver — forces an immediate
   pose refresh, plausibly snapping wheels/suspension into a settled wreck pose.
4. **Instigator resolution**: resolves from the vehicle's own cached last-attacker handle pair
   (`+0x1358`/`+0x135c`) via `0x0091faf0` if none was passed in, falling back to the local player. If the
   resolved instigator is a "valid target" (`0x0091f5e0`), calls `0x007112d0` (OPEN, plausibly a
   kill-attribution/stat hook).
5. **`0x00b206f0(&[+0xc68])`** — resets the vehicle's AI-behavior sub-record (the same sub-record
   `vehicle_chase`/`vehicle_suppress_npc_exit` use) — stops any in-progress chase/pathfind behavior.
6. Resolves the final instigator object and calls **`0x00a9d6c0(..., edi, resolvedInstigator, 1)`** — the
   literal `1` is the `flingAndIgnite` flag, so every occupant of a vehicle destroyed through this real
   trigger is always eligible for ignite+fling, not just a kill.
7. **Fire/smoke/ambient-sound VFX** (§6 below).
8. **Vehicle-kind special case**: `0x00ad2f80` (already-documented vehicle-kind dispatcher); kind `== 2`
   gets an extra call, `0x00aa5ab0` (OPEN, plausibly motorcycle-fall-over/boat-sink). A separate check
   against the skydiving-tank predicate (`0x00ad3140`) gates a further small block, not fully traced.
9. **Part-detachment two-pass walk** (§5 below).
10. **Explosion** (§4 below).
11. Gated on this function's own third stack parameter: a PRNG draw, calls to `0x00ab9030`/`0x00abae80`/
    `0x00ab9800` (OPEN, vector/physics-shaped), and arms a **750 ms** timer at `+0x15f0` via the shared
    `0x00d9e140` primitive — HYPOTHESIS a delayed secondary effect (debris settle, a second small
    detonation), not confirmed.
12. A final instigator-handle resolve (same `0x00458230`/`0x2cc9900`/`0x00853b10` chain, testing a
    newly-sighted capability bit `+0xa`/`0x2`, role OPEN) feeding `0x009e2ac0` (OPEN, plausibly a final
    kill-attribution/notification call).
13. **Clears** `+0x16c8` bit `0x1` unconditionally on this path, calls `0x00a7fef0`, returns.

**A flagged tension, not resolved: `+0x16c8` bit `0x1` vs. `vehicle_is_destroyed`.**
`spec-lua-api-behaviour.md` §4.9 independently confirms `vehicle_is_destroyed` reads this exact bit and
calls it "destroyed." This function sets it near the top and explicitly **clears** it again near the
bottom of the same synchronous call — meaning, at face value, any code calling `vehicle_is_destroyed`
after this function returns would observe "not destroyed." Two possible resolutions, neither confirmed:
(a) the durable "is a wreck" signal actually lives in the damage-state bits at `+0x163c`/`+0x163d`
(already documented as cleared on `vehicle_repair`) rather than `+0x16c8` bit `0x1`, which would then be
a pure re-entrancy latch; or (b) `vehicle_is_destroyed`'s own vtable `+0x70` indirection resolves to a
different object than the raw vehicle pointer this function operates on. Flagged for whoever next opens
`0x00ad3340`'s own `+0x70` target or the `+0x163c`/`+0x163d` bits' full consumer set.

## 3. `0x00a9f4f0` and `0x00a9c1f0` — the vehicle's own damage/force-apply primitives

**`0x00a9f4f0`** (full 382-instruction body — already named by `vehicle_detonate`/`vehicle_repair`).
Entry gates, each an immediate bail: null vehicle; already destroyed (`+0x16c8` bit `0x1`) → true (no-op);
two pause/suppress gates (`0x00853b10`/`0x00853840`) → true; a VFX-suppression flag (`+0x1d7e` bit `0x8`)
→ false (HIGH confidence this is the still-undocumented Lua native `vehicle_disable_explosion_and_
damage_vfx`'s own storage, not independently confirmed); invulnerability (`+0x1d7a` bit `1`) bails
**only if the incoming damage is positive** — a negative (healing) delta falls through regardless,
confirming `vehicle_repair`'s "negative damage = healing" design interacts correctly with
invulnerability; a final global check (`0x00722000`, OPEN, plausibly pause/replay) → false.

Past the gates, `+0xbd0` ("has sub-components") decides the path: **`== 0`** calls **`0x00a9c1f0`**
directly and returns true unconditionally — no destruction check, no occupant handling, no explosion (a
real, confirmed asymmetry: part-less objects sharing the vehicle HP layout never reach destruction
through this path). **`== 1`** (effectively every real driveable vehicle) takes a considerably larger
path, not fully traced, that computes per-sub-component distance/falloff-scaled damage and dispatches
through `0x00a9c6f0`/`0x00a9d070`/`0x00a9dd40`/`0x00a9dbe0` (none opened — **OPEN, highest-priority
named lead** for whoever next traces exactly how/where accumulated component damage decides to call
`0x00a9e1d0`).

**`0x00a9c1f0`** (full 19-instruction body) — the simple-vehicle HP clamp: `+0x1564 -= clamp(delta, 0,
currentHP)`, then re-clamps the result into `[+0x1560, +0x155c]` (min/max). **Never checks for HP
reaching 0 and never calls anything destruction-related** — a pure, bounded accumulator with none of
`0x0096fa20`'s death-trigger logic built in.

**A newly-found difficulty/team damage-scaling wrapper, `0x00aa0510`** (CONFIRMED structure): calls
`0x00853b30(vehicle, 0x22)` — the exact "difficulty-scaled multiplier class" test already documented in
the character-spawn-HP investigation — and, if true, reads the vehicle's team byte (`+0x1ca8`) and a
further predicate before scaling the incoming damage by a constant at `0x12a2d38` and forwarding to
`0x00a9f4f0`. Independent of `vehicle.xtbl`'s own `Player_Damage_Multiplier` field — not reconciled
against this runtime finding, OPEN.

**15 project-wide callers of `0x00a9f4f0`** confirmed by direct-call census (physics/collision code,
weapon/projectile code, both vehicle-damage Lua natives, 6 further `0x00aaXXXX` addresses) — the
strongest evidence that bullets, melee/collision, and scripted calls all funnel through one shared
vehicle damage-apply entry point, the same architectural role `0x0096fa20` plays for characters. None of
the non-Lua callers was opened to confirm the caller-side damage-amount computation — **OPEN, named**,
the natural next-priority follow-up.

**Two small helpers, vehicle-special-cased:** `0x00aa0450` resolves "explosive damage amount" for a
handle; if the resolved object is a live vehicle (`0x2cc9900[+0x34]` bit `0x80` at `+6`), returns a
hardcoded `0.0` — vehicles contribute nothing through this path, handled by `0x00a9f4f0` instead. A
sibling tiny thunk, `0x00aa04e0`, forwards to `0x00a9e1d0` when `+0xbd0 == 1`, with exactly one caller
project-wide (`0x00896183`, not traced — **OPEN, the most promising single lead** for pinning down the
literal "HP reached 0, begin destruction" decision instruction, which this file does not locate).

## 4. The explosion sequence reuses the generic `explosions.xtbl` apply routine

Inside `0x00a9e1d0` (step 10), after part detachment, three suppression checks run in order (any one
skips the explosion entirely): (1) a cached 64-bit explosion-instance handle (`+0xa68`/`+0xa6c`) already
non-zero — this vehicle already exploded once, making the mechanism idempotent; (2) `+0x1d7d` bit `0x20`
set (HYPOTHESIS: an undocumented suppression toggle, possibly `vehicle_set_invulnerable_to_player_
explosives`'s own storage, not confirmed); (3) a bit on the same `+0xbf4` per-vehicle-type sub-object
`vehicle_car_alarm_enable` reads at a different offset, here `[+0xbf4+0x874]` bit `0x100`.

Past the gates, reads a 16-bit value from global `0x11818d8`, a field at `+0x7c`, and
**`[[+0xbf4]+0x7fc]`** — HIGH confidence the vehicle-type's own resolved explosion-definition pointer, by
analogy with `level_objects.xtbl`'s `Death_Explosion` field (resolved through the identical CRC-to-
`explosions.xtbl` resolver, `spec-tables-weapons-combat.md` §10.1) — **not independently confirmed as the
same field; `vehicle.xtbl`'s own schema does not currently document a field at or near `+0x7fc` of
whatever record `+0xbf4` caches — a genuine DATA/CODE reconciliation gap**, not asserted as settled.
These values, the vehicle's stored position, and this function's stack parameters are handed to
**`0x00594460`** — **the exact function `spec-lua-api-behaviour.md` §9.3 independently identifies as the
shared explosion-apply routine behind the `explosion_create` Lua native** (falloff-scaled damage, camera
shake, the shared packed-instance-identifier finalizer). The returned 64-bit handle is cached into
`+0xa68`/`+0xa6c` (closing the idempotency gate), resolved through the standard keyed-object-resolver/
capability-table/liveness chain (testing a newly-sighted bit `+7`/`0x1`, matching `spec-lua-api-behaviour.md`
§9.21's own `effect_stop` sighting of the same bit — a real corroboration, not a new find) before a final
virtual dispatch through the vehicle's own vtable slot `+0x2c`.

**Net: vehicles do not have a separate, bespoke blast system — they reuse the same generic, data-driven
explosion machinery every other `explosions.xtbl`-sourced blast uses**, gated by vehicle-specific
suppression flags and fed a vehicle-type-specific explosion definition.

## 5. Part detachment — the likely code-side deformation mechanism

Inside `0x00a9e1d0` (step 9), a two-pass walk over the vehicle's sub-component array (`+0x688` count,
`+0x68c` array — the same array `set_max_hit_points`/`vehicle_repair` already read for HP/repair
propagation): **Pass 1** — for each part whose static-descriptor render flags don't already exclude it,
calls `0x00a88770`/`0x008a8640` and records a single "qualifies to break" bit per part into a local
bitfield. **Pass 2** — for each part, reads the same `+0x80`/`+0x84` static-descriptor/override-matrix
pointer pair the wheel pose-override mechanism uses, dispatches on a type value (a dedicated handler for
one case), and combines the part's current render-flag state with Pass 1's bitmask to decide whether to
call **`0x00a8eab0(part, vehicle, ..., 750, ...)`** — shaped exactly like "detach/break this part now,"
gated by the same 750ms-shaped literal seen elsewhere in this function.

**This is a real, confirmed, code-driven mechanism that detaches qualifying vehicle parts (doors, hoods,
bumpers) as a discrete event when the vehicle is destroyed — it is NOT a continuous, per-frame,
damage-proportional mesh-warp system.** No code was found anywhere this pass reached that reads the live
HP value and produces a proportional deformation of the body mesh itself (as opposed to a part simply
existing or not). Combined with the data-side material-authoring note already on file
(`spec-tables-vehicle-world.md`: a `shd_damagenormal_n.tga` normal map and a "damage stage" comment in
the same per-vehicle paint/material context), the honest two-part verdict: **part-level breakage is
genuinely code-computed at destruction time; fine-grained body-panel dents/scratches most plausibly come
from shader/texture authoring, not a runtime deformation system** — not independently proven for the
texture side (would require opening the material-blend consumer, out of scope), but consistent with
everything found.

## 6. Fire/smoke/engine-sound VFX at destruction

Inside `0x00a9e1d0` (step 7), three persistent, per-vehicle effect-handle fields are each tested for
validity and, if absent, created or (in one case) explicitly stopped — a small, CRC-hash-keyed effect
lifecycle system:

| Field | Action if absent | Hash literal | Notes |
|---|---|---|---|
| `+0x1628` | **play** | `0x6bb547c6` | one-shot latch `+0x163c` bit `0x10` |
| `+0x1638` | **play** | `0x286e13b2` | no latch observed |
| `+0x15bc` | **stop** | — | position/lifetime-managed differently |

HIGH confidence `+0x1628`/`+0x1638` are smoke and fire respectively (or vice versa — the hash literals
were not resolved to a name — OPEN), and `+0x15bc` is an ambient engine-idle/similar loop sound
explicitly stopped on destruction (it's the only one of the three stopped rather than started). All
three fire exactly once, gated by the function's own one-shot re-entrancy structure — **a discrete
destruction-time VFX trigger, not a progressive function of accumulated damage.** The scripted
alternative, `vehicle_set_smoke_and_fire_state` → `0x00aa3f90` (already on file, body not opened),
remains the only other confirmed smoke/fire control surface. **No third mechanism — a per-frame poll of
the live HP ratio deciding to start smoke below some threshold — was found**, despite checking every
function opened this pass for a read of `+0x1564` (current HP) outside of
`0x00a9c1f0`/`0x00a9f4f0`/`0x00a9e1d0` themselves. A genuine, bounded negative result, not an exhaustive
whole-binary claim.

## 6a. Vehicle-vs-vehicle occupant crush/pancake kill — `0x00b19ad0` (new, 2026-10-08)

A separate, structurally distinct mechanic from everything above: **`0x00b19ad0`** kills a *character*
seated in one vehicle when that vehicle is crushed against another, found while chasing an unrelated
hitscan-weapons lead (`spec-weapons-runtime.md` §2.2a) and confirmed to have nothing to do with weapons —
no weapon-definition read anywhere in its body. For up to 2 cached "vehicle I am in contact with" handles
on `this` vehicle (`this+0x16f0`/`+0x16f8`, refreshed by a companion function `0x00b19e00`), resolved via
the shared handle chain and an "is a vehicle" type-table bit, it walks `this`'s own 8 seats
(`this+0x1358+0x30·seat` — the same seat-occupant field §10.1/`0x00ab5070` already documents) and tests
whether each occupant's transformed position now falls inside the contact vehicle's own bounding box
(`0x00a7a550` fills the box; a 4-lane `cmpltps`/`movmskps` "fully inside" test). On a hit: instant-lethal
damage (`-1.0`, literal `0x012a2d54` — the **same** constant §1's occupant-kill loop and the melee
finisher both use) via `0x0096fa20`, **crediting the contact vehicle's own driver** (`0x00ab5070(contact,
0)`) as instigator; then despawns the corpse if it is a character and the kill registers.

**This is a separate damage path from both §1's own occupant-kill-on-extract loop and §2-§3's HP-driven
destruction economy** — it fires on an ordinary vehicle-vs-vehicle contact/box-overlap condition, not on
an extract-kind branch or a Havok destruction event, and operates on the *crushed* vehicle's own seated
occupants rather than the vehicle's HP. **HIGH CONFIDENCE** on the "vehicle-vs-vehicle occupant crush"
gameplay reading (every structural piece read directly — type gate, seat table, box-overlap test,
driver-as-instigator attribution, instant-lethal literal, death/despawn tail — the specific framing, e.g.
two cars pinning a pedestrian between them, is the natural reading but not itself printed anywhere as a
string). A new, previously undocumented flag bit was also found: on a successful kill, `C+0x1da7` bit
`0x02` is set — HYPOTHESIS, a "crushed/pancaked" marker, not independently confirmed. Full detail:
`scratchpad/weapons_fire_damage_and_player_blip_2026-10-08.md`.

## 7. Open items

1. **The exact "HP reached 0, begin destruction" decision instruction was still not located, now
   after opening `0x007047a0` and `0x00a9f4f0`'s own full body (2026-10-11, continued) in addition
   to all six previously-named candidates.** `0x00a9f4f0`/`0x00a9c1f0` apply damage and clamp HP but
   never check for zero or call anything destruction-related (unchanged). **CONFIRMED by full
   disassembly: `0x007047a0` is NOT the decision instruction and NOT an HP write** — it is a
   generic 22-instruction event-broadcast primitive (looks up and calls one function-pointer-table
   entry, then unconditionally walks a fixed 28-slot registered-listener list calling each flagged
   listener's own vtable `+0x68`), not vehicle-specific, zero hits for `+0x1564`/`+0x155c`/`+0x1560`
   anywhere in its body. **CONFIRMED by full disassembly: `0x00a9f4f0`'s own body, past its
   `+0xbd0==1` bifurcation, never reads or writes `+0x1564`/`+0x155c`/`+0x1560` either** (the only
   hit for any of these offsets in the complete function is the already-known `+0x16c8`
   destroyed-bit test at the entry gate) — the HP write-back for the complex path does not live in
   `0x00a9f4f0`'s own instructions. Past the dispatch group, its complex path also calls a
   previously-unseen validity/component-handle chain (`0x0091f5e0`/`0x00ab50c0`/`0x0096f4f0`/
   `0x00971cf0`/`0x00950550`/`0x00943ea0`/`0x007f0610`, none opened) and, when the `0x00a9d070`
   eligibility gate passes and a position vector is present, `0x00a9c5d0`/`0x00a9f430`/`0x00a9b890`
   plus (conditionally) `0x00b19510(vehicle)`/`0x0045beb0`/`0x00aaebf0(vehicle)`/`0x00a9b910` —
   none of these were named before this pass. **`0x00a9f430` opened and ruled out**: iterates a
   short list calling `0x00a9ee70` per qualifying entry, shaped like per-connected-player/occupant
   targeting (same role-family as `0x00a9dbe0`'s player walk), no HP touch. **`0x00a9b890` opened
   and independently re-verified against the live Ghidra server before splicing (matched exactly)
   — the first confirmed "reached zero" check found anywhere in this investigation, but not on the
   vehicle's own HP field:** it iterates a list of per-vehicle *part* object pointers, calls
   `0x00a8fd70` on each, then tests `entry+0xf0` bit `0x40` AND `entry+0x10 == 0` to set a boolean
   it returns — `+0x10` here is a field on the *part* object, not the vehicle's own `+0x1564`.
   **`0x00a8fd70` opened in full (~280 instructions) — CONFIRMED to be the real per-part
   damage-apply/clamp routine**, the part-level structural analog of `0x00a9c1f0`: decrements the
   part's own HP-like counter at `part+0x10` by the clamped damage amount and re-clamps into `[0,
   part+0xc]` (the part's max), where the vehicle argument reads the same flag bytes `0x00a9f4f0`
   itself uses (`+0x1d7a`/`+0x1d7b`/`+0x1544`/`+0x135c`/`+0x1358`/`+0xbf8`). **However, this
   function does not itself call anything destruction-shaped on the zero case** — four separate
   category/damage-type-gated branches explicitly floor the result back to `1` after the decrement
   (hard-protecting those categories from ever reaching literal zero through this path), and the
   one path that can leave it at literal `0` just returns a status value with no destruction call;
   `0x00a9b890`'s own zero-check likewise only returns a boolean, it does not branch into a
   notify/spawn-wreck/explosion call either. That boolean flows back into `0x00a9f4f0`'s own
   local, passed into the already-fully-documented `0x00a9dd40`, confirmed in the prior pass to
   use it only for camera-shake/rumble feedback scaling, not destruction.

   **`0x00a9c5d0` opened in full, 2026-10-11, and RULED OUT** — it is a nearest-part spatial
   query: walks the part list (skipping entries with `+0xf0` bits `0xe00000` set), computes a
   bounding-box-clamped squared distance to an input position vector via `0x00a8c0d0` (unopened
   leaf), tracks the closest match, applies a small gated distance bias, and fills an output array
   with qualifying part pointers, writing the single closest one to its own out-param. No HP field
   touched, no zero-check, no destruction-shaped call — this is the "find the part(s) nearest the
   hit position" step feeding which parts get damaged, not a decision site.

   **`0x00a8f910` opened in full, 2026-10-11 — closer to the decision but still a dead end as
   traced, correcting the prior guess about its own signature.** It is a thiscall-style method on
   the PART object itself (`ECX` = part pointer, not "vehicle+damage-type+3 vectors without the
   part pointer" as previously guessed — all its field accesses, `+0x10`/`+0xc`/`+0xf0`/`+0x80`/
   `+0x6c`, are part-object fields). `0x00a8fd70` calls it at `0x00a90355`, but ONLY on the
   immediate-apply branch (gated by a mode check, `0x00755310`-family) — on the deferred branch it
   instead queues into the global ring buffer and never calls `0x00a8f910` at all that tick.
   `0x00a8f910`'s own body: if not paused and `part+0x10 == 0`, calls `0x00a8f630` (also opened)
   and returns its result. `0x00a8f630` is destruction-adjacent: for protected categories it
   floors `part+0x10` back to `1` and signals "not a real death"; for unprotected parts it instead
   resolves up to 8 nearby linked objects via the standard handle-table lookup and applies a
   scaled knockback impulse to each (a genuine "part just broke, push nearby things away" side
   effect) and signals "real zero." Separately, gated on HP FRACTION `< 0.5` (half health, not
   zero — a different, weaker condition), `0x00a8f910` sets a graduated "half-damaged" status bit
   (`part+0xf0 |= 0x8000000`, or `part+0x6c = 1` for one physics-object type) — not a
   full-destruction flag. **However: `0x00a8fd70` has exactly two callers in the whole binary
   (`0x00a9b890`, already documented, and `0x00ab76f0`, a fixed "kneecapper" damage-type wrapper
   ruled out separately below), and BOTH discard `0x00a8fd70`'s return value** — `0x00a9b890`
   overwrites `EAX` immediately by re-reading the part's own fields directly, never touching the
   call's actual return value; `0x00ab76f0` never tests it either. **The HP==0 signal
   `0x00a8f910` computes is never consumed by a return-value branch through EITHER of
   `0x00a8fd70`'s two callers** — if a destruction decision is driven by this chain at all, it
   must be via the status-bit side effects above (readers unidentified) or
   `0x00a8f630`'s impulse side effect itself, not a branch on the call's result.

   **Correction to the above, independently found and re-verified against the live Ghidra server
   myself before trusting it: `0x00a8f910` actually has a SECOND caller the prior pass's own xref
   read missed, `0x00a8fbd0`** (not `0x00ab76f0`, which is a real, separate, unrelated caller of
   `0x00a8fd70` itself — a fixed "kneecapper" damage-type wrapper, correctly ruled out). Opening
   `0x00a8fbd0` in full: it iterates a count at `0x0271af40` over array entries at `0x0271af80`
   (stride-0x40 records) calling `0x00a8f910` once per entry, with a lock-check gate
   (`0x00853b10`) — **these are exactly the same addresses already flagged as "the deferred-queue
   consumer of `0x00a8fd70`'s global ring buffer," unidentified until now.** `0x00a8fbd0` IS that
   consumer: for each queued part-damage event it calls `0x00a8f910`, then does a position/handle
   dedup check against a 128-entry history ring buffer (capped `0x80` entries at `0x0271a940`) to
   avoid double-recording the same event, before advancing. The branch immediately after the
   `0x00a8f910` call (`JZ 0x00a8fc3b`) does NOT test the call's own return value — `EAX` is
   reloaded from the queue entry's own fields first — so this specific branch isn't the
   destruction decision either, but the deferred-queue consumer's own identity is now CLOSED.
   **Pass 4 (2026-10-11, agent `a95d910e53a8b3088` + self, direct query): the `part+0xf0` bit
   `0x8000000`/`part+0x6c` reader lead is now PARKED, no reader found.** A thorough search (raw
   literal-scan across `.text` for `0x08000000` and several plausible combined-mask variants,
   filtered to the vehicle/part address neighborhood `0x00a80000`-`0x00ae0000`, every in-neighborhood
   hit individually dumped and checked) found a **second setter** of the same bit —
   `0x00a8e670` (independently re-verified by direct query, body confirmed) — but no reader
   anywhere. `0x00a8e670` gates on `part+0xf0` bit `0x4` set, bits `0x300000` clear, and the part's
   type-enum (`*(part+0x80)+0x44`) not in `{0xb,0xc,0xd,0x19,7,0x1a}`; when called with its 3rd
   argument (an activation flag) set, it runs a crush/pin geometry test (`0x0075c880`) and sets
   `part+0xf0 |= 0x8000000` only
   if that test passes (`0x00a8e7d8`), otherwise — and on every call where the flag is clear — it
   *clears* `0x8000000` and sets `0x100000` instead (`0x00a8e856`-`0x00a8e86e`), then runs the same
   HP-fraction-`<0.5` check item 1 already documents via `0x00a88eb0`. This reads as a one-frame
   transient flag (set, then cleared almost immediately), which is the right shape for a destruction
   trigger, but its actual reader was not found. `part+0x6c` likewise has only the two known writer
   sites (both inside/inlined from `0x00a8f910`), no reader found anywhere. **Confirmed coverage
   gaps, left open rather than closed:** `0x00a8e670`'s three other callers (`0x00a8e8b0`,
   `0x00a785e0`, `0x00a8eff0`) were not dumped; roughly 300 raw-literal hits for `0x08000000` outside
   the `0x00a80000`-`0x00ae0000` neighborhood were not individually opened. **Decision: PARKED per
   skip-and-defer** — this investigation has now run 4 passes / ~15 functions opened without finding
   the destruction-decision site; Team B can already implement the part-damage chain as specced.
   If resumed later, start with `0x00a8e670`'s 3 unopened callers.
   Remaining unopened names, lower priority: `0x00b19510`/`0x0045beb0`/`0x00aaebf0`/`0x00a9b910`
   (named earlier); `0x00a9e1d0`'s own two callers (`0x00a9eb60`; the tiny thunk `0x00aa04e0`, whose
   own single caller `0x00896183` was not traced); and `0x00a9c230`/`0x004d7f30`/`0x009dd230`/
   `0x00979b30` (named earlier, still unopened).

2. **`0x00a9f4f0`'s complex (multi-part) damage-distribution path — item 1's decision confirmed to
   live somewhere past `0x00a9b890`'s part-level HP-zero check, not in any function opened so far.**
   **New structural finding (CONFIRMED by disassembly): vehicle parts carry their own HP-like field
   at `part+0x10` (max at `part+0xc`), structurally parallel to but entirely distinct from the
   vehicle's own `+0x1564`/`+0x155c`/`+0x1560` triple** — clamped by `0x00a8fd70` using the same
   bounded-accumulator shape as `0x00a9c1f0`, with the only confirmed zero-check anywhere in this
   whole investigation sitting in `0x00a9b890` immediately after. That check produces a boolean,
   not a call — so the actual "begin destruction" action is now narrowed further. **`0x00a9c5d0`
   and `0x00a8f910` are now both opened and ruled out** (see item 1 above: the former is a
   nearest-part spatial query, the latter's HP==0 signal is never consumed by a return-value
   branch through either of its own two callers). **The deferred-queue consumer is now also
   identified: `0x00a8fbd0`** — confirmed to operate on the exact ring-buffer addresses
   (`0x0271af40`/`0x0271af80`) already flagged as that unidentified consumer, but its own branch
   after calling `0x00a8f910` does not test the call's return value either, so it doesn't close
   the decision question. **Pass 4 update:** the `part+0xf0`/`part+0x6c` reader lead is now PARKED
   (see item 1's pass-4 note above) — no reader found despite a thorough scan, though a second
   setter (`0x00a8e670`) turned up with 3 unopened callers as the natural resume point if this is
   picked back up. This investigation is PARKED overall per skip-and-defer (4 passes / ~15
   functions opened, decision site not found); Team B can already implement the part-damage chain
   as specced. If resumed, remaining candidates are: `0x00a8e670`'s 3 unopened callers
   (`0x00a8e8b0`, `0x00a785e0`, `0x00a8eff0`); or `0x00a9f4f0`'s own remaining still-unopened calls
   (`0x00b19510`, `0x0045beb0`, `0x00aaebf0`, `0x00a9b910`). The four original dispatch functions'
   roles, below, are unchanged from the prior pass — none reads HP, none writes HP, none branches
   on a zero/threshold check:
   - **`0x00a9c6f0`** — event/notification dispatch only (builds and fires an event-code-`0x42`
     record through a ~10-call chain). No state mutation relevant to damage or destruction.
   - **`0x00a9d070`** — the real per-part **damage-type eligibility and amount-scaling gate**:
     applies invulnerability, difficulty/player-identity multipliers (via `0x00a9c070`/
     `0x00a9c130`), a part-susceptibility float (`+0x1cc0`), a damage-type-specific scale/suppress
     (`+0x1d80`/`+0x1d7a` bit `0x8`), a falloff float (`+0x1584`), a time-scaled falloff
     (`0x00ad3200`/`0x00d9e250`), and a final per-damage-type applicability switch against external
     descriptor flags (`+0x10` bits `0x800`/`0x1000`). Bails early if the scaled amount reaches `0`
     — a *filter*, not a destruction trigger.
   - **`0x00a9dbe0`** — a **per-part replication/notify dispatcher**: conditional calls to
     `0x004d7f30` and `0x00a9c230` gated on type-table flags, a seat-resolution loop, and a walk
     over the global connected-player list calling `0x009dd230` for matching entries (shaped like a
     per-player targeting/proximity notify) — then a final single occupant notify via
     `0x00979b30`.
   - **`0x00a9dd40`** — the actual **apply-and-feedback orchestrator**: resolves target/instigator
     and calls `0x007047a0` — **now opened and ruled out as a destruction site (above): a generic
     event-broadcast primitive, no HP touch** — optionally plays a sound/VFX (`0x007b4b80`), then —
     gated by the shared `0x008addb0` pause/suppress predicate — reads the vehicle's
     `+0x1564`/`+0x155c` HP fields purely to compute a damage-fraction ratio driving occupant and
     nearby-observer
     camera-shake/rumble (`0x00e0cfb0`/`0x00e0ce20`/`0x00e0ce00`). This is the only one of the four
     that reads current HP at all, and it is confirmed feedback/FX scaling, not a decision gate.

   **Verdict: this whole dispatch group computes, filters, and scales a damage amount, applies an
   impulse, fires notifications, and drives rumble feedback — it does not decrement HP and does not
   decide destruction.** The HP write-back for the complex path, and item 1's decision instruction,
   must live elsewhere: most likely directly in `0x00a9f4f0` past its bifurcation, or in
   `0x007047a0` (highest-priority new lead) or one of this group's other unopened callees
   (`0x00a9c230`/`0x004d7f30`/`0x009dd230`/`0x00979b30`).
3. The `+0x16c8` bit `0x1` set/clear tension against `vehicle_is_destroyed`'s own documented read (§2) —
   flagged, not resolved.
4. Whether `+0x1d7d` bit `0x20` and `+0x1d7e` bit `0x8` correspond to the two still-undocumented Lua
   natives named in a stopped prior tranche (`vehicle_set_invulnerable_to_player_explosives`,
   `vehicle_disable_explosion_and_damage_vfx`) — HIGH-confidence candidates only, by shape/position.
5. ~~A dozen-plus leaf calls inside `0x00a9e1d0` named but not opened (`0x00af9030`, `0x00a7f850`,
   `0x008cc930`, `0x00722000`, `0x00a88770`, `0x008a8640`, `0x004d47e0`, `0x00a88b00`, `0x009e2ac0`,
   `0x00a7fef0`, `0x00ab9030`/`0x00abae80`/`0x00ab9800`) — each individually small, none load-bearing
   for this file's own verdicts.~~ **CLOSED 2026-10-10, all 13 triaged by direct disassembly
   (bounded one-level pass):**

   | Address | Characterization |
   |---|---|
   | `0x00af9030` | Per-seat (8 slots) instigator-handle resolver: for each occupied seat, resolves a handle via the shared capability-table/pause-gate chain and, if it passes, calls `0x00af6310` — a per-seat "notify this seat on destruction" dispatcher. Pure fan-out, writes nothing to vehicle HP/state. |
   | `0x00a7f850` | Distance/LOD-style damage-stage selector: computes camera/view distance, binary-walks a per-vehicle float table to pick an index, stores it, and — only if the index changed — calls `0x008cc930`. No HP read, no destruction decision. |
   | `0x008cc930` | Simple status-bit setter: ORs a flag into `+0x74`, calls `0x008cc4e0` (unopened, presumably a "flags changed" notify). |
   | `0x00722000` | Global pause/replay-state predicate (two shared statics, 90+ other callers elsewhere) — not vehicle-specific. |
   | `0x00a88770` | Simple getter feeding the part-detachment "qualifies to break" bitfield (§5 pass 1), as already documented there. |
   | `0x008a8640` | Simple getter, also feeds §5 pass 1's "qualifies to break" test, as documented. |
   | `0x004d47e0` | Trivial bit-test getter (30+ callers project-wide) — generic, nothing vehicle-specific. |
   | `0x00a88b00` | Conditional notify: fires a single event/message dispatch (code `0x1b`) if a nested pointer chain is valid. |
   | `0x009e2ac0` | Kill-attribution/notification dispatcher — confirms §2 step 12's own "plausibly a final kill-attribution/notification call" guess as CONFIRMED shape, not just plausible. |
   | `0x00a7fef0` | **Vehicle wreck-list lifecycle management, not a trivial leaf** — unlinks the vehicle from one global list and splices it into a different global list (`0x027c2eb4`), clears a status bit, and arms randomized despawn/cleanup timers (`45000`-`75000`ms and `2000`-`3000`ms ranges via the shared timer primitive); re-sets the "destroyed" latch bit near its own end (refining §2 step 13's phrasing — the latch is re-set here, not only by `0x00a9e1d0` directly). Doesn't touch HP or the destroy decision — post-decision wreck bookkeeping, likely governing despawn/respawn-recycle timing. |
   | `0x00ab9030` | Scalar getter off the vehicle's physics-proxy sub-object (mass or similar), gated on `+0xbd0==1`. |
   | `0x00abae80` | Vector transform through the physics proxy (quaternion/matrix-shaped), same gate. |
   | `0x00ab9800` | Vector-pair dispatch through a virtual call on the physics proxy — shaped like "apply an impulse/force at a point," consistent with the §2 step 11 "secondary physics kick" hypothesis. |

   **The spec's "none load-bearing" framing is CONFIRMED** — none of the 13 read current HP, write
   HP, or make a destroy/no-destroy branch. Two (`0x00a7f850`, `0x00a7fef0`) are meaningfully larger
   than "individually small" implied; `0x00a7fef0` in particular is a real wreck-list/despawn-timer
   subsystem worth its own line item if wreck cleanup/respawn timing ever becomes a topic of
   interest. The deeper callees these 13 reach (`0x008cc4e0`, `0x00ac2380`, `0x00aa2be0`,
   `0x00abb580`, `0x00b19140`, the timer primitive, `0x00af6310`, `0x00c14790`, `0x00c150c0`,
   `0x005247f0`, the three destruction-category predicates, `0x009daad0`, `0x00608550`, etc.) were
   NOT opened — this was a one-level triage, not a deep trace.
6. Whether a shader/texture-driven progressive dent/deformation system exists was not independently
   verified (would require opening the vehicle material-blend consumer, out of scope).
7. `vehicle.xtbl`'s own `Death_Explosion`-equivalent field (if any) was not reconciled against the
   runtime `[[vehicle+0xbf4]+0x7fc]` explosion-definition pointer found in §4 — `spec-vehicle-data.md`
   does not currently document a field at that offset.
8. **RESOLVED 2026-10-08, cross-reference: the vehicle collision→damage bridge is now found and named**
   (`spec-physics-runtime.md` §10a). The Havok contact-listener's deferred-queue type-pair dispatch
   table registers `0x00763400` (reads the vehicle's own cached bounding box, vtable `+0x60`, to scale
   a damage amount by vehicle size combined with a speed/time term) as the handler for `(7,7)`
   [vehicle-vs-vehicle, via `0x007642f0`, which calls `0x00763400` once per vehicle side] and
   `(7,0xff)`/`(7,0x3a)`/`(7,5)` [vehicle-vs-non-vehicle, including effectively vehicle-vs-static-world
   via the `0xff` unresolved-object sentinel] alike — `0x00763400` then calls `0x00a9f4f0` (§3 above),
   confirming vehicle-vs-vehicle and vehicle-vs-world collision damage share one mechanism. Still OPEN:
   `0x00763400`'s own exact damage formula and whether its time/speed term traces to the Havok contact
   event's own closing-speed scalar. Full detail: `scratchpad/vehicle_world_collision_response_2026-10-08.md`.

## 8. Confidence summary

**CONFIRMED (full disassembly):** `0x00a9d6c0`'s complete body and its single caller; `0x00a9e1d0`'s
complete body, entry gates, and both of its own callers' immediate context; `0x00a9f4f0`'s entry gates
and simple/complex bifurcation; `0x00a9c1f0`'s complete body; the explosion suppression-gate chain and
its call into `0x00594460`; the part-detachment two-pass walk's existence and shape; the fire/smoke/sound
effect-handle trio's existence and lifecycle shape; the 15-site caller census of `0x00a9f4f0`; the
1-site caller census of `0x00a9d6c0`.

**HIGH CONFIDENCE:** `+0x1628`/`+0x1638`/`+0x15bc` as smoke/fire/engine-sound; `[[+0xbf4]+0x7fc]` as the
vehicle-type's explosion-definition pointer; `+0x1d7e` bit `0x8` as `vehicle_disable_explosion_and_
damage_vfx`'s storage; the part-breakage mechanism as the real code-side deformation signal (vs.
shader-driven fine detail, cross-referenced from the data side, not independently re-derived).

**OPEN:** the literal HP-reached-zero decision instruction; `0x00a9f4f0`'s complex multi-part damage
path; the `+0x16c8` bit `0x1` tension against `vehicle_is_destroyed`; two undocumented Lua natives' exact
bit identities; ~~a dozen named-not-opened leaf calls~~ **CLOSED 2026-10-10, see §7 item 5: all 13
leaf calls triaged, none load-bearing for HP/destruction logic**; whether a progressive HP-ratio-driven
smoke/fire mechanic exists anywhere unreached; `vehicle.xtbl`'s own `Death_Explosion`-equivalent field,
unreconciled.

## 9. Clean-room check

Plain hex addresses throughout; no Ghidra decompiler output quoted. No `FUN_`/`DAT_`/`LAB_` names used as
real identifiers. No leaked Volition/Havok/Audiokinetic/RAD material.

## Changelog

- **2026-10-11 — §7 items 1-2, pass 4: `part+0xf0` bit `0x8000000`/`part+0x6c` reader search —
  PARKED, destruction-decision investigation PARKED overall per skip-and-defer** (SPEC TEAM, agent
  `a95d910e53a8b3088` plus self, direct query). Thorough raw-literal scan across `.text` for
  `0x08000000` (plus several combined-mask variants), filtered to the vehicle/part address
  neighborhood and every in-neighborhood hit individually dumped, found a second setter of the
  bit — `0x00a8e670` (independently re-verified) — but no reader of either `part+0xf0` bit
  `0x8000000` or `part+0x6c` anywhere in the binary. `0x00a8e670` runs a crush/pin geometry test
  (`0x0075c880`) and sets the bit only if it passes; otherwise it clears the bit and sets `0x100000`
  instead, suggesting a one-frame transient flag whose reader was not located. Confirmed coverage
  gaps left open: `0x00a8e670`'s 3 other callers (`0x00a8e8b0`, `0x00a785e0`, `0x00a8eff0`) not
  dumped; ~300 raw-literal hits outside the vehicle/part neighborhood not individually opened.
  Decision, agreed with orchestrator `purrsian-f5`: PARK this investigation — 4 passes / ~15
  functions opened without finding the destruction-decision site is a reasonable budget at current
  usage levels; Team B can already implement the part-damage chain as specced. Cleanroom self-check:
  strict regex 0 hits, C-cast-dereference check 0 hits.

- **2026-10-11 — §7 items 1-2: `0x00a9c5d0` ruled out, `0x00a8f910` signature corrected, deferred-queue
  consumer identified as `0x00a8fbd0`** (SPEC TEAM, agent `a9eaca0e76bb63940` plus self, direct query).
  `0x00a9c5d0` is a nearest-part spatial query (fills a 92-entry output array via `0x00a8c0d0`, no
  HP/destruction touch) — ruled out as a destruction-decision candidate. `0x00a8f910`'s signature
  corrected: ECX is the part pointer (not "vehicle+damage-type+3 vectors without the part pointer" as
  a prior pass guessed); it gates on not-paused + `part+0x10==0`, calls `0x00a8f630` (floors protected
  parts back to 1, or applies an 8-nearby-object knockback impulse for unprotected ones), and separately
  sets a graduated half-damaged status bit on HP-fraction `<0.5` (`part+0xf0 |= 0x8000000` or
  `part+0x6c=1`, depending on physics-object type). `0x00a8fd70` has exactly two callers in the whole
  binary (`0x00a9b890`, documented below, and `0x00ab76f0`, a fixed "kneecapper" damage-type wrapper,
  ruled out separately) — both discard `0x00a8fd70`'s return value, so no return-value branch anywhere
  in this chain drives a destruction decision. Independently re-verified the agent's `0x00a8f910` claims
  by direct query (`scratchpad/verify_a8f910/func_0x00a8f910.txt`) before splicing — this surfaced a gap
  the agent's own report missed: `0x00a8f910` has a *second* caller, `0x00a8fbd0`, beyond the one the
  agent analyzed. Opened `0x00a8fbd0` directly (not via agent; `scratchpad/verify_a8fbd0/func_0x00a8fbd0.txt`)
  and confirmed it is the previously-"unidentified" deferred-queue batch consumer flagged by multiple
  prior passes: it iterates the `0x0271af40` count over the `0x0271af80` array, lock-gated via
  `0x00853b10`, calls `0x00a8f910` per entry, and its own post-call branch tests a reloaded
  position-vector pair (not the call's return value) before a dedup check against a 128-entry history
  ring buffer at `0x0271a940`. Destruction-decision site still not found after 3 passes/~13 functions
  opened; new highest-priority lead is the still-unopened readers of the `part+0xf0` bit `0x8000000` /
  `part+0x6c` status bits. Cleanroom self-check: strict regex 0 hits, C-cast-dereference check 0 hits.

- **2026-10-08 — §7 item 8: the vehicle collision→damage bridge found and named, cross-reference to
  new `spec-physics-runtime.md` §10a** (SPEC TEAM, agent `af81321adb1820dad`). `0x00763400`
  (registered for vehicle-vs-vehicle and vehicle-vs-world type pairs alike in the Havok
  contact-listener's deferred-queue dispatch table) calls `0x00a9f4f0`, confirming both collision
  kinds share one damage mechanism. Full detail: `scratchpad/vehicle_world_collision_response_2026-10-08.md`.
  Cleanroom self-check: 0 new hits.

- 2026-10-08: file created (SPEC TEAM, vehicle damage/destruction dispatch). Closed
  `spec-weapons-runtime.md` §2.4/§4.2's "not fully traced past the entry gate" item for `0x00a9d6c0` with
  a full trace and its single project-wide caller. Discovered and fully traced the previously-unnamed
  real vehicle-destruction trigger, `0x00a9e1d0` (662 instructions). Confirmed vehicle explosions reuse
  the generic `explosions.xtbl`-driven apply routine (`0x00594460`, the same target `explosion_create`
  uses). Established vehicle HP and the generic character damage sink are two structurally independent
  economies meeting only at the occupant layer. Found a new difficulty/team damage-scaling wrapper
  (`0x00aa0510`). Checked explicitly for, and did not find, a progressive HP-ratio-driven smoke/fire
  mechanic or a continuous mesh-deformation system — both reported as honest negatives alongside the
  positive findings (scripted/one-shot VFX; discrete part-breakage). Left open: the literal
  HP-reached-zero decision instruction, `0x00a9f4f0`'s complex multi-part damage path, and the
  `+0x16c8`-bit tension against `vehicle_is_destroyed`. Full detail:
  `scratchpad/vehicle_damage_deformation_fire_explosion_2026-10-08.md`. Cleanroom self-check: 0 hits.
  Synced to Team B.

- 2026-10-08: new §6a — `0x00b19ad0`, a vehicle-vs-vehicle occupant crush/pancake kill, found while
  chasing an unrelated hitscan-weapons lead and confirmed to have zero weapon involvement. A seated
  occupant is killed when their vehicle is crushed into box-overlap contact with another vehicle;
  instant-lethal damage credits the contact vehicle's own driver as instigator. Structurally separate
  from this file's own §1 occupant-kill-on-extract loop and §2-§3's HP-driven destruction economy. Full
  detail: `scratchpad/weapons_fire_damage_and_player_blip_2026-10-08.md`. Cleanroom self-check: 0 new
  hits. Synced to Team B.
- **2026-10-10 — §7 item 5 CLOSED: all 13 `0x00a9e1d0` leaf calls triaged** (SPEC TEAM, agent
  `ab7d28c1e44da6410`). Confirms the "none load-bearing" framing — none read/write HP or make a
  destroy/no-destroy branch. Two (`0x00a7f850`, `0x00a7fef0`) turned out larger than "individually
  small" implied; `0x00a7fef0` is a real vehicle wreck-list/despawn-timer subsystem (global list
  splice plus four randomized timer arms) worth its own future line item. Independently
  re-verified two of the thirteen (`0x00722000`, `0x004d47e0`) against the live Ghidra server
  before splicing (both matched exactly). Cleanroom self-check: strict regex 0 hits, C-cast-
  dereference check 0 hits.
- 2026-10-11 (SPEC TEAM, agent `a26783a030db826f6`): §7 items 1/2 — all four named complex-path
  dispatch functions opened and fully characterized by direct disassembly, plus a 2-hop follow-up
  (difficulty-multiplier helpers). Confirmed none make the HP-zero destruction decision and, more
  specifically, none write the vehicle's own `+0x1564` HP field at all — narrowing the still-open
  decision-instruction search to `0x00a9f4f0`'s own body past its bifurcation, `0x007047a0` (new
  lead), or the group's other unopened callees. Independently re-verified the HP-field read/write
  claim myself by grepping the raw disassembly dumps before splicing. Cleanroom self-check: strict
  regex 0 hits, C-cast-dereference check 0 hits.
- 2026-10-11 (SPEC TEAM, agent `a204f96e7f0a99109`, continued): opened `0x007047a0` (ruled out — a
  generic event-broadcast primitive) and `0x00a9f4f0`'s own full body (confirmed it never touches
  `+0x1564` itself on the complex path). Found a previously-unknown PART-level HP field
  (`part+0x10`, distinct from the vehicle's own `+0x1564`) clamped by `0x00a8fd70` and zero-checked
  by `0x00a9b890` — the first confirmed zero-check found in this entire investigation — but that
  check only returns a boolean, not a call. New named leads: `0x00a9c5d0` (highest priority),
  `0x00a8f910`, the deferred-queue consumer. Independently re-verified `0x00a9b890`'s disassembly
  against the live Ghidra server before splicing (matched exactly). Cleanroom self-check: strict
  regex 0 hits, C-cast-dereference check 0 hits.
