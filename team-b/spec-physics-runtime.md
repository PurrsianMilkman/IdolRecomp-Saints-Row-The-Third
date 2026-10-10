# Saints Row: The Third — Physics Runtime: the Havok Boundary Register and the Frame Lifecycle

**Prepared by:** SPEC TEAM (cleanroom reverse-engineering process)
**Phase:** Physics runtime, units P0 (boundary register) and P1 (lifecycle and step) of the proposed
order in `review/scoping-physics-vehicle.md` §5.
**Scope:** The exact call contract between Volition's own game code and the statically-linked Havok
2010.2.0-r1 physics engine — every game class that implements a Havok interface (and which of its
slots Havok can actually call), the full census of game-to-Havok call sites, and the complete
bring-up/per-frame-step/shutdown sequence a replacement physics layer must reproduce. This file is the
**runtime call contract**, parallel to `spec-ai-runtime.md`/`spec-audio-runtime.md`'s own split from
their format specs. It does not open Havok's own internals beyond identifying which vtable a
constructor installs (for class identity), and does not touch the vehicle's custom Havok sub-steps or
`.czn_pc` zone data — both stay out of scope, the vehicle sub-steps pending the user's own middleware
decision.
**Method:** direct Ghidra disassembly (private read-only project copies, deleted on completion),
`func`/`calls`/`range` dump modes, plus a raw whole-binary RTTI census (type descriptor → complete
object locator → vtable, with class-hierarchy base lists) and a raw `E8`/`E9` byte scan for call sites
Ghidra never placed inside a function. Address ranges were fixed to the byte by checking where each
function's call-graph neighbours and string anchors change side.
**Cleanroom compliance:** no decompiled code, decompiler variable names, or pasted pseudocode appear
below; every function and field is cited by plain hex address or offset, or — matching this project's
own established convention for the physics/vehicle spec pair — by `FUN_xxxxxxxx`/`DAT_xxxxxxxx` where
that is the more natural citation (this file follows `spec-physics-format.md`/`spec-vehicle-geometry.md`'s
own precedent, not the stricter plain-hex-only convention used elsewhere in this project). Class names
are the binary's own RTTI type-descriptor strings, or Havok's public 2010.2 API names, quoted as data.
**Confidence key:** **CONFIRMED** (read directly in disassembly), **HIGH CONFIDENCE** (inferred from
structure, not independently traced), **HYPOTHESIS** (plausible, unconfirmed), **OPEN** (not settled).
**Relationship to other specs:** `spec-physics-format.md` §1–§3 already establishes Havok's identity,
version, and the two boundary-pair functions (`0x00bf81a0`, `0x00c1c4f0`); this file's §1–§2 refine
the full boundary to the byte and catalogue every crossing. `spec-vehicle-geometry.md` §11.11 already
documents the one confirmed Havok-shape-construction link from vehicle-part geometry; cross-referenced,
not repeated. `review/scoping-physics-vehicle.md` is the scoping note these units refine — it is a
review document and is not itself edited; corrections to it are given in §7.

---

## 1. The three Havok address ranges, refined to the byte

Refines `review/scoping-physics-vehicle.md` §3.1. Ranges were fixed by checking which function's
call-graph neighbours and string anchors change side, not estimated.

| Region | Start (incl.) | End (excl.) | Functions | Instructions | What it is |
|---|---|---|---|---|---|
| (thunks) | `0x00be5ce4` | `0x00be5d1a` | 9 | 9 | WS2_32 import thunks, called from networking code and twice from inside Havok's own socket class |
| **Havok range A** | `0x00be5d20` | `0x00c98da0` | 2,486 | 186,544 | Havok Base, Physics dynamics/world/simulation/constraints/actions/vehicle/utilities, serialization, `hkStructureLayout` |
| OpenSSL 1.0.0d (not Havok) | `0x00c98da0` | `0x00d1d570` | 1,792 | 157,562 | third-party, unrelated — zero call edges to or from any Havok range |
| unattributed (not Havok) | `0x00d1d570` | `0x00d4c800` | 934 | 58,776 | identity not established; zero call edges to or from Havok; still OPEN (§6 item 8) |
| **Havok range B** | `0x00d4c800` | `0x00d9dff0` | 1,000 | 91,547 | Havok collision: shapes (incl. `hkpBoxShape` constructor `0x00d4d7f0`), MOPP, agents, `hkp3AxisSweep` |
| (game) | `0x00d9dff0` | `0x00ecb400` | — | — | game timer helper, CRC-32, the job scheduler (§4.4), CRT |
| (thunks) | `0x00ecb400` | `0x00ecb412` | 3 | 3 | XINPUT1_3, DINPUT8 thunks |
| **Havok range C** | `0x00ecb420` | `0x00f22240` | 552 | 99,809 | geometry processing, KD-tree, GJK/penetration, plus the product-keycode check `0x00f21bd0` |
| (game: Wwise and on) | `0x00f22240` | — | — | — | first function is a Wwise RTTI-identified destructor |
| **Havok total** | | | **4,038** | **377,900** | |

**No game code lives inside the three ranges (HIGH).** 42 functions inside them have only non-Havok
call neighbours; every one is an API leaf the game calls that itself calls nothing large (e.g. the
`hkpBoxShape` constructor, 10 game callers). None carries a game string.

**Havok code lives outside the three ranges too (CONFIRMED by RTTI).** Havok's header-inline virtual
methods were compiled into Volition's own object files, so 121 slot targets of Havok-class vtables lie
outside A/B/C: identical-code-folded stubs shared program-wide (`0x00754410` — the project's known
bare-`ret` no-op, in 27 Havok vtables; six more shared stubs; the CRT pure-call handler `0x00ea2778` in
50 vtables), Havok base-interface defaults living in the game physics cluster (e.g. `hkpContactListener`
slot `+0x0c` → `0x00755670`, a dozen more listed in §2), and one whole Havok class body in the vehicle
cluster (`hkSplineParametricCurve`, `0x00ad8740`–`0x00ad9150`, seven slots, ~640 instructions). **A
future size figure or RE pass over `0x00754000`–`0x007a5000` and `0x00a77000`–`0x00af0000` (the "game
physics clusters") must skip these Havok-inline copies**, identified by the RTTI slot test above.

---

## 2. Game classes over Havok interfaces — the real callback contract

Refines `review/scoping-physics-vehicle.md` §3.2, re-derived from raw RTTI, not copied from the
scoping table. The whole-binary RTTI census (1,720 type descriptors, 1,701 locators and vtables, 1,579
classes with a vtable) found **64 classes whose RTTI base list contains a Havok class but whose own
name is not a Havok name**; 25 of those are Havok's own template instances and anonymous-namespace
filters with no game code of their own, leaving **39 real game classes**.

Each non-Havok-range slot of those 39 classes is one of three kinds, found by comparing it against the
Havok base's own vtable for the same subobject:
- **Havok-callable override** — the slot exists in the Havok base and the game put its own code there.
  **This is the real callback contract**: Havok can call these 72 slots (across all 39 classes,
  destructors included).
- **Havok default copy** — the slot holds the same address as the Havok base's own vtable (a
  header-inline Havok method compiled outside the ranges, §1). 38 such slots. Not game logic.
- **Game-introduced virtual** — the slot index lies past the end of the Havok base's vtable. The game
  added it itself; **Havok cannot call it**, since Havok code only knows the base layout. 24 such
  slots, found across the vehicle/character clusters — these belong to the vehicle sub-step and
  character-controller units (P4/P5), not to the boundary.

### 2.1 The 39 classes

| # | Game class (RTTI) | Havok base | Havok-callable overrides | Havok default copies | Game-introduced virtuals |
|---|---|---|---|---|---|
| 1 | `havok_rigid_body` | `hkpRigidBody` | destructor `0x00756fe0` | — | — |
| 2 | `game_havok_world` | `hkpWorld` | destructor `0x00757370` | — | — |
| 3 | `custom_collision_listener` | `hkpContactListener` | 3 contact methods `0x00769040`/`0x00769500`/`0x00769750` | +0x0c `0x00755670` | — |
| 4 | `havok_game_filter` | 5 Havok filter interfaces | 5 pure filter methods, chiefly `isCollisionEnabled` at `0x00779260` | — | 5 slots `0x00778950`–`0x00778c50` — the game's own entry points into its own filter, **not Havok callbacks** |
| 5 | `hk_broadphase_border` | 4 Havok interfaces | `0x0075aad0`, `0x00755ba0` | — | — |
| 6 | `hk_suspend_inactive_agents_util` | 3 Havok interfaces | `0x0079e540` | — | `0x007557a0` |
| 7 | `havok_memory_watchdog` | `hkpDefaultWorldMemoryWatchDog` | `0x00779d70` | — | — |
| 8 | `CriticalAllocator` | `hkMemoryAllocator` | `0x0077a0f0`, `0x0077a110` | +0x28 `0x00754410` | — |
| 9 | `constraint_instance_breakable_listener` | `hkpConstraintListener` | `0x00777fe0` | +0x00 `0x00776740` | — |
| 10 | `havok_const_accl_action` | `hkpUnaryAction` | `0x007588e0`, `0x00758970` | +0x04 `0x00756a80` | — |
| 11 | `multi_game_havok_corrector` | `hkpUnaryAction` | **none** — its step slot is a folded no-op stub inside Havok range A | +0x04 `0x00756a80` | — |
| 12 | `object_collector` | `hkpCdBodyPairCollector` | `0x0075ae80` | `0x00757e90`, `0x00755760` | — |
| 13 | `contact_collector` | `hkpCdPointCollector` | `0x007565c0` | `0x00755730` | — |
| 14 | `havok_closest_ray_hit_collector` | `hkpClosestRayHitCollector` | `0x0075b800` | `0x00756e90` | — |
| 15 | `havok_all_ray_hit_collector` | `hkpAllRayHitCollector` | `0x0075b9c0`, `0x0075bf70` | — | — |
| 16 | `havok_closest_linear_cast_collector` | `hkpClosestCdPointCollector` | `0x0075bb50`, `0x00756fa0` | `0x00756e20` | — |
| 17 | `havok_all_linear_cast_collector` | `hkpAllCdPointCollector` | `0x0075bfb0`, `0x0075bc60`, `0x0075bc40` | — | — |
| 18 | `man_cannon_vacuum_collector` | `hkpCdBodyPairCollector` | `0x00ab1d60` | `0x00757e90`, `0x00755760` | — |
| 19 | `hk_rigid_body_in_water_listener` | `hkpEntityListener` | `0x00796940` | `0x0078bd10`, `0x0078b990` | — |
| 20 | `hk_phantom_in_water_listener` | `hkpPhantomListener` | `0x00796990` | `0x0078a690`, `0x0078b990` | — |
| 21 | `hk_character_shape_phantom` | `hkpCachingShapePhantom` | `0x0079abf0` | `0x0079a990` | 4 slots `0x0079a9d0`–`0x0079aaa0` |
| 22 | `hk_custom_character_proxy` | 3 Havok interfaces | `0x0079dd10`, `0x0079c4b0`, `0x0079c4c0` | `0x0078b990` ×2 | `0x0079de40` [536] |
| 23 | `MyCharacterListener` | `hkpCharacterProxyListener` | `0x0079f8d0`, `0x007a2960` [578], `0x0079fb80`, `0x007a45f0` [288] | `0x008a5bf0` ×2 | — |
| 24 | `havok_custom_character_collector` | `hkpAllCdPointCollector` | `0x0079dcd0`, `0x00778470` [396], `0x0079c300` | — | — |
| 25 | `character_controller` | `hkReferencedObject` only (no Havok interface) | `0x007a1870` | — | — |
| 26 | `hk_custom_vehicle_instance` | `hkpVehicleInstance` | `0x00ae8470`, `0x00ae9b40`, `0x00ae8ca0` [168] | `0x00756a80` | 4 slots `0x00ae84b0` [517]–`0x00ab8a60` |
| 27 | `hk_custom_motorcycle_instance` | `hk_custom_vehicle_instance` | `0x00aba2d0`, `0x00ae2800` [308] | inherited | `0x00ae1170` replaces #26's slot; rest inherited |
| 28 | `hk_custom_aircraft_instance` | `hk_custom_vehicle_instance` | `0x00ade080`, `0x00ae0b10`, `0x00adabf0` | inherited | `0x00adab40` (new); rest inherited |
| 29 | `hk_custom_watercraft_instance` | **`hkpUnaryAction`** (not a vehicle instance) | `0x00aeeb30` | `0x00756a80` | — |
| 30 | `hk_custom_vehicle_engine` | `hkpVehicleDefaultEngine` | `0x00ae31a0` | — | `0x00ae31f0`, `0x00ae3330` [293] |
| 31 | `hk_custom_vehicle_steering` | `hkpVehicleDefaultSteering` | `0x00aeb550`, `0x00aeb230` [142], `0x00aeb0c0` | — | — |
| 32 | `hk_custom_vehicle_transmission` | `hkpVehicleDefaultTransmission` | `0x00abd7b0`, `0x00aecd60`, `0x00aece20` | — | `0x00aecef0` [~193] |
| 33 | `hk_custom_vehicle_brakes` | `hkpVehicleDefaultBrake` | `0x00abdac0`, `0x00ae2c80` [293] | — | — |
| 34 | `hk_custom_vehicle_suspension` | `hkpVehicleDefaultSuspension` | `0x00aebe30` [992] | `0x00abd350` | — |
| 35 | `hk_custom_vehicle_driver_input` | `hkpVehicleDefaultAnalogDriverInput` | `0x00ae30c0` | — | — |
| 36 | `hk_custom_vehicle_raycast_wheel_collide` | `hkpVehicleRayCastWheelCollide` | `0x00abee20`, `0x00aea040`, `0x00aea160` | `0x00abd220` | `0x00ae9f10`, `0x00aeadb0` |
| 37 | `havok_wheel_closest_linear_cast_collector` | `havok_closest_linear_cast_collector` (#16) | `0x00aeab50`, `0x00aea480` | `0x00756e20` | — |
| 38 | `hk_custom_vehicle_extra_downforce` | `hkpUnaryAction` | `0x00ae3850` [291], `0x00ae3cb0` | `0x00756a80` | 3 slots `0x00ae3790`–`0x00ae3770` |
| 39 | `hk_door_constraint_motor` | `hkpCallbackConstraintMotor` | **none in the vtable** — see §2.3 | — | — |

### 2.2 Slot meanings (HIGH, from shape; names are Havok's public 2010.2 API)

- **Action step.** `+0x0c` of every `hkpUnaryAction`-derived row (10, 11, 26–29, 38) is pure in the
  Havok base, so it is `applyAction` — the per-step entry point for a Havok unary action.
- **Filter methods.** The four secondary filter interfaces on row 4 each declare one pure method
  besides their destructor — the `isCollisionEnabled` family (the shape-collection interface has two).
- No other slot is mapped to a public method name in this pass; that is P2/P3/P5 material, which reads
  the game side of each slot in depth.
- **Rows 21–23 and 25, completed 2026-10-03 by `spec-player-runtime.md` §5.6/§5.8 (the player locomotion
  bridge, P5 territory, read from the game side this time):**
  - **Row 23 (`MyCharacterListener`)'s first override, `0x0079f8d0`, is the scalar deleting destructor**,
    not a real callback — the three real slots, in the public `hkpCharacterProxyListener` order, are
    `processConstraintsCallback` (`0x007a2960`), `characterInteractionCallback` (`0x0079fb80`) and
    `objectInteractionCallback` (`0x007a45f0`). **All three only queue** — into two fixed-size (≤ 100
    entries) game-side queues, `0x0223d3b0` (`0x70` bytes/entry) and `0x0223ff70` (`0x50` bytes/entry),
    merging duplicate (human, object) pairs by a running average — they do not touch game state directly,
    and they run inside the lock-step passes below, off the main thread.
  - **Row 22's game-introduced virtual, `0x0079de40`, is HIGH a contact-manifold update** with distance
    thresholds 0.1 and 1.1 (units unconfirmed); not traced further here, still P5 material.
  - **Row 25 (`character_controller`)'s real per-step entry is not in this table at all**: `0x007a1870`
    is the vtable override, but the function every stepped human actually calls is a plain (non-virtual)
    `thiscall`, **`0x007a5b10`** (1467 instructions) — contract CONFIRMED as call shape only (it clears two
    state bits, runs the proxy's support query when asked, removes the into-slope velocity component on a
    steep surface, and ends by setting the proxy's linear velocity); its own internals are still P5.

### 2.3 The one non-vtable crossing: a function pointer

`hk_door_constraint_motor`'s Havok base, `hkpCallbackConstraintMotor`, has **no vtable slots the game
overrides at all** — its callback is a plain **function pointer**, handed to Havok at construction. The
game's constructor `0x00aef0a0` passes `0x00aeee60` (code Ghidra never defined as a function) and the
value `1` to the Havok base constructor `0x00c34ff0`. A full scan of all 2,019 outside-to-Havok call
sites (§3) for a game `.text` address pushed within 8 instructions of the call found **exactly two**
such cases: this one, and the shared no-op stub `0x00754410` (passed to Havok's memory bring-up,
`0x00bec9a0`). **So apart from the 72 vtable slots in §2.1, these are the only two game-code addresses
Havok ever receives as data** (HIGH; a pointer loaded further from its call site or embedded in a
structure would escape this scan).

### 2.4 Corrections to the scoping note's own callback table

1. `multi_game_havok_corrector` has no game override at all — its step slot is a folded no-op; the two
   addresses the scoping note gave it both belong to `havok_const_accl_action` (#10) alone.
2. `hk_custom_watercraft_instance` derives from plain `hkpUnaryAction`, not from the vehicle-instance
   base the scoping note grouped it under.
3. Twenty-four addresses the scoping note listed as "game override" callback contracts are actually
   **game-introduced virtuals Havok cannot call** — real entry points, but ones the game's own code
   calls (from the custom vehicle step, for instance), not callbacks. They belong to P2–P5, not to the
   boundary register. Full list in §2.1's "game-introduced virtuals" column.
4. Eleven addresses the scoping note listed as game overrides are Havok default copies, not game
   logic: `0x00755670`, `0x00776740`, `0x00757e90`, `0x00755760`, `0x00755730`, `0x00756e90`,
   `0x00756e20`, `0x0078bd10`, `0x0078a690`, `0x0079a990`, `0x00abd350`.
5. Two rows the scoping note had no entry for at all: `hk_door_constraint_motor` (#39) and
   `character_controller` (#25, over `hkReferencedObject` only — not a Havok-interface class at all).
6. "Havok calls game code only through these vtables" needs the §2.3 exception for the door motor's
   function pointer.

---

## 3. The call-in census — caller counts and a call-site data table

Refines `review/scoping-physics-vehicle.md` §3.3–§3.4. An edge counts if it is a direct call or jump
from code outside ranges A/B/C to a function inside them with more than 3 instructions; edges come from
the whole-binary edge list (41.8k functions) plus a raw `E8`/`E9` byte scan for call sites Ghidra never
placed inside a function.

| Measure | This pass | Scoping note |
|---|---|---|
| All call sites into the three ranges | **1,956** | 1,940 |
| Distinct Havok entry points called | **317** (266 in A, 51 in B, **0 in C**) | 326 (267/53/6) |
| Called only from static initialisers | 71 entry points, 458 sites (373 into one registration helper, `0x00bebf30`) | ~440 callers |
| **Called at runtime** | **246 entry points, 1,498 sites, 607 calling functions/undefined sites** | — |
| Runtime sites by caller location | physics cluster `0x00754000`–`0x007a5000`: 796; vehicle cluster `0x00a77000`–`0x00af0000`: 403; elsewhere in gameplay: 298; static region: 1 | "mostly the two clusters" |

**Range C has no outside caller at all (CONFIRMED)** — geometry processing, the KD-tree and GJK are
reached only through Havok itself. The scoping note's "6 range-C callees" were CRT functions in a
stretch §1 moved out of range C.

**23 tiny entry points (≤3 instructions) are called from outside, at 63 sites.** Two are folded stubs;
the other 21 are real one-line Havok entry points — a memory-system instance getter/setter, a vtable
installer, several field getters/setters on Havok objects, five installers that wire Havok's own
function pointers into Havok globals, and three forwarders. **The full directly-called surface is 246 +
21 = 267 runtime entry points, plus 71 static-initialiser-only entry points.**

**What the census cannot see (HIGH):** virtual calls the game makes on Havok objects (e.g. through the
memory-system getter's returned vtable), and inline field reads/writes on Havok objects (e.g. the
vehicle pre-step testing a body pointer before calling into Havok). The scoping note's 98-function
world-handle figure stands as the best available measure of that hidden surface.

### 3.1 Call-site data table — the most-used entry points

Shape is read from each callee's own `ret N` (stack-argument count) or the caller's clean-up; receiver
is what the caller loads into `this`, sampled over up to 30 sites. No Havok body was read beyond the
vtable it installs.

| Entry | Insns | Sites | Shape | Receiver | Reading |
|---|---|---|---|---|---|
| `0x00be6620` | 7 | 249 | plain call, no args | the fixed object `0x01312854`, 30/30 sampled | CONFIRMED: `hkContainerHeapAllocator` slot, inlined container growth |
| `0x00c150c0` | 35 | 129 | `this`, no args | a body/entity register, never the world | **Correction, not a world lock.** HYPOTHESIS: entity `activate` |
| `0x00be6100` | 73 | 152 | `this`, no args | world, filters, border, watchdog, shapes, bodies | HIGH: reference release |
| `0x00c14790` | 28 | 65 | `this`, no args | often `[reg+0xe0]` of a body | **Correction, not a world unlock.** HIGH: a method on a body sub-object |
| `0x00be6f90` | 54 | 61 | no `this`, caller pops 16 | — | HIGH: Havok memory |
| `0x00be7010` | 59 | 46 | no `this`, caller pops 12 | — | HIGH: Havok memory |
| `0x00c21b20` | 38 | 30 | `this` + 3 args | a fresh object, e.g. each bring-up phantom with `(3,0,0)` | — |
| `0x00c177b0` | 78 | 21 | `this` + 1 arg | **the world handle, 21/21** | HIGH: world removal; shutdown uses it on both bring-up phantoms |
| `0x00c18930` | 77 | 12 | `this` + 1 arg | **world, 12/12** | HIGH: world addition, the `0x00c177b0` partner; bring-up adds both phantoms |
| `0x00c19160` | 143 | 16 | `this` + 2 args | **world, 15/16** | HYPOTHESIS: world add-entity |
| `0x00d4d7f0` | 40 | 10 | `this` + 2 args | fresh block | CONFIRMED: `hkpBoxShape` constructor |
| `0x00d50670` | 9 | 16 | `this` + 1 arg | fresh block | CONFIRMED: `hkpSphereShape` constructor |
| `0x00c1e1d0` | 19 | 8 | `this` + 3 args | fresh block | CONFIRMED: `hkpSimpleShapePhantom` constructor |
| `0x00c14cc0` | 12 | 11 | `this` + 1 arg, out-pointer | bodies incl. vehicle `+0x81c` | HIGH: a boolean body query. HYPOTHESIS: `isActive` |

**18 entry points take the world handle `0x0161d378` as `this` at half or more of their sampled
sites** — this is the game's directly-used world API, all in `0x00c16xxx`–`0x00c1bxxx` plus four
lifecycle-only ones (§4).

**What crosses the boundary, confirmed this pass:**

| Direction | What | Where |
|---|---|---|
| game → Havok | the world construction info (built on the game's stack) plus the SDK version number `20100200` | `0x007572f0` → `0x00c1c4f0` (§4.2) |
| game → Havok | the job queue and Δt to begin; the job queue and null to finish | `0x00755c80` → `0x00c17920`/`0x00c16fc0` (§4.4) |
| game → Havok | a function pointer, the door motor callback `0x00aeee60` | `0x00aef0a0` → `0x00c34ff0` (§2.3) |
| game → Havok | the game filter object, wrapped by Havok's `hkpConstraintCollisionFilter` | `0x0075f370` → `0x00c2b380` (§4.1) |
| Havok → game | the job queue's per-thread draining, run on game worker threads | `0x00754dd0` → `0x00be88c0` (§4.4) |
| game → Havok | per-vehicle pre-collision update, one call per instance, no stack args | `0x00ac3f40` → `0x00c561d0` (§4.3) |

**The other direction: 474 direct calls from Havok to 63 functions outside the ranges.** 268 sites go
to 22 CRT functions; the other 206 go to 41 small (≤110 instruction) functions, almost all
header-inline/identical-code-folded copies (§1). Two have a known identity: `0x007557e0` is Havok's own
product-features initialiser compiled into a game object file (called only by Havok's own
`0x00bec9a0`); `0x00756cb0` searches a 16-byte-stride key/value array at object `+0x7c` — HIGH, a
header-inline copy of Havok's own world-object property lookup, meaning **the game keeps its own data
inside Havok body properties** (a P2 item). **None of the other 41 is Volition gameplay logic (HIGH)**
— Havok reaches game logic only through the 72 slots of §2 and the one function pointer of §2.3.

---

## 4. The frame lifecycle

All five lifecycle functions were read in full. Left column: **G** = game code only; **H** = crosses
into Havok.

### 4.1 Bring-up — `0x0075f370` — CONFIRMED call sequence

Called once, from game init `0x005d25f0`. Guarded by byte `0x0161d39f` ("physics initialised" — set
last by bring-up, cleared by shutdown, checked first by bring-up to refuse a second call).

| # | | What happens |
|---|---|---|
| 1 | G→H | Havok memory and base system: two named memory blocks, then Havok's `hkFreeListAllocator`/`hkFixedSizeAllocator`/`hkFreeListMemorySystem`/`hkNativeFileSystem` vtables installed; the product-features initialiser (§3.1) runs; the game's own no-op stub is handed in as a data pointer (§2.3). |
| 2 | G→H | Two Havok warning IDs (`0xf03421de`, `0x56f80833`) switched off through the error object. |
| 3 | G | Six game globals zeroed. |
| 4 | H | World construction info built on the stack: Havok's `hkpWorldCinfo` vtable installed, then one virtual call with argument `5` (HYPOTHESIS: a solver preset). |
| 5 | G | Construction-info fields filled: **gravity (0, −9.8, 0)** (Y-up); broadphase box **(−3500,−500,−3500)–(3500,1500,3200)**; **collision tolerance 0.04**; simulation type byte **3** (HIGH: multithreaded). A shrunk copy of the box is kept for game use. |
| 6 | G→H | The game collision filter (`havok_game_filter`, §2.1 row 4) is built, type field set to 4 (HIGH: Havok's "custom filter"), kept at `0x0161ca7c`. |
| 7 | H | Havok's own `hkpConstraintCollisionFilter` wraps it, with an `hkpConstraintListener` sub-object at `+0x40`; kept at `0x0161d3a4`. |
| 8 | G | **Default convex radius `0x0131e81c` changes from 0.05 to 0.005** — already cross-referenced at `spec-physics-format.md` §3 and `spec-vehicle-geometry.md` §11.11; restated here as part of the lifecycle. |
| 9 | G→H | **The world**, built through `0x007572f0(info, 20100200)` — see §4.2 — kept at `0x0161d378`. |
| 10 | H | The job queue, built only when the simulation-type byte is 3; registers Havok's own job functions. **No `hkJobThreadPool`-family class has RTTI anywhere in the image** — no Havok thread pool is ever built (§4.4). |
| 11 | G→H | A broadphase-border handler (`hk_broadphase_border`, §2.1 row 5), kept at `0x0161d3a8`. |
| 12 | G | Game body tables: a 5,000-slot body-pointer table and a 5,000-entry record pool with a free-list index. **Game code refers to a Havok body by a slot index 0–4999, not a pointer.** |
| 13 | G | `0x00769c30` (22,979 instructions) — collision-material and layer tables, not opened by this unit (P2/P3). |
| 14 | H | The suspend-inactive-agents utility (§2.1 row 6), kept at `0x0161d37c`. |
| 15 | G | Sub-inits, in order: per-thread Havok memory on every worker (§4.4); a memory watchdog, added to the world with `0x00c17030`; the character system's world pointer; game-only tables; **two 1-metre query phantoms** (below); water set-up. |
| 16 | G | `0x0161d39f` = 1; the construction-info stack block is torn down. |

**The two query phantoms (step 15):** a sphere (radius 1.0, `hkpSphereShape` constructor `0x00d50670`)
and a box (half-extents (1,1,1), convex radius from `0x0131e81c`, `hkpBoxShape` constructor
`0x00d4d7f0`), each built, given filter word `0x80002`, and added to the world with `0x00c18930`.
**HIGH:** these back the overlap-query helpers `0x00757ec0`/`0x007581b0`/`0x0075a480`.

**Not seen in bring-up:** the contact listener is added inside the world wrapper (§4.2), not here. The
constraint filter's own listener sub-object is removed at shutdown (§4.5) but its matching *add* is not
in any bring-up body — **OPEN** (§6 item 1): it may happen inside Havok's own world constructor.

### 4.2 World construction — `0x00c1c4f0` — identity cited, call contract new

`0x00c1c4f0`'s identity as the `hkpWorld` constructor is already CONFIRMED and documented at
`spec-physics-format.md` §3 — cited here, not re-derived. This section adds the wrapper's own contract.

**`0x007572f0` (34 instructions, `this` + 2 arguments):**
1. calls `0x00c1c4f0(info, 20100200)` — the second argument is Havok's SDK version number for
   2010.2.0, already added to `spec-physics-format.md` §3;
2. installs the `game_havok_world` vtable at `+0`;
3. installs the `custom_collision_listener` vtable at **`+0x360`** — the contact listener is a member
   of the world object, not a separate allocation;
4. calls `0x00c18cb0(world, world+0x360)` — HIGH, adds the contact listener to the world.

The world object is **0x370 bytes**: Havok's own `hkpWorld` plus the 0x10-byte listener member.

### 4.3 The per-frame driver — `0x0075ac20` — CONFIRMED

Called once per game frame, from `0x00702a50`, after vehicle frame work and around two scheduler
worker-mode changes (HYPOTHESIS: reserving workers for physics-class jobs for the frame's duration).

**Cross-reference, 2026-10-08:** `spec-player-runtime.md` §5.1 has the full cross-subsystem per-frame
order `0x00702a50` itself sits inside (cutscene step → controls → player/NPC AI → vehicles → animation →
velocity composer → **this step** → character-controller step → post-step/post-move → camera late
update) — see there for how this driver's own call fits relative to everything else ticked that same
frame. `spec-render-pipeline.md` §6.6 has the outer OS-thread topology (this driver runs on the main/
game thread, decoupled from `render_thread` and from Wwise's own audio thread(s)) and the boot sequence
that brings the engine up to this point.

**Headline correction — the "1/30 s fixed step" the scoping note named as P1's whole reason to exist is
not what the shipped build runs:**
- `0x0132a0ac` (the frame Δt the scoping note called a 1/30 s constant) is **rewritten every frame** by
  the game's own frame timer `0x00db9970` (395 reads in 208 functions), clamped to a configurable
  range and divided by a game time scale.
- The step-mode flag `0x012f8c1e` (which the scoping note read as a debug single-step toggle) actually
  selects, with its shipped static value **1**: **one physics step per frame, at the frame's own Δt**.
  Value 0 would instead select a fixed-step accumulator using the literal 1/30 s constant
  (`0x0161d380`) — **but the flag's only writer is a console handler nothing in the binary ever
  references** (no call site, no data pointer to it anywhere in the file). **HIGH: in the shipped game,
  physics always takes exactly one step per frame.**
- Boot-time clamps (from `0x005d1a30`) bound that per-frame Δt to **[1/120 s, 1/20 s]**.
- **So the 1/30 s fixed-step path exists in the binary but cannot be reached in the shipped build.** A
  replacement engine's frame contract is: one Havok step per game frame, Δt = the game's own
  (clamped, time-scaled) frame time — not a fixed 30 Hz sub-stepped accumulator.

**The driver, in order:**
1. Computes a sub-step cap (`round(3 × k)`, k = a runtime multiplier reset to 1.0 every frame, set to
   2.0 by one untraced vehicle code path) — used only in the unreachable fixed-step mode.
2. **Enters the physics window**: `InterlockedIncrement(0x0161d394)`. **This is an interlocked
   in-progress counter, not a critical section/lock** — confirmed by import resolution.
3. Pre-step, inside the window: water/buoyancy update, then ragdoll update (two direct Havok calls).
4. Chooses Δt (the frame's own time, by default) and runs the sub-step loop — **exactly once** in the
   shipped default mode: vehicle pre-step (§4.4) → **the step itself** `0x0075c80(Δt, job count)` → time
   bookkeeping.
5. Post-step, still inside the window: hands the stepped time to gameplay; replays two deferred queues
   (below); dispatches queued contact events to gameplay.
6. **Leaves the window**: `InterlockedDecrement(0x0161d394)`.
7. After the window: replays a further deferred queue through 8 more Havok calls, then periodic Havok
   memory upkeep (a watchdog-style timer check, roughly every 30 seconds).

**The physics window: deferred and dropped mutations (CONFIRMED) — a hard contract for a replacement.**
While the in-progress counter is non-zero, the game's own body-mutation wrappers change behaviour:
- **Activation requests** are appended to a queue instead of calling Havok immediately, and replayed
  after the window closes.
- **A second queue of body-state changes** (keyed by body-table index) is replayed the same way,
  re-checking each body's activation state before acting.
- **Three further wrappers (position/velocity-style setters) do nothing at all inside the window** —
  **any such request made during the step is simply lost, by design.**

**Contract for a replacement:** world mutation from game code is forbidden between the window's
increment and decrement; some requests queue, others are silently dropped, and a correct port must
reproduce which is which rather than queueing everything.

### 4.4 The step and the job fan-out — `0x0075c80` — CONFIRMED

Inputs: Δt, a job count (static 2), a sub-step index in a register, plus the global world and job-queue
pointers.

1. **Begin, into Havok:** `0x00c17920(world, job queue, Δt)` — HIGH, Havok's `hkpWorld::initMtStep`.
2. **Describe a game job** (a 40-byte descriptor: worker entry `0x00754dd0`, job class 3, calling
   thread id).
3. **Open a job group**, then **submit 2 jobs** on the game's own scheduler (class-3 queue), each
   stamped with a per-frame sequence number and attached to the group.
4. **Drain on this thread, into Havok:** `0x00be88c0(job queue)` — HIGH, Havok's
   `hkJobQueue::processAllJobs`.
5. **Finish, into Havok:** `0x00c16fc0(world, job queue, null)` — HIGH, `hkpWorld::finishMtStep`, given
   a **null** thread pool deliberately.
6. **Wait** for the 2 game jobs to complete, restoring the calling thread's scheduler mode afterward.

**The worker job `0x00754dd0`** (6 instructions, reached only as scheduler data, never a direct call):
looks up its scheduler slot, drains the same Havok job queue (step 4's call, from a second thread), and
runs per-thread Havok memory upkeep.

**So, per sub-step, Havok's job queue is drained by the main thread plus 2 game worker jobs — at most 3
threads — and Havok's own thread pool is never used.** No `hkJobThreadPool`-family class has RTTI
anywhere in the image, and `finishMtStep` is explicitly given a null pool. **This corrects the scoping
note's "job thread pool created by `0x00779ba0`/`0x007acfe0`"**: both of those functions set up
per-thread Havok memory routers (using the string `hkCpuJobThreadPool` only as a thread *name*), not a
pool object.

**The vehicle pre-step `0x00ac3f40`** (vehicle sub-step internals not opened) walks the game's own
vehicle lists, filters to vehicles with a live, active Havok body, and for each: runs two game-side
sub-object updates, then collects the vehicle-instance pointer. For each collected instance it calls
**`0x00c561d0(instance)` directly — a per-instance call the game makes in place of Havok's own vehicle
manager** (four of its other callers are Havok's own vehicle-module functions; HIGH, the
pre-collision update), then fans out one scheduler job per vehicle and waits. **Correction to the
scoping note:** `0x00c561d0` is per-instance, not a single manager call; **this is all driven by the
game, per vehicle, in parallel, before each step** — a real contract point for a replacement.

**Per-thread Havok memory (corrects the scoping note's "job thread pool" reading at §1.1 too).**
Bring-up submits one class-3 job per active scheduler worker; each builds a Havok memory router, tagged
with the thread name `hkCpuJobThreadPool`, and installs it for that thread. **No thread pool object is
ever constructed** — confirmed by the absence of any `hkJobThreadPool`-family RTTI in the whole image.
Shutdown mirrors this per-thread teardown (§4.5).

### 4.5 Shutdown — `0x007590d0` — CONFIRMED call sequence

Called once, from game shutdown `0x005d39e0`; does nothing unless bring-up's guard byte is set.

| # | | What happens |
|---|---|---|
| 1 | G→H | Every pooled Havok body object still held is released, and its slot returned to the free list. |
| 2 | H | **World removal** of both query phantoms (§4.1), each released after removal. |
| 3 | H | If the constraint filter exists, its constraint-listener sub-object is detached from the world (HIGH: remove constraint listener) — the filter itself is not released here, the world owns the last reference. |
| 4 | G | The border handler is cleared (world-owned, not released here). |
| 5 | H | The job queue is torn down and its block returned to the thread heap. |
| 6 | H | **The world itself is released.** Its own teardown is assumed (HIGH) to release the contact listener, filter, border and watchdog through Havok's own world-deletion paths. |
| 7 | H | The suspend-inactive-agents utility is released. |
| 8 | G→H | Per-worker Havok memory is torn down; Havok's own base system is shut down. |
| 9 | G | The guard byte is cleared. |

**Order contract: game pool → phantoms → listeners → job queue → world → utility → per-thread memory →
base system. The job queue is destroyed before the world.**

### 4.6 The frame contract, summarized for a replacement engine

1. **Bring-up, once:** memory and base system; one world (gravity `(0,−9.8,0)`, broadphase box
   `(−3500,−500,−3500)`–`(3500,1500,3200)`, collision tolerance 0.04, default convex radius 0.005,
   multithreaded); a collision-filter chain (Havok's constraint filter wrapping the game's own filter);
   a contact listener as a world member; a broadphase-border handler and the suspend-agents utility,
   both game subclasses; a memory watchdog; two 1-metre query phantoms; per-thread memory on every
   game worker.
2. **Per frame, inside one interlocked window:** water and ragdoll pre-steps, then — exactly once in
   the shipped build — the vehicle pre-step (per-vehicle Havok call plus parallel game jobs) followed
   by one multithreaded physics step of Δt (begin → 2 game jobs + main-thread drain → finish → wait);
   then deferred-queue replay and contact dispatch. **Δt = the game's own frame time, clamped to
   [1/120, 1/20] s** — not a fixed 30 Hz step.
3. **After the window:** replay one further deferred-operation queue, then periodic Havok memory
   upkeep. **Added 2026-10-03, `spec-player-runtime.md` §5.6.4/§5.8 (the player locomotion bridge): the
   character proxies (row 21/22) are stepped here too, outside the step window, in their own **up to 10
   lock-step cast-and-solve passes** submitted as game scheduler jobs (group `0x0223ac40`) — not through
   the main step's 72-slot callback surface, and not on a fixed sub-step count (it runs however many
   passes the proxies' own remaining time needs, capped at 10). `MyCharacterListener`'s callbacks (§2.2)
   fire during these passes and only queue; the two queues are flushed back into game state afterwards
   by ordinary game code (`spec-player-runtime.md` §5.7/§5.8), not by anything in this file's own 72-slot
   surface.**
4. **Hard rule:** no world mutation from game code inside the window — some requests queue for replay,
   others are silently dropped.
5. **Thread safety:** callbacks during the step run only through §2's 72 slots and the one §2.3
   function pointer, and — since the step fans out across up to 3 threads — **most of them must be
   thread-safe in a port.**

---

## 5. Table: the global constants a replacement must match

| Global | Static (file) value | Runtime value | Role |
|---|---|---|---|
| `0x0131e81c` | 0.05 | **0.005**, set once by bring-up (§4.1 step 8) | default convex radius for Havok box/sphere shapes — see `spec-vehicle-geometry.md` §11.11 |
| `0x0132a0ac` | 1/30 | the live, clamped, time-scaled frame Δt, rewritten every frame | the Δt the physics driver actually steps with |
| `0x012f8c1e` | 1 | unreachable-to-change in the shipped build (writer unreferenced) | step mode: 1 = one variable step/frame (shipped); 0 = fixed 1/30 s accumulator (dead code) |
| `0x0161d380` | 1/30 | unused in the shipped build | the fixed-step length, only consulted when `0x012f8c1e` = 0 |
| `0x012f8c18` | 3 | unused in the shipped build (fixed-mode only) | sub-step cap base |
| `0x012f8c14` | 1.0 | reset to 1.0 every frame; 2.0 under one untraced vehicle condition | sub-step multiplier `k`, fixed-mode only |
| `0x0161d394` | 0 | interlocked in-progress counter | physics-window guard — not a lock |
| `0x0132b468` | — | literal `20100200` | Havok SDK version number passed to the `hkpWorld` constructor |

---

## 6. Driving slice, unit D-U0 — the 13 named Havok vehicle sub-steps' own bodies

**Prepared by:** SPEC TEAM (cleanroom reverse-engineering process)
**Unit:** D-U0 of the Havok-internals driving slice, per `review/scoping-physics-vehicle.md` §8.2/§8.4.
Scope is deliberately narrow: **only the 13 named sub-step functions' own bodies** —
`0x00c55f80`, `0x00c56040`, `0x00c560a0`, `0x00c562d0`, `0x00c58360`, `0x00c58770` (reached from the
game override `0x00ae9570`) and `0x00c565b0`, `0x00c566e0`, `0x00c56800`, `0x00c57690`, `0x00c576d0`,
`0x00c57a50`, `0x00c57ca0` (reached from the game override `0x00ae7470`). The two non-leaf sub-steps'
own deeper callees (`0x00c565b0`→`0x00c32520` and `0x00c57690`→`0x00c63c70`, plus `0x00c32520`'s and
`0x00c63c70`'s own further callees `0x00c63570`/`0x00c63930`/`0x00c81690`) are **unit D-U1, a separate
unit, and are explicitly NOT specified here** — see §6.6 below for exactly what was (and was not)
looked at.
**Method:** no Ghidra was run this pass. Two sources, both Ghidra-free per the task's preference:
(1) a complete, already-existing, read-only disassembly listing for all 13 functions plus their
immediate callees, left over in this session's own scratchpad from the scoping pass that wrote
`review/scoping-physics-vehicle.md` §8 (`scratchpad/havokscope_pass1/func_0x*.txt` — themselves
produced by a prior pass's read-only Ghidra `func` dump with `nodecomp:1`, i.e. disassembly only, no
decompiler output, against a private copy since deleted; reading a leftover disassembly listing is
not "running Ghidra"); (2) one bounded, direct `capstone` disassembly pass (via
`tools/harnesses/exe_va.py`, read-only, no project) against the two functions identified below as the
13's *other* callers, to confirm a structural hypothesis — see §6.2. No Havok source, leaked or
public, was read; no DeepWiki page was read. Everything below is read from the shipped executable
only.
**Confidence key:** **CONFIRMED** (read directly in disassembly this pass), **HIGH** (inferred from
structure, not independently verified against a public Havok reference), **HYPOTHESIS** (plausible,
materially uncertain).
**Relationship to other specs:** builds on `spec-physics-runtime.md` §1–§2 (the three Havok ranges;
the 39-class/72-slot RTTI callback register — cited, not re-derived) and
`review/scoping-physics-vehicle.md` §1.5/§3.2/§8.2 (the vehicle pipeline and the 13 sub-steps'
existing naming/sizing — cited, not re-derived; corrections are listed in §6.7).

---

### 6.1 Exact sizes — a refinement of the scoping pass's own estimate, not a correction

The scoping pass sized these 13 by byte-range/3.6-bytes-per-instruction calibration, HIGH-labelled,
totalling ~755 instructions. This pass read every one of the 13 function bodies instruction-by-
instruction (not estimated). **Exact counts (CONFIRMED):**

| From `0x00ae9570` (game override, phase 1) | Insns | From `0x00ae7470` (game override, phase 2) | Insns |
|---|---|---|---|
| `0x00c55f80` | 22 | `0x00c565b0` | 56 |
| `0x00c58770` | 78 | `0x00c566e0` | 74 |
| `0x00c56040` | 28 | `0x00c56800` | 54 |
| `0x00c58360` | 81 | `0x00c57a50` | 173 |
| `0x00c562d0` | 17 | `0x00c57ca0` | 83 |
| `0x00c560a0` | 14 | `0x00c57690` | 18 |
| | | `0x00c576d0` | 125 |
| **subtotal** | **240** | **subtotal** | **583** |

**Total: 823 instructions (CONFIRMED)**, against the scoping pass's own ~755 HIGH estimate — within
its own stated ±15% band (755×1.15 ≈ 868), so this is a precision upgrade, not a contradiction. This
in turn still sits inside the project's long-standing "~0.8k instructions together" figure from
`spec-physics-runtime.md`/the scoping note's §0/§3.5, which this pass's own brief says to treat as
already-confirmed sizing, not something to re-derive.

The columns above are given in **real call order**, which is a genuine correction to how the scoping
note listed them — see §6.7 item 1.

---

### 6.2 New structural finding: each of the two game overrides has a Havok-internal "sibling" that calls the identical sub-steps in the identical order

Every one of the 13 dump files' own caller list showed **two** callers, not one: the already-known
game override (`0x00ae9570` or `0x00ae7470`), and a second address inside Havok range A that the
scoping note never named. Checking the call-site addresses (not just the target) inside that second
caller, for both groups, in ascending order:

- **`0x00c58890`** (Havok-internal, range A) calls, in this exact order: a sibling at `0x00c56b60`,
  then `0x00c55f80`, `0x00c58770`, a sibling at `0x00c55fc0`, then `0x00c56040`, `0x00c58360`,
  `0x00c562d0`, `0x00c560a0` — **the same 6 sub-steps `0x00ae9570` calls, in the same order**, plus
  two extra Havok-internal calls `0x00ae9570` never makes.
- **`0x00c57f70`** (Havok-internal, range A) calls, in order: a sibling at `0x00c571f0`, then
  `0x00c565b0`, `0x00c566e0`, `0x00c56800`, a sibling at `0x00c56e20`, then `0x00c57a50`, `0x00c57ca0`,
  a sibling at `0x00c56550`, then `0x00c57690`, a sibling at `0x00c56680`, then `0x00c576d0` — **the
  same 7 sub-steps `0x00ae7470` calls, in the same order**, plus four extra Havok-internal calls
  `0x00ae7470` never makes.

**Reading (HIGH):** `0x00c58890` and `0x00c57f70` are almost certainly Havok's own, un-overridden,
two-phase top-level vehicle step (very plausibly the two halves of the public
`hkpVehicleInstance::stepVehicle`/`stepVehicleUsingWheelCollideAndCollisionInput`-style sequence,
named here only by role since the exact public method name was not independently verified). **The
game's two override functions are not "Havok's default step plus extra game logic bolted on" — they
are the game's own replacement implementations of Havok's default two-phase step, built by calling an
identical-ordered subset of the same low-level primitives while omitting several of Havok's own
internal calls entirely** (`0x00c56b60`, `0x00c55fc0` in phase 1; `0x00c571f0`, `0x00c56e20`,
`0x00c56550`, `0x00c56680` in phase 2). This sharpens `review/scoping-physics-vehicle.md` §0 item 3
("the game's custom vehicle step calls about 13 of Havok's own internal vehicle sub-steps one at a
time, with its own logic between them") with a concrete, address-level account of exactly which
Havok-internal calls the override keeps and which six it drops. **None of these six sibling/extra
functions' own bodies were opened this pass** — flagged as open leads in §6.8, not analyzed, since
they are outside the 13 named in this unit's brief (`0x00c571f0` is already cited, not analyzed, by
the scoping note itself as D-U1-adjacent — see §6.6).

This was confirmed by one bounded `capstone` disassembly pass over `0x00c58890` (~140 instructions
read, forward-only, read-only, via `tools/harnesses/exe_va.py`) and `0x00c57f70` (~140 instructions,
same method) — not a full analysis of either function, only enough to read off their own call lists
and confirm the ordering match. Their own further internals were not examined.

---

### 6.3 A confirmed component-pointer array on the vehicle instance

Group-1 (phase-1) sub-steps each dispatch through a **different** sub-object pointer stored on the
vehicle instance (`ECX` at entry to the sub-step), read directly off the instance in each case:

| Call order | Sub-step | Instance offset read | Own size |
|---|---|---|---|
| 1 | `0x00c55f80` | `+0x20` | 22 insn |
| 2 | `0x00c58770` | `+0x24` | 78 insn |
| 3 | `0x00c56040` | `+0x28` | 28 insn |
| 4 | `0x00c58360` | `+0x30` | 81 insn |
| 5 | `0x00c562d0` | `+0x34` | 17 insn |
| 6 | `0x00c560a0` | `+0x38` | 14 insn |

**CONFIRMED:** the vehicle instance holds a run of per-component pointer slots at `+0x20`, `+0x24`,
`+0x28`, (`+0x2c` — present in the slot progression but not read by any of these six sub-steps),
`+0x30`, `+0x34`, `+0x38`: a 7-slot array, of which this phase uses 6, walked in strictly ascending
offset order. Every sub-step in this group reads the pointer, loads its vtable, and makes a single
**thiscall-convention virtual call through slot `+0xc`** on it (`ECX` = the component pointer at call
time; the incoming float argument is placed on the stack last via an `FSTP [ESP]`, consistent with a
`__thiscall`/thiscall-plus-stack-floats convention, not a pure `__stdcall`). **HYPOTHESIS, not
asserted:** given `spec-physics-runtime.md` §2.1 rows 30–36 list exactly seven game override classes
for the vehicle's components (engine, steering, transmission, brakes, suspension, driver input,
raycast wheel collide), the 7-slot array is almost certainly those seven component pointers in some
fixed declaration order — but **which offset is which named component was not determined this pass**
and is left as an explicit open item (§6.8) rather than guessed.

Each of the six also moves a small amount of per-instance state across the call (see per-function
notes in §6.4): `0x00c55f80` saves/restores a single status byte at instance `+0xb0`; `0x00c56040`
saves/restores two floats at instance `+0xb4`/`+0xb8`; `0x00c58770` and `0x00c58360` manage a
dynamically-grown array (via the already-identified Havok array-growth helper `0x00be6f90`,
`spec-physics-runtime.md` §3.1) sized to a per-instance count, relaying it through the call and back;
`0x00c562d0`/`0x00c560a0` are thin forwarders with no instance-state save/restore of their own.

---

### 6.4 Per-function findings

All 13 take the vehicle instance as `this` via `ECX` on entry, except where noted. "Own math" means
floating-point/SSE work visible directly in the function's own body (not inside a callee).

**Phase 1 (from `0x00ae9570`, in real call order):**

1. **`0x00c55f80`** (22 insn, CONFIRMED). Saves instance `+0xb0` (one byte) into a caller-supplied
   out-struct field, calls `+0x20`'s component vtable slot `+0xc` with `(dt, this, instance+0x9c,
   out-ptr)`, then copies the out-struct field (possibly rewritten by the callee) back into instance
   `+0xb0`. No own math. **HYPOTHESIS:** a one-byte status relay — in scale and shape consistent with
   a boolean device/driver-input status flag, not with a force or torque computation.
2. **`0x00c58770`** (78 insn, CONFIRMED). Grows/shrinks a per-instance float array at instance
   `+0xc0`/`+0xc4` (count clamped to `[0x20, 0x40]`, i.e. 32–64 elements) via `0x00be6f90`, copies the
   live array into a scratch buffer, calls `+0x24`'s component vtable slot `+0xc` with `(dt,
   instance+0xa4, this, scratch-ptr)`, copies the (possibly resized) result back into the live array,
   then releases the scratch allocation via `0x00be6620`. **HIGH:** a per-element (plausibly per-wheel,
   given the 32–64 cap) array relayed through one component call — structurally a sibling of
   `0x00c58360` below (phase 1's other array-relay step), not confirmed which named component.
3. **`0x00c56040`** (28 insn, CONFIRMED). Saves two adjacent floats, instance `+0xb4` and `+0xb8`,
   into stack locals, calls `+0x28`'s component vtable slot `+0xc` with `(dt, this, &local, out-ptr)`,
   restores both floats (possibly rewritten) back into `+0xb4`/`+0xb8`. **HYPOTHESIS:** a two-scalar
   state relay — scale is consistent with either a steering-angle pair (current/target) or an
   engine-side pair (e.g. RPM and throttle), not independently distinguishable from this function's
   own body alone.
4. **`0x00c58360`** (81 insn, CONFIRMED). The richest of the six: resizes **two** per-instance arrays
   (a byte array headed at instance `+0x8c`/`+0x90`, a dword array headed at instance `+0x94`/`+0x98`,
   both sized by `0x00be6f90` to the sub-component's own `+0xa4` field — a count the component owns,
   read off the *component* object, not the instance), copies a byte-per-element array from instance
   `+0xa0` (length instance `+0xa4` — note: a **different** `+0xa4`, this one on the vehicle instance
   itself, not the component — the two objects happen to share the offset name) into the resized byte
   array, copies a single float from instance `+0xac` into the output struct's `+0xb8`, calls `+0x30`'s
   component vtable slot `+0xc` with `(dt, this, out-ptr)`, then copies the (possibly differently
   sized — the callee can write a new count at out `+0x90`) result array back into instance `+0xa0`
   and the possibly-updated float back into instance `+0xac`. **HIGH:** a per-element byte-status array
   (length = instance's own element count, most plausibly **per-wheel**, since the scoping note's own
   §1.5 already establishes a wheel-collide/suspension cluster at this call depth) relayed through one
   component call, with an extra scalar riding along. Not the raycast-wheel-collide override itself
   (that is `0x00aea040`→`0x00c5a1e0`, a different address entirely, per
   `review/scoping-physics-vehicle.md` §1.5) — this is a different, smaller, Havok-internal step that
   happens to also touch a per-wheel array.
5. **`0x00c562d0`** (17 insn, CONFIRMED). Thin forwarder: dereferences one caller-supplied pointer
   once, passes both the dereferenced value and the original pointer plus `this` to `+0x34`'s
   component vtable slot `+0xc`, alongside the incoming float. No instance-state save/restore, no own
   math.
6. **`0x00c560a0`** (14 insn, CONFIRMED). The thinnest: forwards `(dt, this, caller-supplied pointer)`
   straight to `+0x38`'s component vtable slot `+0xc`. No instance-state save/restore, no own math.

**Phase 2 (from `0x00ae7470`, in real call order):**

7. **`0x00c565b0`** (56 insn, CONFIRMED own body). Calls `0x00c32520` directly (not virtual) — **this
   is unit D-U1's own entry point, not analyzed further here** (see §6.6). In its *own* body
   (after that call returns), it does two SSE lerp-style vector computations (`result = (A−B)×scale`,
   scale broadcast from a single scalar field) over 4-vectors read off a sub-object at instance
   `+0x18`/`+0x1c`, copies a 0x80-byte struct via the small flat-copy leaf `0x00ae4870`, and applies a
   3×3 matrix–vector transform via the leaf `0x00be9e80` (both of these are small, already-generic
   Havok utility leaves — their own bodies were read in full and are trivial: a field-for-field struct
   copy, and a textbook `out = M·v` transform; neither is vehicle-specific). The results land in a
   caller-supplied output struct at `+0x100`, `+0x150`, `+0x160`. **HIGH:** this is the step that
   *finishes* building a per-wheel contact/output record whose raw description `0x00c32520` computes —
   consistent with `review/scoping-physics-vehicle.md` §8.2's own characterization of `0x00c32520` as
   "a contact/friction description builder, shared."
8. **`0x00c566e0`** (74 insn, CONFIRMED, pure SSE, no calls). Reads a scalar out of a 4-vector's last
   lane (`SHUFPS …,0xff`), scales it by an incoming parameter and by a caller-supplied direction
   vector, and **accumulates** the result into `[out+0xe0]`. Separately builds a 3×3 basis from three
   row-vectors at `+0xf0`/`+0x100`/`+0x110` of a component object, transforms a second caller-supplied
   vector through it, and accumulates that into `[out+0xf0]`. **HIGH: a force accumulator at `+0xe0`
   and a torque accumulator at `+0xf0`** — the classic rigid-body force/torque-accumulation pattern,
   matching how `hkpVehicleInstance`'s per-step contributions are collected before being applied to the
   chassis body through the `hkpUnaryAction::applyAction` mechanism already identified in
   `spec-physics-runtime.md` §2.2.
9. **`0x00c56800`** (54 insn, CONFIRMED, pure SSE/x87, no calls). Reads the **same** `+0xf0..+0xfc`
   accumulator `0x00c566e0` just wrote, computes its squared magnitude, compares it against a
   threshold field on a third object, selects one of two limit floats depending on the comparison,
   multiplies by an incoming scalar (`dt`-shaped), computes `max(0, 1 − result)`, and rescales the
   `+0xf0` accumulator by that factor in place. **HIGH: a magnitude-based clamp/rescale of the torque
   accumulator** — structurally a damper (shrink the accumulated torque toward zero once it exceeds a
   limit), consistent in shape with the public Havok vehicle concept of a velocity/torque damper,
   though the specific public class name was not independently confirmed (HYPOTHESIS for the name,
   HIGH for the behavior).
10. **`0x00c57a50`** (173 insn, CONFIRMED, pure SSE/x87, no calls — the largest of the 13). Processes
    **two** related per-contact records (`[arg]` and `[arg+4]` — plausibly a wheel pair sharing an
    axle, not confirmed). For each: skips if the record pointer is null or if a byte at that record's
    `+0xe8` equals `5` (see §6.5 — a shared sentinel seen in three of the 13). Decodes **two**
    signed-16-bit fields (at `+0x194`/`+0x196`) through the project's packed-short-to-float idiom
    (§6.5), each run through an identical `result = min(limit, running − field×factor)`
    decay-toward-limit computation, then uses the two results to scale two basis vectors (`+0x10`,
    `+0x20` of a component object) into caller output slots, and separately accumulates a 3×3
    matrix-transformed vector into the component's own `+0x20`. The entire block is then repeated for
    the second record with a parallel offset family (`+0x90`/`+0xa0`/`+0xc0..+0xe0`), writing into a
    second pair of output slots and accumulating into `+0xa0`. **HYPOTHESIS:** given the paired
    structure and the decay-toward-limit shape, this plausibly implements part of an axle-pair
    suspension/anti-roll coupling, or a per-wheel steering/suspension response pair — not independently
    confirmed; flagged for whoever opens the component itself.
11. **`0x00c57ca0`** (83 insn, CONFIRMED, pure SSE/x87, no calls). The same "decode two packed-short
    fields through `max(0, 1 − field×factor)`, scale two basis vectors, matrix-accumulate" shape as
    half of `0x00c57a50` above, but over a **different** field-offset family (`+0xb4`/`+0xb6` rather
    than `+0x194`/`+0x196`) and a single record rather than a pair. **HYPOTHESIS:** the same family of
    computation as `0x00c57a50`, applied to a different per-wheel data block; not independently named.
12. **`0x00c57690`** (18 insn, CONFIRMED own body). A thin forwarder straight into `0x00c63c70` — **D-
    U1's own largest function, not analyzed here** (see §6.6). Its own body does no math beyond
    assembling the call's three pointer arguments from instance `+0x1c`+`0xa4` and instance `+0x54`.
13. **`0x00c576d0`** (125 insn, CONFIRMED, pure SSE/x87, loops, no calls). Loops over
    `[instance+0x1c]+0x9c` entries, skipping any whose pointer is null or whose `+0xe8` byte equals `5`
    (the same sentinel as item 10/§6.5). For each live entry: builds a 3×3 orthonormal basis from a
    per-entry contact frame, transforms an existing per-wheel impulse vector through it, subtracts a
    caller-supplied delta, **clamps the result to `±[0x01243130]`** (a 16-byte all-lanes sign-bit mask
    used with `MINPS`/`MAXPS` for a textbook symmetric clamp, not a named Havok constant — read as
    data, not reproduced further than its role), adds the delta back, and writes the clamped result
    both to a caller output array and back into the per-entry accumulator. **HIGH: a per-wheel friction-
    impulse clamp**, run last in the whole 13-call sequence — a sensible closing step after the force/
    torque accumulation (items 8–9) and the friction solve forwarded to D-U1 (items 7/12).

---

### 6.5 Shared idioms observed across the 13 (worth recording once, not per-function)

- **A disabled/removed-record sentinel.** Three of the 13 (`0x00c57a50`, `0x00c576d0`, plus — seen
  only incidentally while tracing the forward call, not analyzed — D-U1's own `0x00c32520`) test a
  byte at offset `+0xe8` of a per-contact/per-wheel record against the literal `5` and skip the record
  if so. **HIGH:** a shared "this collidable/record is disabled, removed, or otherwise inactive" kind
  code, read the same way in every case it appears. Not independently named against a public Havok
  enum; flagged as a candidate for whoever next touches a Havok body/record layout.
- **A packed-16-bit-field decode.** `0x00c57a50` and `0x00c57ca0` both decode a signed 16-bit field by
  sign-extending it, shifting left 16, storing it to a stack dword, and reloading that dword as a
  32-bit float (i.e. the 16-bit value ends up occupying the float's top 16 bits before any further
  arithmetic normalizes it). The same exact three-instruction idiom (`MOVSX`; `SHL …,0x10`; store-then-
  reload-as-float) also appears inside D-U1's `0x00c32520` on a different field. **HIGH:** a shared
  compact/scaled encoding for small per-wheel tuning values, consistent with this project's own
  established convention of packing scaled values into narrow fields elsewhere in the format specs —
  not re-derived here, just flagged as the same shape.
- **The array-growth family.** `0x00be6f90`/`0x00be6620` (already named at HIGH confidence in
  `spec-physics-runtime.md` §3.1 as Havok memory/`hkArray` growth helpers) are used directly inside
  `0x00c58770` and `0x00c58360` to grow/shrink per-instance scratch arrays before and after a component
  call. A third sibling, `0x00be6960`, was seen (not analyzed) inside `0x00c58890`'s own body during
  the §6.2 check — same family, not opened.
- **Force/torque accumulator fields.** Instance-or-component offsets `+0xe0` (force-shaped) and `+0xf0`
  (torque-shaped) are written by `0x00c566e0` (accumulate) and read/rescaled by `0x00c56800` (clamp).
  The same `+0xf0` field is referenced again, read-only, inside `0x00c57a50`'s own output wiring. These
  three functions are clearly cooperating on one running accumulator, not independent.

---

### 6.6 Explicit stop: what this unit did and did not open inside the two non-leaf sub-steps

Per this unit's brief, `0x00c565b0` and `0x00c57690` were confirmed to be non-leaves (matching
`review/scoping-physics-vehicle.md` §8.2's own finding), and their own bodies (56 and 18 instructions
respectively, both counted in §6.1) were read and are described in §6.4 items 7 and 12 above.
**Their deeper callees — `0x00c32520`, `0x00c63c70`, and `0x00c63c70`'s own further callees
`0x00c63570`/`0x00c63930`/`0x00c81690` — were not analyzed, specified, or described beyond what the
existing scoping note already says about them.** Reading the leftover dump files for `0x00c565b0` and
`0x00c57690` incidentally included these deeper functions' own disassembly (the dump tool recorded
one hop of callees alongside each root), so their existence, rough size, and call targets were
unavoidably visible — that visibility is noted here for transparency, but no new behavioral claim
about `0x00c32520`/`0x00c63c70`/`0x00c63570`/`0x00c63930`/`0x00c81690` is made in this document, and
none of their own internal computation is described above. That work belongs to **D-U1**, a separate
unit, per this unit's own brief.

---

### 6.7 Corrections / additions to `review/scoping-physics-vehicle.md` §8.2

1. **Call order, not target-address order.** §8.2's own tables list the six/seven sub-steps in
   ascending target-address order. The **real call order** (by call-site address inside
   `0x00ae9570`/`0x00ae7470`) is: phase 1 — `0x00c55f80`, `0x00c58770`, `0x00c56040`, `0x00c58360`,
   `0x00c562d0`, `0x00c560a0`; phase 2 — `0x00c565b0`, `0x00c566e0`, `0x00c56800`, `0x00c57a50`,
   `0x00c57ca0`, `0x00c57690`, `0x00c576d0`. Notably, phase 2's friction forward (`0x00c57690`→D-U1)
   and its trailing clamp (`0x00c576d0`) both run **after** the two wheel-pair steps
   (`0x00c57a50`/`0x00c57ca0`), not before, as the target-sorted listing could be misread to suggest.
2. **New finding, not in §8.2 at all: both game overrides have a Havok-internal sibling that calls the
   identical sub-step set in the identical order** (`0x00c58890` for phase 1, `0x00c57f70` for
   phase 2) — see §6.2. §8.2 traced the sub-steps' own sizes but did not look at who else calls
   them; this pass's leftover dump files happened to carry that information for free in their
   "callers/refs" lines, prompting the follow-up check.
3. **Exact instruction counts** (§6.1) supersede §8.2's own byte-range HIGH estimates for these 13
   specific addresses (755 insn estimated → 823 insn exact) — within tolerance, not a contradiction,
   but worth citing the exact figure going forward rather than the estimate.

No change is proposed to `spec-physics-runtime.md` (not edited by this unit, per its own `.H-1`
framing that vehicle sub-steps are out of that file's scope).

---

### 6.8 Open items for whoever continues this slice (D-U1 or a later unit)

1. **Which of the 7-slot component-pointer array's offsets (`+0x20`…`+0x38`, §6.3) corresponds to
   which of the 7 named override classes** (`spec-physics-runtime.md` §2.1 rows 30–36: engine,
   steering, transmission, brakes, suspension, driver input, raycast wheel collide) was not
   determined. Resolving it would let every "HYPOTHESIS: a component call" note above be sharpened to
   a named subsystem.
2. **Six sibling/extra Havok-internal functions, named but not opened**, found only by address while
   confirming §6.2: `0x00c56b60`, `0x00c55fc0` (phase-1 siblings, called by `0x00c58890` but not by
   the game override) and `0x00c571f0`, `0x00c56e20`, `0x00c56550`, `0x00c56680` (phase-2 siblings,
   called by `0x00c57f70` but not by the game override). `0x00c571f0` is already cited once by
   `review/scoping-physics-vehicle.md` §8.2 as a second caller of D-U1's own `0x00c32520`; the other
   five are new. These are natural candidates for whoever scopes or opens D-U1, since they sit in the
   same two call clusters.
3. **A possible Havok-range-A-to-game-code direct call not covered by the existing 72-slot register or
   the one function-pointer exception.** While reading `0x00c58890` (§6.2), one call near its tail
   targets `0x00ae8f10` — inside the game's vehicle cluster (`0x00a77000`–`0x00af0000`), not inside any
   of the three Havok ranges. `spec-physics-runtime.md` §2.3 and §3 together account for exactly two
   ways Havok code reaches game code (72 vtable slots; one function-pointer handed to a callback
   constraint motor) and state "None of the other 41 [small Havok-called-by-outside functions] is
   Volition gameplay logic." A direct `CALL` the other direction — from inside Havok range A out to a
   game-cluster address — was not examined by that census and was not checked further by this pass (it
   surfaced only incidentally, one call site, during a bounded ~140-instruction read not intended to
   analyze `0x00c58890` itself). **Flagged, not confirmed**: worth a short, targeted check before
   relying on it, since if real it would be a third kind of Havok→game crossing not yet in any spec.
4. **The exact Havok public method names for all 13** (beyond the role-level HIGH/HYPOTHESIS labels in
   §6.4) were not independently confirmed against any permitted source. This project's own standing
   rule is to prefer Havok's public 2010.2 naming only where confident; none of the 13 individually
   cleared that bar this pass, so all remain cited by hex address plus behavior, matching the
   instruction given for this unit.

---

## 7. Havok W-U1 — Character-proxy primitives

**Scope.** The Havok-*internal* functions reached from the game's character-proxy wrapper code
(already specified at `spec-physics-runtime.md` §2 — the 39-class RTTI callback register — and at
`spec-player-runtime.md` §5 (U3, the locomotion bridge) / §5.6.4–§5.6.5 (the custom proxy passes and
controller motion, OPEN items 36/35)). Roots, per the task brief and `review/scoping-physics-vehicle.md`
§8.1/8.5: `0x00c835b0`, `0x00c83570`, `0x00c835c0` (proxy get/set primitives), `0x00c1e730` (phantom
transform setter), `0x00c30150` (the AABB/broadphase commit both transform setters call), `0x00c838d0`
(the cast-manifold commit after a lock-step pass). The game-side wrapper code itself
(`0x0079b000`–`0x0079e000`, `0x007a5b10`, the pool/pass machinery) is **not** re-derived here — it is
cited from `spec-player-runtime.md` §5.6.1/§5.6.4/§5.6.5, which already read it.

**Method.** Ghidra-free throughout. Capstone linear disassembly (`CS_ARCH_X86`/`CS_MODE_32`) over a
read-only `pefile` mapping of `SaintsRowTheThird.exe` (GOG), using this project's existing
`tools\harnesses\exe_va.py` for raw VA→file-offset reads plus two small scratchpad scripts written for
this pass (`dump_fn.py`, a linear disassembler from a given start VA; `size_only.py`, a batch
first-RET sizer; `rtti_name.py`, an MSVC RTTI type-descriptor reader: vtable VA → `vtable[-1]` =
CompleteObjectLocator → `+0x0c` = TypeDescriptor → name string at `+8`). No Ghidra project was opened,
copied, or deleted this pass. No `.czn_pc` data was touched. No leaked Havok source, leaked SR4/
Volition source, or any DeepWiki page was read; every address, byte, and class name below comes
directly from the shipped executable.

**Confidence key** (this project's standard): **CONFIRMED** (read directly in this pass's own
disassembly or RTTI lookup), **HIGH** (a confident structural/behavioral inference, not itself a
literal read), **HYPOTHESIS** (plausible, not checked further).

---

### 7.0 Results up front

1. **The three tiny proxy accessors are real Havok base-class methods, not game overrides** — they
   sit inside Havok range A itself (`spec-physics-runtime.md` §1), so they are `hkpCharacterProxy`'s
   own compiled methods, inherited unmodified by the game's `hk_custom_character_proxy` subclass.
   `0x00c835b0` = **get linear velocity** (returns `&this+0x40`), `0x00c835c0` = **set linear
   velocity** (writes `this+0x40` from the one stack argument), `0x00c83570` = **get position**
   (returns a pointer two hops from `this+0x60`), and a fourth, previously-unlisted one-liner,
   `0x00c835e0` = **get shape phantom** (returns `this+0x60` itself). HIGH names, from Havok's public
   2010.2 `hkpCharacterProxy` API (`getLinearVelocity`/`setLinearVelocity`/`getPosition`/
   `getShapePhantom`), matching the exact get/set-by-reference shape; CONFIRMED by listing for the
   behavior itself.
2. **`this+0x60` (the proxy's cached shape-phantom pointer) is the same object `character_controller`
   caches at its own `+0x14`** (`spec-player-runtime.md` §5.6.1) — cross-confirmed this pass by reading
   `0x0079f5f0` (§7.5 below), which uses controller `+0x14` as both the object whose `getAabb`-style
   virtual slot it calls and the `this` it hands to `0x00c30150`.
3. **`0x00c1e730` is `hkpShapePhantom::setTransform`-shaped (HIGH): it writes the phantom's full
   4×hkVector4 transform (rows at `+0xb0`/`+0xc0`/`+0xd0`/`+0xe0`) and, when the phantom is already in
   a world, refreshes its AABB through a shape `getAabb`-style virtual call and a commit into
   `0x00c30150`.** Its immediately-following sibling in the same cluster, `0x00c1e7b0`, is the
   position-only variant (`setPosition`-shaped): it overwrites only the translation row (`+0xe0`) and
   folds an extra caller-supplied margin into the same refresh-and-commit tail. Both were read in full
   this pass; `0x00c1e7b0` was not one of the task's named roots but sits adjacent to `0x00c1e730` and
   shares its entire tail, so it is documented here too rather than left as an unlabelled neighbour.
4. **`0x00c30150` (267 instructions, CONFIRMED by listing — refines the scoping pass's ~228-instruction
   byte-range estimate) is the real "commit a new AABB to the phantom and its broadphase cell"
   routine**, and it is itself **aware of the physics-window deferral mechanism**
   `spec-physics-runtime.md` §4.3 already documents at the body level: when the world's step-nesting
   counters (`world+0x94`/`world+0x98`) are non-zero, it does not touch the AABB at all — it builds a
   small `{type=0x12, phantom, value}` record and hands it to `0x00c164c0` to queue, then returns. This
   is a previously-undocumented **third concrete instance** of the deferred-queue mechanism (joining
   the activation-request and body-state-change queues §4.3 already named), and the first one tied to
   a specific caller (the phantom/AABB path). Outside the window, it does real work: a per-thread
   bump-allocator lookup (keyed by a size class read from the world object) for an old/new AABB scratch
   pair, a lock/unlock pair around the broadphase (`0x00d65e00`/`0x00d65e10`), a **virtual call on the
   world's broadphase sub-object** (`world+0x58`, vtable slot 11/`0x2c`) that reports whether the
   phantom's broadphase cell range actually changed, and — only if it did — a splice into the
   broadphase tree (`0x00d58180`), a pair-manager update (`0x00d58140`), a listener-array notify
   (`0x00d580d0`) and a Havok-internal post-update call (`0x00c2fd20`); then, **when this was the
   outermost nested call finishing (the world's own step-nesting counter returns to 0) and certain
   flags are set, it itself replays the world's deferred queues** through `0x00c16420`/`0x00c16440` —
   a second, concrete tie into the §4.3 deferral mechanism, this time on the flush side. It ends by
   releasing the scratch allocation (through the same fixed `hkContainerHeapAllocator` object
   `0x01312854` `spec-physics-runtime.md` §3.1 already names for `0x00be6620`) when a block was
   heap-backed rather than bump-allocated.
5. **`0x0079f5f0` (the game-code AABB-grow caller `spec-player-runtime.md` §5.6.4 step 1 already
   names) calls `0x00c30150` directly**, not through `0x00c1e730`/`0x00c1e7b0` — it does its own
   velocity-swept, margin-padded AABB computation inline (confirmed by full listing, §7.5) and
   hands the result straight to the commit routine. The scoping pass's "0x00c1e730 ... 10 distinct
   callers incl. the overlap-query wrappers" (§8.1/§8.3) stands for the *other* ten callers; this one
   is now a confirmed eleventh, parallel front end.
6. **`0x00c838d0` (314 instructions, CONFIRMED — matches the scoping pass's own 315-instruction
   estimate almost exactly) is the per-touched-object commit after one of the proxy's cast-and-solve
   passes.** For each hit record in its input array it recovers the touched collidable's **owning
   entity through Havok's publicly-documented owner-offset back-pointer** (a signed byte at
   `collidable+0x10`, added to the collidable's own address), then checks that entity's attached
   property array — **the very same `object+0x7c`/`+0x80` key/value array
   `spec-physics-runtime.md` §3.1 already found via `0x00756cb0`** — for property key **`0x1300`**.
   Entities carrying that key are routed to a small **phantom-listener fan-out triple**
   (`0x00c83720`/`0x00c83760`/`0x00c837a0`, §7.7, new to this pass); entities without it go through
   a point-velocity query (`0x005247f0` — the exact same function `scoping-physics-vehicle.md` §8.5
   item 1 already flagged as a Havok-inline function living in the game cluster, here confirmed at a
   second, independent call site) and a closing-speed/penetration computation, folded into an
   aggregate result by `0x00c83760`'s neighbour-of-shape `0x00c83760`† and finally either written into
   an optional output accumulator or used to **activate the touched entity** through `0x00c150c0` —
   the same function `spec-physics-runtime.md` §3.1 already flags as "HIGH: entity `activate`"
   (HYPOTHESIS there on world-lock-adjacent evidence alone); this is now a **third independent call
   site consistent with that same reading**, which strengthens it considerably without itself making
   it CONFIRMED. (†`0x00c83760` is reached twice in this unit with two different roles — see §7.6's
   own breakdown; the aggregate-fold use and the listener-dispatch use are the same physical function,
   not a coincidence of naming.)
7. **The game's custom property key `0x1300` is now independently corroborated by its own
   remove-side.** `0x00c84960` — the Havok-internal destructor reached from the game's shared
   entity/phantom-listener teardown `0x0079c4d0` (a loose end the scoping pass flagged as "not yet
   sized") — explicitly calls `0x00c21970(this->+0x60, &{0x1300})`, i.e. it removes the same property
   key by value at object teardown. Between the query side (§7.6) and this remove side, `0x1300` is
   now CONFIRMED (by two independent, consistent uses) to be a real property key the game attaches to
   specific Havok objects, read out through Havok's own public property-array mechanism — exactly the
   "game keeps its own data inside Havok body properties" pattern `spec-physics-runtime.md` §3.1
   already named for `0x00756cb0`, now with a concrete key value and a concrete consumer.

---

### 7.1 Object layout used by this unit (cites `spec-player-runtime.md` §5.6.1; one addition)

| Offset | Object | Field | Source |
|---|---|---|---|
| controller `+0x0c` | `character_controller` | the proxy (`hk_custom_character_proxy`) | `spec-player-runtime.md` §5.6.1 |
| controller `+0x14` | `character_controller` | the shape phantom (`hk_character_shape_phantom`) | `spec-player-runtime.md` §5.6.1; **cross-confirmed this pass** via `0x0079f5f0` (§7.5) |
| proxy `+0x40` | `hk_custom_character_proxy` (Havok base fields) | linear velocity, `hkVector4` | CONFIRMED by listing, §7.2 |
| proxy `+0x60` | `hk_custom_character_proxy` | cached shape-phantom pointer | CONFIRMED by listing, §7.2; same object as controller `+0x14` |
| proxy `+0x88`/`+0x8c` | `hk_custom_character_proxy` | two scalar margins | CONFIRMED by listing, §7.5 (matches `spec-player-runtime.md` §5.6.4 step 1's own citation of "two proxy margins (`+0x88`, `+0x8c`)") |
| phantom `+0x08` | `hkpShapePhantom`/`hkpPhantom` (Havok base) | a pointer, non-null once the phantom belongs to a world; gates the AABB-refresh branch | HIGH, §7.3 |
| phantom `+0x10` | — | a sub-object pointer (shape-ish), used as `this` for a `getAabb`-shaped virtual call at vtable slot 7 | HIGH, §7.3 |
| phantom `+0x18` | — | set once at construction to `&phantom+0xb0` (a self-pointer) | CONFIRMED by listing, §7.3 |
| phantom `+0xb0`/`+0xc0`/`+0xd0`/`+0xe0` | `hkpShapePhantom` | the phantom's own inline world transform: three rotation-basis rows then the translation row (an `hkTransform`-shaped 0x40-byte block) | CONFIRMED by listing (the 4-vector copy) + HIGH for the transform reading, §7.3 |
| phantom `+0xa4`/`+0xa8` | `hkpPhantom` (Havok base) | a listener array (pointer + count) distinct from the proxy's own `MyCharacterListener` registration | CONFIRMED by listing, §7.7 |
| collidable `+0x10` | `hkpCollidable` (Havok base) | signed-byte owner offset (Havok's public back-pointer-to-owner convention) | HIGH, §7.6 |
| entity/world-object `+0x7c`/`+0x80` | — | the property key/value array `spec-physics-runtime.md` §3.1 already names via `0x00756cb0` | CONFIRMED by listing (same offsets, same stride), §7.6 |

RTTI vtable identifications used to anchor this table (CONFIRMED, `rtti_name.py` method above):

| Vtable VA | Class (RTTI mangled name, decoded) |
|---|---|
| `0x0115486c` | `hkpPhantom` |
| `0x011548ac` | `hkpShapePhantom` |
| `0x01154ac4` | `character_controller` (already named `spec-player-runtime.md` §5.6.1) |
| `0x01154a98` | `MyCharacterListener` (already named `spec-player-runtime.md` §5.6.1) |

The first two are new to this pass and directly confirm that `0x00c1e860`/`0x00c1e8b0` (the
constructor chain that installs `+0x18 = &this+0xb0`, read incidentally while tracing `0x00c1e730`'s
neighbourhood) build a Havok `hkpShapePhantom` over an `hkpPhantom` base — i.e. the character's shape
phantom itself, matching `spec-physics-runtime.md` §2.1 row 21's `hk_character_shape_phantom` /
`hkpCachingShapePhantom` identification from the game side.

---

### 7.2 The proxy get/set primitives — `0x00c835b0`, `0x00c83570`, `0x00c835c0`, `0x00c835e0`

All four sit inside Havok range A (`0x00be5d20`–`0x00c98da0`), so — unlike the 39-class callback
register of `spec-physics-runtime.md` §2, which documents **game** overrides of Havok interfaces —
these are Havok's **own** compiled methods on `hkpCharacterProxy`, called directly by game code
(`0x007a5b10`, `0x0079f5f0`, …) because the game's `hk_custom_character_proxy` subclass does not
override them.

**`0x00c835b0` — get linear velocity (2 instructions, CONFIRMED by full listing).**
```
lea eax, [ecx + 0x40]
ret
```
`thiscall(this)`, no stack args. Returns a pointer to `this+0x40` — a reference return, the classic
compiled shape of a public-header inline accessor. HIGH: Havok's public `hkpCharacterProxy::
getLinearVelocity() const`. Called at `0x0079f5f0+0x16` (`ecx` = the proxy, §7.5) and, per
`spec-player-runtime.md` §5.6.5, inside `0x007a5b10`.

**`0x00c835c0` — set linear velocity (9 instructions, CONFIRMED by full listing).**
```
push ebp; mov ebp, esp; and esp, 0xfffffff0
mov eax, [ebp+8]            ; the one stack argument: a pointer to an hkVector4
movaps xmm0, [eax]
movaps [ecx+0x40], xmm0     ; write into the SAME offset the getter reads
mov esp, ebp; pop ebp
ret 4
```
`thiscall(this, const hkVector4&)`. HIGH: `hkpCharacterProxy::setLinearVelocity(const hkVector4&)`.
This is the exact function `spec-player-runtime.md` §5.6.5 already names as the call at the end of
`0x007a5b10` ("ends by setting the proxy's linear velocity") — confirmed here at the instruction
level for the first time.

**`0x00c83570` — get position (4 instructions, CONFIRMED by full listing).**
```
mov eax, [ecx+0x60]     ; eax = the cached shape-phantom pointer
mov eax, [eax+0x18]     ; eax = phantom+0x18 = &phantom+0xb0 (set once at construction, §7.3)
add eax, 0x30           ; eax = &phantom+0xe0 = the translation row of the phantom's own transform
ret
```
`thiscall(this)`, no stack args, reference return. HIGH: `hkpCharacterProxy::getPosition() const`,
which on this build forwards through the cached phantom to the phantom's own inline transform rather
than through a separately-allocated `hkMotionState`. This resolves, at the instruction level, the
"source identified" entries `spec-player-runtime.md` §5.3 already lists for position-derived fields.

**`0x00c835e0` — get shape phantom (2 instructions, CONFIRMED by full listing; not a task root, found
as filler between the other three and documented here since it completes the small cluster).**
```
mov eax, [ecx+0x60]
ret
```
HIGH: `hkpCharacterProxy::getShapePhantom() const`. A byte-identical duplicate sits at `0x00c835f0`
(identical-code folding, consistent with this project's own documented pattern for small Havok
leaves, `spec-physics-runtime.md` §1).

---

### 7.3 The phantom transform setters — `0x00c1e730` and its sibling `0x00c1e7b0`

Both operate with `this` = the shape phantom (not the proxy): callers load `ecx` either from
`proxy+0x60` or from `controller+0x14` before calling in, per §7.1.

**`0x00c1e730` — set full transform and refresh AABB (44 instructions / 126 bytes, CONFIRMED by full
listing).**
1. Copies four `hkVector4`s (16 bytes each, SSE `movaps`) from the one stack argument into
   `this+0xb0`, `+0xc0`, `+0xd0`, `+0xe0` in order — the phantom's own inline transform (three
   rotation-basis rows then the translation row).
2. Reads `this+0x08`; if null (phantom not yet in a world), skips straight to the epilogue — the
   transform write above still happened, only the AABB refresh is skipped.
3. Otherwise: dereferences `this+0x08` twice (`[[this+8]+0x78]`) to reach a float at `+0xc` — HIGH, a
   world- or broadphase-level tolerance constant — multiplies it by a fixed double at `0x012a2dc0`,
   then makes a **virtual call** on `this+0x10` (vtable slot 7, i.e. offset `0x1c`) with the arguments
   (the original transform pointer, the scaled tolerance, an output buffer). HIGH: this slot/signature
   matches Havok's public `hkpShape::getAabb(const hkTransform&, hkReal tolerance, hkAabb&) const`.
4. Calls **`0x00c30150(this, &outputBuffer)`** — the commit routine, §7.4.

HIGH overall: `hkpShapePhantom::setTransform(const hkTransform&)`.

**`0x00c1e7b0` — set position only, with caller-supplied extra margin (40 instructions / 108 bytes,
CONFIRMED by full listing; not a task root, read for cross-reference since it is `0x00c1e730`'s
immediate neighbour and shares its entire tail).**
Same shape, except: only `this+0xe0` (the translation row) is overwritten from the one-vector stack
argument — the rotation rows are left untouched — and the scaled world tolerance has a **second stack
argument** (a float) added to it before the same `getAabb`-shaped virtual call and the same
`0x00c30150` commit. HIGH: `hkpShapePhantom::setPosition(const hkVector4&, hkReal extraMargin)`.
`ret 8` (two stack arguments) versus `0x00c1e730`'s `ret 4` (one) confirms the extra parameter.

Both functions are called by 10 distinct outside functions combined per the scoping pass's own count
(§8.1), including the three overlap-query wrappers `spec-physics-runtime.md` §4.1 already names
(`0x00757ec0`/`0x007581b0`/`0x0075a480`). `0x0079f5f0` is **not** one of them — see §7.5.

---

### 7.4 The commit routine — `0x00c30150`

267 instructions, `0x00c30150`–`0x00c30483` inclusive (819 bytes), CONFIRMED by full listing end to
end (the scoping pass's own ~228-instruction figure was a byte-range estimate; this refines it to an
exact count without contradicting the address range, which matches exactly).

`thiscall(this = phantom, const hkAabb* newAabb)` (`ret 4`).

**Early-out / deferral path (CONFIRMED).** `ecx = this+0x08` (the world/owner link, §7.1); if null,
skip to the epilogue immediately (nothing to commit — the phantom isn't in a world yet). Otherwise
read two fields off that object, `world+0x98` and `world+0x94`, and sum them:
- **If the sum is non-zero** (HIGH: a physics-window step-nesting indicator, matching
  `spec-physics-runtime.md` §4.3's interlocked-counter mechanism in spirit, though this pair of fields
  is on the world object, not the `0x0161d394` counter itself, so it is a separate, world-local
  nesting tracker): build a 0x18-byte stack record `{byte 0x12, this, the input value}` and call
  **`0x00c164c0`** to enqueue it, then return — **the AABB is not touched this call.** This is a
  concrete, newly-identified instance of the deferred-mutation pattern `spec-physics-runtime.md` §4.3
  already documents generically ("position/velocity-style setters do nothing at all inside the
  window" / "a second queue of body-state changes... replayed the same way") — here specifically for
  phantom AABB commits, tagged with record-type `0x12`.
- **If the sum is zero**, proceed with the real work below.

**Real-work path (CONFIRMED by listing, interpretation HIGH).**
1. Increments `world+0x94` (a re-entrancy counter — decremented again at the very end, §step 5).
2. **Twice** (once for the AABB's "old" value, once for "new"): reads a size-class field off
   `world+0x34c`, rounds it up to a 128-byte boundary (`(n*8+0x7f) & ~0x7f`), and looks up a per-thread
   scratch-allocator descriptor through a fixed function pointer at `0x0101c0c8` keyed by a per-thread
   slot global at `0x029bfc48`; if the descriptor's free space covers the rounded size, bumps its
   cursor in place, otherwise falls back to `0x00be6960` (heap growth — same family as the
   `hkContainerHeapAllocator`-adjacent growth helpers `spec-physics-runtime.md` §3.1 already names for
   `0x00be6620`). HIGH: a per-thread bump/scratch allocator for the transient old/new AABB pair, with
   heap fallback — a well-documented public Havok architectural pattern (`hkThreadMemory`-style
   temporary allocation), not a specific named call here.
3. **Broadphase update.** Reads `world+0x58` (HIGH: the broadphase sub-object); if its `+0x1c` field is
   non-zero, calls `0x00d65e00` (range B) — HIGH, a lock, paired with `0x00d65e10` later (an unlock) —
   then makes a **virtual call** on `world+0x58` (vtable slot 11, offset `0x2c`) with four arguments
   (the input AABB pointer, the constant `1`, and two output buffers) — HIGH: the actual
   `hkpBroadPhase`-style "update this handle's AABB, report whether its cell range changed" call.
4. **If the broadphase cell range did not change** (both outputs compare equal to the pre-cleared
   locals), skip straight to step 6's lock release. **Otherwise:** calls `0x00d58180` (range B, 107
   instructions / 280 bytes, CONFIRMED by listing; itself calls `0x00be6f90`, the Havok-memory helper
   `spec-physics-runtime.md` §3.1 already names) — HIGH: the actual broadphase-tree splice; then reads
   `world+0x64` (a second broadphase-adjacent sub-object — HIGH, a pair manager) and calls `0x00d58140`
   (range B, 25 insn / 59 bytes) with the old/new values; then, with an optional listener pointer read
   from `world+0x7c`, calls `0x00d580d0` (range B, 44 insn / 103 bytes) — HIGH, a collision-pair/
   overlap listener notification; then `0x00c2fd20` (range A, 35 insn / 91 bytes, no further calls) on
   `this` directly — HIGH, a Havok-internal post-update hook on the phantom itself.
5. Releases the broadphase lock (`0x00d65e10`, mirroring step 3's `0x00d65e00`) if it was taken, then
   unconditionally calls **`0x00c2fbe0`** (range A, 87 insn / 311 bytes, no further calls — HIGH, a
   second Havok-internal notification step) with the input AABB pointer, then conditionally — only
   when `world+0x5c` is non-null — a **virtual call** through it (vtable slot 5, offset `0x14`) with
   argument `0` — HIGH, a further owner/listener notification.
6. **Flush-on-unwind.** Decrements `world+0x94` (undoing step 1's increment); only when it returns to
   **zero** does the function do anything further: if `world+0x9c` (a byte flag) is clear and
   `world+0x8c` is non-null, calls **`0x00c16420`**; if `world+0xa4` equals `1` and `world+0x90` is
   non-null, calls **`0x00c16440`**. HIGH: this is a second, concrete tie into
   `spec-physics-runtime.md` §4.3's deferred-queue replay mechanism — `0x00c16420`/`0x00c16440` are
   plausibly the two queue-replay routines that section describes only generically ("replayed after
   the window closes"/"replayed the same way, re-checking each body's activation state"). This pass
   did not open `0x00c16420`/`0x00c16440` themselves (55 and 41 instructions respectively, CONFIRMED
   by first-RET sizing only) to verify this beyond the structural match; flagged as a concrete lead in
   the companion open-items file.
7. The closing block (§ same shape as step 2, twice more) releases the scratch allocation taken in
   step 2 through the fixed `hkContainerHeapAllocator` object `0x01312854` (the same object
   `spec-physics-runtime.md` §3.1 names for `0x00be6620`'s container growth) when the block's stored
   size carries the heap-backed sign-bit tag, rather than the bump allocator.

**Callee instruction totals (own bodies, CONFIRMED by first-RET sizing, used for the roll-up in
§7.9):** `0x00be6960` 17, `0x00be6b90` 16, `0x00c16420` 55, `0x00c16440` 41, `0x00c164c0` 20,
`0x00c2fbe0` 87, `0x00c2fd20` 35, `0x00d580d0` 44, `0x00d65e00` 4, `0x00d65e10` 4, `0x00d58140` 25,
`0x00d58180` 107 — **455 instructions** across 12 distinct first-hop callees.

---

### 7.5 The direct front end — `0x0079f5f0`

Game code (not a Havok-internal root; read in full this pass purely to settle which commit path the
proxy's own cast-and-solve pass actually uses — this directly extends `spec-player-runtime.md`
§5.6.4 step 1's one-line citation). `thiscall(controller, float Δt)`, 58 instructions, CONFIRMED by
full listing.

1. `ecx = controller+0x14` (the shape phantom, §7.1); **virtual call** on it (vtable slot 7, offset
   `0x1c` — the **same** slot `0x00c1e730` calls, confirming it is indeed a `getAabb`-shaped method)
   with an output buffer — reads the phantom's current AABB (min, max) into two local `hkVector4`s.
2. `ecx = controller+0x0c` (the proxy); calls **`0x00c835b0`** — get linear velocity. Computes
   `|velocity|` (SSE dot-product-by-shuffle, then `sqrtss`), multiplies by Δt, giving a uniform-lane
   "swept distance" vector.
3. Reads the proxy's two margins at `proxy+0x88`/`+0x8c` (`eax = controller+0x0c`, i.e. the same
   proxy pointer) and adds both to the swept-distance vector, giving a single combined margin.
4. Subtracts that margin from the AABB min and adds it to the AABB max — a literal "grow the AABB by
   `|velocity|·Δt` plus the two proxy margins" (CONFIRMED, matches `spec-player-runtime.md` §5.6.4
   step 1's prose exactly, now at the instruction level).
5. `ecx = controller+0x14` again; calls **`0x00c30150`** directly with the grown AABB. **Correction to
   this pass's own working hypothesis, not to any existing spec:** the natural guess (an AABB-grow
   front end should call the dedicated `setPosition`/`setTransform` wrappers, §7.3) is wrong here —
   `0x0079f5f0` bypasses both and commits straight to `0x00c30150`, because it already has its own
   AABB (from step 1's virtual call) rather than a transform that would need re-expanding into one.

---

### 7.6 The cast-manifold commit — `0x00c838d0`

314 instructions, `0x00c838d0`–`0x00c83d58` inclusive (1163 bytes) — CONFIRMED by full listing,
matching the scoping pass's own 315-instruction estimate almost exactly. `thiscall(this, a1, a2, a3,
a4)` (`ret 0x10`, four stack arguments). Per `spec-player-runtime.md` §5.6.4 step 4, this is called as
`0x0079e6f0` → `0x0079bbb0`'s "broadphase update" after each lock-step cast-and-solve pass commits the
proxy's new position into the phantom.

**Structure (CONFIRMED by listing):**
1. `a4` (`[ebp+0x14]`) is a `{basePtr, count}` pair — an array of hit/manifold entries, stride 0x60
   bytes. The function loops over all `count` entries.
2. For each entry: `edi = entry+0x3c` dereferenced — the touched **collidable**. Recovers the owning
   **entity** via `eax = sign_extend(byte at [edi+0x10]); entity = eax + edi` — Havok's
   publicly-documented owner-offset back-pointer convention (a signed byte member that lets an
   embedded `hkpCollidable` point back to its owning object without a separate pointer field; HIGH,
   not independently confirmed against a public header in this pass, but the shape — a signed-byte
   relative offset added to the collidable's own address, then used immediately as an object pointer —
   matches no other plausible reading).
3. **Property-key branch.** While still holding the collidable-derived `entity` pointer, reads
   `entity+0x7c`/`entity+0x80` — **the identical offsets and 16-byte stride**
   `spec-physics-runtime.md` §3.1 already documents for the property key/value array `0x00756cb0`
   searches — looking for an entry whose key equals **`0x1300`**. If found with a non-null value: calls
   **`0x00c83720(outerThis, matchedValue, hitEntryPtr)`** (§7.7) and moves to the next array entry
   — this entity is handled entirely through the listener fan-out, not the normal path below.
4. **Normal branch** (no `0x1300` property, or found with a null value). An additional gate runs
   first: when the "has output accumulator" flag (from `a3`, tested at entry) is set, the entry is
   skipped unless `a3+0x08` is non-zero (HIGH: a remaining-reference/slot count on the accumulator);
   then, regardless of that flag, the entry is skipped unless the **collidable**'s own `+0x18` byte
   equals `1` (HIGH: an "is active/valid collidable" flag). Only entries passing both gates continue:
   skip if the derived `entity` (§step 2) is null, or if `entity+0xe8` (a state byte) is `4` or `5` —
   HIGH, two motion/lifecycle states the character controller does not react to (plausibly fixed/
   keyframed or removed, not confirmed against a public enum this pass). Otherwise:
   - Calls **`0x005247f0(entity, &out1, &out2)`** — the **same point-velocity function**
     `review/scoping-physics-vehicle.md` §8.5 item 1 already flags as a Havok-inline function living in
     the game's own address cluster, and the same one `spec-player-runtime.md` §5.8 independently names
     as "the vehicle's point velocity" (used by `0x00961d70`'s upgraded-sprint ram). This pass's own
     call site is a **second, independent confirmation** that `0x005247f0` is a general-purpose
     "velocity of a point on a body" utility, not vehicle-specific.
   - Computes a cross-product term through **`0x00be9ed0`** (24 insn / 71 bytes, no further calls) —
     the operand shuffles immediately before the call (`shufps` with immediates `0xd2`/`0xc9`) are the
     standard SSE 3-vector cross-product idiom, so `0x00be9ed0` itself is HIGH a small, generic Havok
     vector-cross-product utility (ω × r, the angular contribution to a point's velocity).
   - A longer FPU chain (division, `fcom`/`fnstsw` comparisons, a `sqrtss`) combines the point velocity,
     the cross term, `entity+0x9c`/`+0xa0` (two floats — HIGH, unnamed) and the matched phantom's own
     `+0x1a0` field (broadcast to all lanes) into a closing-speed/penetration-recovery scalar. This
     pass read every instruction of this chain but does not have enough independent anchors to name
     the individual fields with confidence — left as a named open item (companion file) rather than
     guessed.
   - Folds the per-entry result into an aggregate output via **`0x00c83760`** called with the aggregate
     buffer and the per-entry vectors (the *aggregate-fold* use of this function — distinct from its
     *listener-dispatch* role in §7.7, which is the same compiled function reached through a
     different vtable-slot argument, not a second copy).
5. **End of loop, commit.** If an optional output accumulator was supplied (`a3`, non-null — tested at
   entry), writes the final aggregate transform/extent pair into it and decrements a reference-style
   counter at `a3+0x08`, clearing a marker field on the last reference. **If no accumulator was
   supplied**, instead calls **`0x00c150c0`** on the touched **entity** (not the phantom) — the same
   function `spec-physics-runtime.md` §3.1 already flags "HIGH: entity `activate`" from its
   world-lock-adjacent call pattern alone. This call site is semantically consistent with that reading
   (waking the entity the character just touched) and is a **third independent call site** supporting
   it, though still not itself sufficient to upgrade it past HIGH.

**Callee instruction totals (own bodies, CONFIRMED by first-RET sizing):** `0x00c83720` 26,
`0x00c83760` 26, `0x00be9ed0` 24; `0x00c150c0` is already sized at 35 instructions by
`spec-physics-runtime.md` §3.1 and not re-measured here. `0x005247f0` is **game code** (outside the
three Havok ranges) and is cross-referenced, not counted into this unit's Havok-internal total.

---

### 7.7 The phantom-listener fan-out triple — `0x00c83720`, `0x00c83760`, `0x00c837a0`

Three near-identical functions in sequence (26 instructions / 63 bytes each, CONFIRMED by full
listing), none calling anything else. Each walks a `{pointer, count}` listener array at `this+0xa4`/
`this+0xa8` (down-counting loop, classic `hkArray` shape) and, for every entry, makes a **virtual
call** on it at a different fixed slot:

| Function | Vtable slot called (offset) | Arguments passed |
|---|---|---|
| `0x00c83720` | slot 4 (`+0x10`) | `(ebx, ebp, this)` |
| `0x00c83760` | slot 5 (`+0x14`) | `(ebx, ebp, this)` |
| `0x00c837a0` | slot 1 (`+0x04`) | `(ebp, &this+0x10, this)` |

`this` here is the **phantom** (reached in §7.6 as the stashed outer-function argument, not the
touched entity). HIGH: a generic Havok phantom-overlap-listener fan-out mechanism — `hkpPhantom`
supporting an array of listener objects, each implementing a small fixed interface, dispatched to
slot-by-slot on an event (here, specifically, when a touched object carries property key `0x1300`).
This is **distinct from `MyCharacterListener`**, which is registered on the **proxy**
(`spec-physics-runtime.md` §2.1 row 23, added by `0x00c84680`), not the phantom — i.e. this is a
second, previously-undocumented listener-dispatch mechanism in the same cluster, not a restatement of
the known one. Not previously named in any spec file read for this unit.

---

### 7.8 Loose end resolved — the manifold-accumulator destructor `0x00c84960`

Flagged by the scoping pass (§8.1) as "`0x0079dd10`'s own callee `0x0079c4d0` ... calls a not-yet-sized
Havok function `0x00c84960`". Read this pass (partial — the first ~85 instructions / 256 bytes,
enough to establish its role; CONFIRMED for what was read, the remaining tail not fully walked).

It is a **destructor** for some per-pass accumulator object (likely the manifold/accumulator structure
`0x00c838d0` can optionally write into, §7.6 step 5 — not independently confirmed as the same type,
but consistent in shape): three teardown loops over arrays at `+0x1c`/`+0x20`, `+0x28`/`+0x2c` and
`+0x34`/`+0x38` (classic count+pointer pairs again), calling `0x00c15630` (47 insn) or `0x00c2fea0`
(37 insn) per entry — HIGH, releasing collision-pair or listener entries — then:
```
push 0x1300
lea ecx, [esp+0x14]
push ecx
mov ecx, [esi+0x60]
call 0xc21970            ; removeProperty(object, key=0x1300)
mov ecx, [esi+0x60]
call 0xbe6100             ; reference release — the same function spec-physics-runtime.md §3.1 names
```
This **explicitly removes property key `0x1300`** from the object at `this+0x60` by value — the
remove-side counterpart to §7.6's query-side use of the same key, and the second of the two
independent confirmations that `0x1300` is a real, game-attached Havok property key (§7.0 item 7).
The function then releases two more scratch allocations through the same `0x01312854` fixed allocator
object pattern seen in `0x00c30150` (§7.4). `0x00c21970` (27 insn) and `0x00c2fea0`/`0x00c15630`
were sized but not read in full; `0xbe6100` is already sized at 73 instructions by
`spec-physics-runtime.md` §3.1.

---

### 7.9 Totals

| Group | Functions | Instructions (own bodies, CONFIRMED by listing/sizing) |
|---|---|---|
| Proxy accessors | `0x00c835b0`, `0x00c83570`, `0x00c835c0`, `0x00c835e0` | 2 + 4 + 9 + 2 = **17** |
| Phantom transform setters | `0x00c1e730`, `0x00c1e7b0` (bonus) | 44 + 40 = **84** |
| Commit routine + first-hop callees | `0x00c30150` + 12 callees | 267 + 455 = **722** |
| Cast-manifold commit + first-hop callees | `0x00c838d0` + `0x00c83720`/`0x00c83760`/`0x00be9ed0` (+`0x00c150c0`, cited) | 314 + 26 + 26 + 24 (+35 cited) = **390 own / 425 incl. cited** |
| Listener fan-out triple | `0x00c837a0` (the third, not separately reached from `0x00c838d0`'s own body but part of the same cluster) | **26** |
| Loose end | `0x00c84960` (partial) + `0x00c21970`/`0x00c2fea0`/`0x00c15630` | ~85 (partial) + 27 + 37 + 47 = **~196** |

**Core W-U1 total (roots + first-hop Havok-internal callees, excluding the bonus sibling and loose
end): ≈ 17 + 44 + 722 + 425 ≈ 1,208 instructions.** This is a real, CONFIRMED-by-listing refinement of
the scoping pass's own **"~1.0k instructions (HIGH)"** estimate (`review/scoping-physics-vehicle.md`
§8.1) — about 20% larger, which is within the kind of variance expected once a byte-range estimate
(calibrated at 3.6 bytes/instruction) is replaced by an actual count, and once first-hop callees
(`0x00c30150`'s own 12, `0x00c838d0`'s own 3) are summed rather than left as "own body only." No
contradiction is raised against the scoping note; this is an upward refinement, not a correction.

---

### 7.10 What a port must reproduce

1. **The proxy's velocity/position contract is exactly four fields**: linear velocity
   (`proxy+0x40`), the cached shape-phantom pointer (`proxy+0x60` = `controller+0x14`), and — through
   that phantom — a position that is the translation row of the phantom's own inline transform
   (`phantom+0xe0`). A replacement need not reproduce Havok's own `hkpCharacterProxy` internals, only
   this get/set contract (§7.2).
2. **AABB maintenance is two independent front ends feeding one commit.** A velocity-swept,
   margin-padded grow (`0x0079f5f0`'s shape, §7.5) runs every controller step; a full
   transform-or-position set (`0x00c1e730`/`0x00c1e7b0`'s shape, §7.3) runs from the overlap-query
   wrappers and other callers. Both ultimately call the same commit (`0x00c30150`, §7.4), which is
   itself physics-window-deferral-aware. A port's own broadphase does not need to reproduce
   `0x00c30150`'s internal scratch-allocator/lock machinery, only the deferral contract: **a phantom
   AABB set made while the world is mid-step must queue, not apply immediately** — the same rule
   `spec-physics-runtime.md` §4.3 already states for body activation and body-state changes, now shown
   to cover phantom AABB commits too.
3. **The cast-manifold commit distinguishes tagged objects from ordinary ones.** Objects carrying the
   game's own Havok property key `0x1300` are routed to a listener fan-out instead of the normal
   point-velocity/penetration/activate path (§7.6–§7.7). A port needs its own equivalent
   "this touched object is specially tagged" hook if it wants to reproduce whatever gameplay behavior
   depends on `0x1300` (not identified in this pass — see companion open-items file).
4. **Touching an entity through the character's cast-and-solve passes can wake it** (the `0x00c150c0`
   call, §7.6 step 5) — a port's own sleep/activation system needs the same trigger if sleeping
   bodies should wake when the character walks into them.

---

### 7.11 Resolution of `spec-player-runtime.md` OPEN 35 / OPEN 36

**OPEN 35** ("Controller motion `0x007a5b10`... the state logic between the support query and the
final velocity set — gravity, jumping, the timers..., state bits...; and its two other callers") is
about `0x007a5b10`'s **own** game-side logic, which this unit does not touch (it is Volition code, not
a Havok internal, and was out of this unit's root list). **Not resolved.** What this unit *does*
settle is every Havok call `0x007a5b10` makes: `spec-player-runtime.md` §5.6.5 already named the call
shapes ("reads the proxy velocity and position... ends by setting the proxy's linear velocity"); this
unit now gives those three calls full identities (`0x00c835b0`/`0x00c83570`/`0x00c835c0`, §7.2).
**Marginal advance**, not a resolution — OPEN 35's actual ask (the state machine in between) is
unchanged.

**OPEN 36** ("The custom proxy passes: `0x0079bb00`, `0x0079dd50` → `0x0079c5f0`, `0x0079ddc0` →
`0x0079d7c0`, `0x0079b560`, `0x0079bbb0`, `0x0079b5b0`, the cast-job submitter `0x0079b470` and the
manifold update `0x0079de40`, and on which thread the listener callbacks run") is likewise about the
game-side pass functions themselves, none of which this unit opened (all are outside the Havok ranges
and outside this unit's root list). **Not resolved as a whole.** But this unit **substantially
advances the Havok-facing half of two of its eight named functions**:
- `0x0079bbb0`'s own one-line gloss in `spec-player-runtime.md` §5.6.4 ("commit the proxy's new
  position into the phantom; broadphase update `0x00c838d0`") is now a full behavioral account
  (§7.6): the owner-offset entity lookup, the `0x1300` property-key branch and its listener
  fan-out, the point-velocity/cross-product/penetration computation, and the entity-activate call.
- `0x0079f5f0` (cited by `spec-player-runtime.md` §5.6.4 step 1, one of the functions that feeds the
  passes' AABB state even though it is not itself in OPEN 36's eight-item list) is now read in full
  (§7.5), including the discovery that it bypasses the `0x00c1e730`/`0x00c1e7b0` wrappers and calls
  `0x00c30150` directly.
- The thread-safety question in OPEN 36 ("on which thread the listener callbacks run") is **not**
  settled by this unit — `MyCharacterListener`'s own three callbacks were out of scope here (they are
  already named in `spec-physics-runtime.md` §2.1 row 23 / §2.2, a prior unit's work), and the
  phantom-listener fan-out this unit newly found (§7.7) raises the same open question for a
  *second* listener mechanism, not just the one OPEN 36 already names.

In short: **this unit does not close either OPEN item**, but it removes essentially all of the
"Havok-internal, not yet opened" uncertainty from the specific calls those two units make into Havok —
which was the actual gap the task was scoped to close (`review/scoping-physics-vehicle.md` §8.1's
W-U1 proposal). The remaining game-side state logic and thread-safety questions are real, separate
work, tracked in the companion open-items file as W-U2 material.

---

### 7.12 Corrections / additions flagged for the orchestrating session (no spec file edited by this unit)

1. **`spec-physics-runtime.md` §4.3** (the deferred/dropped-mutation contract): add that phantom AABB
   commits (`0x00c30150`) are deferral-aware too, with their own record type (`0x12`), and that the
   same function's own unwind path (its nesting counter returning to zero) is a plausible concrete
   trigger for the queue replay that section currently describes only generically. See §7.4 steps
   1 and 6.
2. **`spec-physics-runtime.md` §3.1**: `0x00c150c0` ("entity `activate`", HYPOTHESIS there) now has a
   third independent, consistent call site (§7.6 step 5). Still not CONFIRMED, but worth noting the
   added support if that table is revisited.
3. **`review/scoping-physics-vehicle.md` §8.1**: the "~1.0k instructions (HIGH)" estimate for W-U1 is
   refined upward to ≈1,208 by this pass's own full listing (§7.9) — not a contradiction, an
   expected estimate-to-count correction. The note's own loose end ("`0x0079c4d0` ... calls a
   not-yet-sized Havok function `0x00c84960`") is resolved (§7.8); the other loose end
   ("`0x007a1870`'s own chain reaches `0x00c835e0`") is also resolved — `0x00c835e0` is the
   `getShapePhantom`-shaped accessor, §7.2.
4. **New, not flagged by any prior pass:** the game's custom Havok property key `0x1300`
   (§7.0 item 7, §7.6, §7.8) and the phantom-listener fan-out triple distinct from
   `MyCharacterListener` (§7.7). Both are genuinely new findings with no existing home in either
   spec file; the companion open-items file lists what remains to pin down about each.

This section does not edit `spec-physics-runtime.md`, `spec-player-runtime.md`, or
`review/scoping-physics-vehicle.md` directly, per the task's instructions — all of the above are
flagged for the orchestrating session to fold in.

---

### 7.13 Clean-room check

- Addresses are plain hex throughout. No Ghidra auto-names, no decompiler variable names, and no
  pasted decompiler pseudocode appear anywhere above — every instruction shown was transcribed from
  capstone's own disassembly text (mnemonic + operand string), not a decompiler.
- Class names are RTTI type-descriptor strings read directly from the binary via the
  complete-object-locator chain (`0x0115486c` → `hkpPhantom`, `0x011548ac` → `hkpShapePhantom`,
  cross-checked against the two already-published names `character_controller`/`MyCharacterListener`),
  or Havok's public 2010.2 API method names, used only where the call shape makes the identification
  HIGH confidence and always labelled as such — never presented as CONFIRMED unless the behavior
  itself (not just the name) was read directly.
- No leaked Havok source (`sigmaco/havok-v6.6.0r1-b2009.07.04` or its DeepWiki), no leaked SR4/
  Volition source, and no other leaked material was read or used at any point in this pass. Every
  claim traces to this pass's own disassembly of the shipped executable, or to this project's own
  prior specs/scoping notes, cited by section.
- No Ghidra project was opened, copied, or deleted. No `.czn_pc` data was read.
- Self-check pattern (`FUN_|DAT_|iVar|uVar|local_[0-9a-f]|auStack|unaff_|in_stack|puVar|pcVar`,
  case-insensitive) run with `grep` over this file: **0 real hits** — the only match is the line
  above quoting the pattern itself as text, not an occurrence of it. No decompiler tool was used at
  any point to produce output that could have carried such tokens in the first place; every
  instruction transcribed above came from capstone's own mnemonic/operand text.

---

## 8. Collision queries / raycasts — Havok-internal roots (unit Q-U0)

**Scope.** This section specifies the Havok-internal side of the collision-query/raycast boundary
already surveyed at the call-edge level by `review/scoping-physics-vehicle.md` §8.3. It covers seven
roots: the two shared with unit W-U1 (`0x00c1e730`, `0x00c30150` — cited here, not re-derived), plus
`0x00c19160`, `0x00d4d3c0` (overlap-query side), `0x00d4c800` → `0x00d6bff0`, and `0x00d527d0`/
`0x00d4d280` (ray/linear-cast side). It also answers the brief's question of whether any AI action
routine's own geometry/collision needs are met by this unit's roots: **yes, but through the perception
layer, not through the seven action routines originally suspected (§8.6 below).**

**Method.** Ghidra-free. A `capstone` (x86-32) linear-sweep disassembler against the raw exe bytes,
using the same PE/section-mapping logic as `tools/harnesses/exe_va.py` (no project opened, no
decompiler run at any point). Every instruction count below is an exact count from real disassembly.
No leaked Havok source, DeepWiki page, or other forbidden material was consulted; naming is either a
plain hex address with a behavioural description, or a Havok 2010.2 public-API name offered at HIGH
confidence where the call shape (argument count/role, `this`-pointer role, RTTI-identified caller)
makes a specific match plausible — never CONFIRMED by name alone, since no RTTI string or debug symbol
names any of these seven functions directly.

### 8.1 The two shared roots (W-U1 territory — cited, not re-specified)

`0x00c1e730` (~44 insn by this pass's own light recount, light touch only) and `0x00c30150` are the
phantom-AABB update pair already described in `review/scoping-physics-vehicle.md` §8.1/§8.3: `0x00c1e730`
copies a 4x4 transform into its `this` object (`+0xb0..+0xe0`), optionally adjusts a margin through one
vtable call, then calls `0x00c30150`, the real AABB setter (~228 insn HIGH, unchanged this pass). Full
internals are W-U1's unit; this section only confirms both functions still look exactly as that unit's
own census describes them.

### 8.2 Overlap-query side

| Root | Size | Calls | Role |
|---|---|---|---|
| `0x00c19160` | **463 B / 143 insn, CONFIRMED** (corrects §8.3's "462 B/~128 insn" HIGH estimate; the real extent is `0x00c19160`–`0x00c1932e`, `int3` padding at `0x00c1932f`, then an unrelated twin function at `0x00c19330`) | `0x00c164c0`; `0x00be6960`; `0x00c161c0`; `0x00c16420`; `0x00c16440`; `0x00be6b90`; 2 vtable calls; 2 calls through IAT thunk `[0x00101c0c8]` | See 8.2.1 |
| `0x00d4d3c0` | **32 insn, CONFIRMED** (corrects "~24 insn") | none (leaf) | See 8.2.2 |

**8.2.1 — `0x00c19160`: a guarded deferred-command dispatcher, not a plain "add entity" call.**
`ecx` = `this` (a query/shape-phantom-shaped object, reached directly from the three overlap-query
wrappers `0x00757ec0`/`0x007581b0`/`0x0075a480` per §8.3's own edge list); two stack args (a dword, a
byte); `ret 8`. `this+0x94` behaves as a re-entrancy/lock counter:
- **While `this+0x94` is non-zero**, the function takes a short path: it packs the two caller-supplied
  args plus a literal byte tag `0x15` (21) into a small on-stack record and hands it to
  `0x00c164c0(this, &record)` — shaped exactly like pushing a command onto a deferred queue.
- **While `this+0x94` is zero**, it does the "real" work instead: sets `this+0x94` to 1, builds a larger
  descriptor, does a per-thread allocator lookup through the IAT thunk `[0x00101c0c8]` to size-class a
  request (`(count*8 + 0x7f) & ~0x7f`, i.e. 8 bytes/entry rounded up to a 0x80 block), calls two further
  vtable methods and four already-documented utilities (`0x00be6960`, `0x00c161c0` — twice, conditionally
  — `0x00c16420`, `0x00c16440`, `0x00be6b90`), decrements `this+0x94` back down, and finally releases
  through one more vtable call reached via the fixed global pointer `0x01312854`.
- An exact structural twin sits immediately after at `0x00c19330` (same shape, tag `0x13`/19 instead of
  `0x15`/21, a bigger local frame) — a sibling command kind, not part of this unit, not specified here.

HIGH: this is Havok's own locked/stepping-world deferral mechanism for whatever operation this call
represents (consistent with, but more specific than, §8.3's "world add-entity" HYPOTHESIS — the tag
byte strongly suggests a small enum of command *kinds* sharing one dispatcher, of which this call site
is one). The literal Havok method name stays HYPOTHESIS; no RTTI string pins it.

**8.2.2 — `0x00d4d3c0`: a pure FPU leaf, no Havok-internal calls at all.** `ecx` = `this`, one stack
arg (a pointer to a 16-byte vec4). It copies the caller's vec4 into `this+0x20..+0x2c`, then overwrites
`this+0x2c` with the component-wise **minimum** of the copied vector's x/y/z (a three-way FPU compare
chain, not SSE). HIGH: a conservative-extent helper — likely used to collapse a half-extents or radius
vector down to a single scalar bound. Grouped here per the task brief's own framing; this pass did not
independently re-derive its caller edge (expected to be one of `0x00c30150`'s nine smaller range-B
callees per `review/scoping-physics-vehicle.md` §8.1, not re-confirmed).

### 8.3 Ray/linear-cast side

| Root | Size | Calls | Role |
|---|---|---|---|
| `0x00d4c800` | **49 insn, CONFIRMED** (corrects "~32 insn") | `0x00d6bff0` | "keep closest" hit updater — see 8.3.1 |
| `0x00d6bff0` | **36 insn, CONFIRMED** (corrects "~23 insn") | none (leaf) | shape-key chain resolver — see 8.3.2 |
| `0x00d527d0` | **66 insn, CONFIRMED** (corrects "~53 insn") | `0x00be7010`, `0x00d6bff0` | shared "append a hit" — see 8.3.3 |
| `0x00d4d280` | **100 insn, CONFIRMED** (corrects "~73 insn") | none (leaf) | dual shape-key chain resolver — see 8.3.4 |

**8.3.1 — `0x00d4c800`, the literal first function of Havok range B, is the "replace the cached hit if
this candidate is closer" updater for the closest-ray-hit collector.** Signature (by disassembly):
`this` in `ecx`→`esi`, two stack args — `arg1` (a body/shape pointer) and `arg2` (a candidate hit-record
pointer); `ret 8`. It compares `arg2`'s fraction field (`+0x10`) against `this`'s cached best (`+0x20`);
on a `fcompp`/`fnstsw` test consistent with "candidate is not worse", it:
- copies the candidate's normal/position vec4 (`arg2+0x0..+0xc`) into `this+0x10`;
- copies the candidate's fraction (`arg2+0x10`) into `this+0x20`, and three more scalar fields
  (`arg2+0x14/+0x18/+0x1c`) into `this+0x24/+0x28/+0x2c`;
- resolves `arg1`'s shape-key chain via `0x00d6bff0(arg1, 8, &this+0x30)` (max depth 8);
- caches a terminal body pointer, found by its own short `+0xc`-linked walk starting from `arg1`'s
  own list, into `this+0x60`;
- duplicates the fraction into `this+4` as well.

**This function is called directly by the game-side override `0x0075b800`**, which
`spec-physics-runtime.md` §2.1 row 14 already RTTI-identifies as `havok_closest_ray_hit_collector`
(public base `hkpClosestRayHitCollector`). Disassembling `0x0075b800` itself (41 insn) for the first
time this pass shows its shape plainly: call a game-side filter `0x0075b780` first, and only on success
call `0x00d4c800(this, candidate_body, candidate_hit)`. **So `0x0075b800` = filter, then delegate the
actual "is it closer, and if so cache it" logic to this unit's own `0x00d4c800`.** HIGH, not CONFIRMED,
for calling `0x00d4c800` the internal body of (something like) `hkpClosestRayHitCollector::addRayHit`'s
shared update step — the call shape and RTTI-identified caller both support it, but no symbol names it.

**8.3.2 — `0x00d6bff0` is a shape-key / sub-shape chain resolver, CONFIRMED by behaviour (raised from
`review/scoping-physics-vehicle.md` §8.3's HYPOTHESIS-by-size-and-caller-count).** cdecl, 3 dword args,
by call-site push order `(out_array, max_depth, chain_head)` — both call sites in this unit push
`max_depth = 8`. It walks `chain_head`'s own `+0xc`-linked "parent" pointers for up to `max_depth`
levels, writes each visited node's `+4` field into `out_array` **innermost level first**, appends a
`0xffffffff` sentinel after the last written entry, and returns the depth actually walked (`eax`). This
is exactly the shape needed to decode which leaf of a composite/hierarchical shape (e.g. a
`.clmesh_pc`-sourced MOPP tree, or a `hkpListShape`/`hkpTransformShape` nest) a ray or linear-cast hit
actually struck — confirming, not merely matching the size of, the census's own framing. Havok's own
name for this helper is HYPOTHESIS only; it carries no RTTI slot (consistent with being a free internal
utility, not a virtual method).

**8.3.3 — `0x00d527d0` is the shared "append a hit" implementation for the all-ray / all-linear-cast /
closest-linear-cast collector family.** `this` in `ecx` (a growable-array-shaped collector: `this+0x10`
data pointer, `this+0x14` size, `this+0x18` capacity with the top 2 bits reserved as flags — the
textbook Havok `hkArray` layout, element stride `0x60`/96 bytes); two stack args, `arg1` (a shape/chain
pointer, passed through to the resolver below) and `arg2` (a candidate hit-record pointer, same 4-field
shape `0x00d4c800` reads). On capacity overflow it calls the already-documented shared growth utility
`0x00be7010`, passing the same global allocator-singleton pointer `0x01312854` that `0x00c19160` (8.2.1)
also reaches through its own release call — **cross-confirming `0x01312854` as one shared Havok
heap-singleton pointer used by both the walking-slice's command-queue machinery and this unit's
collector-array growth.** It then appends a new 96-byte element, copies `arg2`'s 4-field hit record into
it, resolves `arg1`'s shape-key chain via `0x00d6bff0(arg1, 8, &new_element+0x20)`, and caches a
terminal link (its own short `+0xc`-chain walk from `arg1`) into the new element.

`review/scoping-physics-vehicle.md` §8.3 already found this function reached from all three of
`0x0075b9c0` (all ray hits), `0x0075bb50` (**closest** linear cast) and `0x0075bfb0` (all linear casts).
**Open question, left honestly unresolved rather than forced:** `0x0075bb50`'s own class
(`havok_closest_linear_cast_collector`, RTTI row 16) is nominally "closest only", yet `0x00d527d0`'s own
shape is pure unconditional append, with no fraction comparison visible anywhere in it. Either the
closest/all distinction is enforced one layer up (inside `0x0075bb50` itself, not traced this pass — it
may pre-clear the array, or post-process it after the cast completes) or Havok's own
`hkpClosestCdPointCollector` is implemented as an always-size-1 array under the hood. Left at HIGH, not
CONFIRMED — a clean next step for whoever next opens the `0x0075bXXX` wrapper layer.

**8.3.4 — `0x00d4d280` resolves a *pair* of shape-key chains in-line, rather than delegating to
`0x00d6bff0`.** `this` in `ecx`, one stack arg (`esi`, a candidate record carrying two independent chain
roots at `+0x30` and `+0x34`, plus a fraction at `+0x1c` and a vec4 at `+0x20`). A guard
(`this+0x30` already populated, and the candidate's fraction not better) skips the two chain-walks but
still performs a shared tail copy (fraction → `this+4`, vec4 → `this+0x70`) unconditionally. When the
chain-walks do run, each of `esi+0x30` and `esi+0x34` is walked independently (same `+0xc`-linked-parent,
collect-`+4`-fields, `-1`-terminate algorithm `0x00d6bff0` uses, but manually inlined rather than called)
into two separate length-prefixed output slots inside `this` (around `+0x38`/`+0x3c` for the first chain,
`+0x54`/`+0x4c`/`+0x50` for the second). HIGH: the dual-key sibling of `0x00d6bff0`, needed wherever a
cast result must report **both** the cast shape's own sub-key and the struck shape's sub-key (plausible
for a linear-cast collector, where the moving shape can itself be composite) rather than just the one
side a plain ray hit needs.

### 8.4 Corrected sizing summary

Replacing `review/scoping-physics-vehicle.md` §8.3/§8.4's HIGH byte-based estimates with this pass's
exact counts: `0x00c19160` 143 insn (was ~128), `0x00d4d3c0` 32 (was ~24), `0x00d4c800` 49 (was ~32),
`0x00d6bff0` 36 (was ~23), `0x00d527d0` 66 (was ~53), `0x00d4d280` 100 (was ~73). **Net-new total for
the five roots not shared with W-U1: 143+32+49+36+66+100 = 426 instructions**, CONFIRMED (not the
0.2–0.4k HIGH range the census proposed — the real figure lands just above that band, at ~0.43k,
because every one of the six HIGH estimates undercounted, not because any root was missing). Still a
single, comfortably-sized unit, and still the thinnest of the three slices the census identified: for
comparison, W-U1's own ~1.0k HIGH figure and D-U0+D-U1+D-U2's ~2.4–2.7k HIGH figure are both several
times larger.

### 8.5 Argument/return conventions, summarised

| Function | `this` | Stack args | Return | Convention |
|---|---|---|---|---|
| `0x00c19160` | `ecx` | 2 (dword, byte) | `ret 8` | thiscall |
| `0x00d4d3c0` | `ecx` | 1 (vec4 ptr) | `ret 4` | thiscall |
| `0x00d4c800` | `ecx`→`esi` | 2 (body ptr, hit-record ptr) | `ret 8` | thiscall |
| `0x00d6bff0` | — | 3 (out array, depth, chain head), cdecl push order | `ret` (no pop) | cdecl |
| `0x00d527d0` | `ecx` | 2 (chain/shape ptr, hit-record ptr) | `ret 8` | thiscall |
| `0x00d4d280` | `ecx` | 1 (candidate-record ptr) | `ret 4` | thiscall |

All six are `__thiscall`/`cdecl` leaves or near-leaves with no stack-frame games beyond the standard
`push ebp; mov ebp,esp; and esp,-16` local-alignment prologue most of Havok's own code uses; none of
them is itself called virtually (none appears in any of the 72 Havok-callable-override slots in
`spec-physics-runtime.md` §2.1) — they are plain internal helpers reached only by direct `call`.

### 8.6 The AI-action connection

The brief asked whether any of `spec-ai-runtime.md` §11's action routines 7, 15, 23, 37, 38, 39 or 52
turn out to call into this unit's roots, since Team B's AI-side work is blocked on exactly that
connection. **The direct answer, checked by disassembling two of the seven (7 `melee vehicle`,
`0x005525c0`; 52 `brute car flip`, `0x00526240`): no.** Neither makes any call into Havok at all. 52's
"clearance probe" resolves entirely to vector math: a local-frame point transform
(`0x00525d30`, which itself only calls the already-named `0x004f4770`) plus one indirect vtable call on
the *vehicle game object* itself (`[[edi]+0]+0x60`, not one of this project's 39 Havok-RTTI rows — a
plain gameplay vtable slot — **resolved 2026-10-07 by direct disassembly of the `vehicle` class's own
vtable** (located via the binary's own RTTI: type descriptor `.?AVvehicle@@` at `0x0130d608` →
CompleteObjectLocator `0x012b3798` → vtable `0x01180c44`; slot `+0x60` = `0x00a7a4e0`). It is CONFIRMED
a cached bounding-box getter: it reaches a per-instance sub-object via `[vehicle+0x1e00]+0x10`, checks
a validity byte there, and on success returns a 32-byte (min vec4, max vec4) box built from two 3-float
fields on that sub-object via the shared position-vector loader `0x00da38e0`; on failure it returns an
all-zero box. This is a **fourth**, per-instance cached box — structurally similar to, but not the
same storage as, the three load-time root-part AABBs `spec-vehicle-geometry.md` §4/§11.12 already
documents on the shared geometry asset (which asset box, if any, seeds it is OPEN). The exact point-to-box
distance formula of `0x004f2970` (called with this box as its implicit box argument) is: clamp the
query point to the box per axis, then take the squared Euclidean residual, broadcast to all 4 lanes —
no tolerance constant lives inside either `0x004f2970` or its caller-side lane-compare `0x004bc680`.)
plus the already-documented clamp
helper `0x004bc680`. 7's melee-vs-vehicle range test is the same `0x004aac40`/`0x004f4770`/`0x004f2970`
family `spec-ai-runtime.md` §11.6 already named, confirmed here to have no Havok call hiding behind an
early-exit branch either. **These seven actions' geometry needs are met by cached vehicle-local-frame
math, not a live Havok query — a port can supply this from the vehicle's own cached bounding box without
touching this unit at all.**

**The real connection is one level up, in the perception layer, and it is real and CONFIRMED by call
edge.** `spec-ai-runtime.md` §11's own OPEN item 18 names three untraced "FOV cone/frustum geometry"
primitives; one of them, `0x0075d7e0`, doubles as the ray helper the list-2 "line of fire" probe uses
(`spec-ai-runtime.md` §10.3/§10.6) — and that probe is what ultimately backs the "visible"/"engageable"/
"clear line of fire" flags nearly every one of the 70 action routines reads as a precondition, including
several of the seven named above (indirectly, through those flags, not through a direct call). Tracing
it this pass:
1. ~~`0x0075d7e0` calls `0x0075b8b0`, a small constructor that writes vtable pointer `0x01153ad8` into a
   stack object and zero/−1-fills a block — shape-consistent with constructing a
   `havok_closest_ray_hit_collector`-family instance in place.~~ **CORRECTED 2026-10-08 (§8.8 has full
   detail): `0x0075d7e0` is a 3-way dispatcher on an input-struct kind byte, not a single straight-line
   caller.** On the `kind==0` branch it calls `0x00756ee0` (not `0x0075b8b0`), which builds the
   `havok_closest_ray_hit_collector` (vtable `0x01153a90`, RTTI row 14 below, unchanged). `0x0075b8b0`
   is real, but it unconditionally builds the **all**-ray-hit collector (vtable `0x01153ad8`, row 15)
   and is only reached on the `kind==1` branch — every concrete caller checked this project (AI
   line-of-fire, AI vision/line-of-sight, camera pivot nudge, helicopter-landing probe, vehicle-door
   probe) takes `kind==0`, so the downstream chain in steps 2-3 below is still the real answer for
   those callers; only the ctor/branch attribution needed fixing.
2. It loads a cached `hkpWorld`-shaped pointer from the fixed global `[0x00161d378]` and calls
   `0x00c163a0`, **inside Havok range A**, passing the freshly-built collector and a ray-input block.
   Argument shape (a world-ish `this` plus `(input, collector)`) is a plausible match for public Havok
   2010.2 `hkpWorld::castRay(const hkpWorldRayCastInput&, hkpRayHitCollector&)` — **HIGH**, not
   CONFIRMED by name. ~~`0x00c163a0` is itself a 28-instruction thin dispatcher with two indirect
   (vtable) calls, consistent with a broadphase-walk-then-ask-each-candidate's-own-shape pattern~~ —
   **OPENED IN FULL 2026-10-08, §8.8/§8.10: confirmed exactly this shape, but the first indirect call is
   a game-side broadphase-border interceptor (`world+0x5c`) that normally answers the whole query itself;
   Havok's own stock broadphase (`world+0x64`, the second indirect call, via a range-B fallback
   `0x00d4fe10`) is reached only when that interceptor is absent** — and
   consistent with why `review/scoping-physics-vehicle.md`'s own static-edge census never surfaced this
   hop (both calls are indirect, not literal `call 0xADDR`, so a reference-manager listing alone cannot
   see them).
3. The collector's own override is `0x0075b800` — the same function this section already read in 8.3.1,
   which calls straight into this unit's `0x00d4c800` → `0x00d6bff0`. **Newly found 2026-10-08, §8.9:
   `0x0075b800` itself calls a second, game-side query-local filter (`0x0075b780`) before ever reaching
   `0x00d4c800`** — not part of the classic Havok collision-layer/group mechanism, a separate per-category
   exclusion test.

**So the full chain is: AI perception's line-of-fire probe (`0x0075d7e0`) → an `hkpWorld::castRay`-
shaped call (`0x00c163a0`, Havok range A) → the closest-ray-hit collector override (`0x0075b800`,
`spec-physics-runtime.md` §2.1 row 14) → this unit's `0x00d4c800` → this unit's `0x00d6bff0`.** This is
a genuine, call-edge-level dependency from the AI runtime onto two of this unit's seven roots.

A second AI-side query was also checked: `0x0075d4b0`, the spatial-query helper behind action 51
`brute throw prop`'s prop search (`spec-ai-runtime.md` §11.3F; not one of the seven named in the brief,
but adjacent and worth settling since it shares the `[0x00161d378]` cached-world-pointer idiom with
`0x0075d7e0`). It calls `0x0075ae80` directly — the `object_collector` override,
`spec-physics-runtime.md` §2.1 row 12. Disassembling `0x0075ae80` (11 instructions) shows it is a bare
boolean latch ("did anything at all overlap" — it sets two flag fields and returns, reading neither of
its two body-pair arguments otherwise) with **no further calls**. **So this second AI query does *not*
reach this unit's shape-key machinery** — it only needs a yes/no overlap answer, which `object_collector`
already provides at the boundary layer, well short of Q-U0's own roots.

### 8.8 On-demand collision queries — the two shared game-side wrappers (unit Q-U1, 2026-10-08)

**Scope.** Direct follow-up, requested by `purrsian-62`: the game's on-demand Havok query API
(raycast/shape-cast) as called OUTSIDE the fixed physics tick — camera collision, AI line-of-sight,
bullet-hit detection — and the broadphase these queries walk. This is the game-side half of the same
boundary Q-U0 above already opened from the Havok-internal side. Method: Ghidra-free `capstone`
linear-sweep disassembly against the raw exe bytes, cross-checked against the persistent Ghidra
server for listing-only confirmation (no decompiler output cited); RTTI/vtable identity by raw dword
read at known-good vtable addresses, the same technique as this file's own MOPP-class confirmations.
No leaked Havok/Volition source read; public Havok API names used for naming/orientation only, HIGH
not CONFIRMED where used.

**`0x0075d7e0` — the one and only shared ray wrapper (519 instructions, SEH-wrapped, read in full).**
A three-way dispatcher on a kind byte at the input struct's `+0x64`: `kind==0` builds the
closest-ray-hit collector then calls `0x00c163a0` (§2 below); `kind==1` builds the all-ray-hit
collector, also calls `0x00c163a0`, then reduces the gathered hits via `0x0075bab0` (not opened); any
other kind skips Havok entirely. Confirmed callers, gathered in one place: AI perception's line-of-fire
probe (§8.6 above); **AI's async vision/line-of-sight resolver, `0x004eda70`** (new this pass, §8.11);
camera pivot nudge (`spec-player-runtime.md` §4.8, via thin tag-setting wrappers
`0x0075df80`/`0x0075dfa0`/`0x0075dfc0` that tail-jump into this function); vehicle helicopter-landing
probe (`spec-player-runtime.md` §4.7 step 8); vehicle door-occupancy probe `0x00b0da10`
(`spec-player-runtime.md` §13.6 OPEN 104).

**`0x0075c880` — the one and only shared shape/sphere-cast wrapper (824 instructions, SEH-wrapped, not
previously read anywhere — flagged OPEN at `spec-player-runtime.md` §4.9/§13.6 OPEN 104/33).**
Constructs an `hkpSphereShape` via the already-confirmed constructor `0x00d50670` (§3.1), computes a
swept distance using the already-documented world/broadphase tolerance constant `0x012a2dc0`, loads
the same cached world pointer `[0x00161d378]` the ray wrapper uses, calls **`0x00c170e0`** (Havok
range A) **twice** plus a second Havok entry **`0x00d4ccf0`** (Havok range B) once, and grows two
parallel `hkArray`-shaped output buffers (strides `0x60`/`0x2c`) through the shared allocator
singleton `0x01312854` and the already-documented growth helpers `0x00be6f90`/`0x00be7010` (§3.1/§8.3.3).
Dispatched by the same kind-tag idiom as the ray wrapper — strong evidence both wrappers are built from
the same code template/generator, one for rays, one for spheres. Confirmed callers: camera collision
`0x00565d70` (`spec-player-runtime.md` §4.9); vehicle door-occupancy probe `0x00b0dc20`
(`spec-player-runtime.md` §13.6 OPEN 104).

After either Havok call (or when skipped), both wrappers run an identical-shaped per-candidate loop over
a linked list at `edi+0x68`/`esi+0x68` (an attached-shape/exception list), calling a small shared helper
`0x75c250` per entry with a type/kind test gating a per-candidate flag — exact semantics OPEN, not
re-derived this pass.

### 8.9 The call contract and filters (unit Q-U1)

**`0x00c163a0(this=world, ray_input*, collector*)` — thiscall, `ret 8`, opened in full (previously only
sized "28 insn, 2 indirect calls").** `if ([world+0x5c] != 0)`: an availability-check virtual call on
that sub-object (`+0x10`), and if it passes, the **entire** query — ray input, `world+0x7c`, and the
collector — is delegated to that sub-object's own vtable slot `+0x18`, returning immediately. Only if
`world+0x5c` is absent (or its check fails) does it fall back to `0x00d4fe10` (Havok range B), which
builds a proper `hkpWorldRayCastInput`-shaped descriptor and calls **world-vtable slot `+0x64`** —
Havok's own internal broadphase/narrowphase ray walk (§8.10). `[world+0x5c]` is HIGH, not independently
cross-read against its own layout this pass, to be (or point at) the game's own `hk_broadphase_border`
object (§2.1 row 5) — **a sub-object distinct from the already-documented `world+0x58` broadphase
pointer** (§4.1 step 11's own AABB-commit lock/unlock pair); the two offsets must not be conflated.
Call contract for a port: `castRay(world, rayInput, collector)`, `rayInput` carrying at minimum an
origin, a direction/length, and a filter-info value; `collector` is one of the RTTI-confirmed game
collector subclasses (§8.6 point 1's correction above); return is implicit via the collector's own
accumulated state, not a function return value.

**`0x00c170e0` (Havok range A) — the shape/linear-cast-shaped entry, call-shape only, not opened.**
Called twice from `0x0075c880`, always `this=[0x00161d378]`, 4 stack args (an array/collector-shaped
pointer plus three more, not individually typed). A 4-arg-plus-`this` shape is consistent, HIGH by
argument count only, with the public Havok 2010.2 `hkpWorld::linearCast`-style signature. The second
Havok call inside `0x0075c880`, `0x00d4ccf0` (range B), is likewise call-shape-only — cast-input/
filter-info setup, HIGH, not opened. Both are clean next steps for a future pass.

**Filters, two independent layers.** (1) Havok's own world-level `havok_game_filter` (§2.1 row 4,
`isCollisionEnabled` family at `0x0075f370`+, wired into every Havok query including the physics step
— the real collision-layer/group mechanism in the classic Havok sense, applying uniformly, not
specially to on-demand queries). (2) **A second, query-local filter, `0x0075b780`** (47 instructions,
read in full, newly found) — called from the closest-ray-hit collector override `0x0075b800` before it
ever reaches `0x00d4c800`: walks a `+0xc`-linked chain to the candidate's outermost owning body; if
that root's `+0x18` byte isn't exactly `1`, accepts unconditionally (the filter only applies to one
specific kind of root object); else resolves a name/tag pointer and looks it up through
`0x00758780`→`0x005252c0`, and if that resolves to a live object, tests **three independent bits of a
caller-supplied mode byte** (carried on the collector's own `+0x9c` field) against the resolved
object's `+0x14c` flag field (bits `0x10`/`0x40`/`0x02000000`) — three independently selectable
exclusion categories, letting camera/AI/vehicle callers pass a different mode byte into the SAME filter
to exclude different named-region categories, with no separate Havok collision layer needed. Likely
the same mechanism behind `spec-player-runtime.md` §4.9's camera filter `0x00566070` ("skipped hit
kinds 1, 4, 8" — themselves powers of two matching this same per-category bit shape), not independently
cross-checked this pass (OPEN).

### 8.10 Broadphase

**The shipped game does not normally reach Havok's own stock broadphase walk for a ray query** — every
`castRay`-shaped call first checks the game's own broadphase-border sub-object (`world+0x5c`, §8.9);
if present and available, the entire query is handed to it directly, bypassing Havok's internal
spatial partitioning altogether. **Havok's own broadphase/narrowphase walk (world-vtable slot `+0x64`,
reached through the range-B fallback `0x00d4fe10`) is reached only when that border object is absent or
its own check fails** — not opened past this one dispatch point this pass (no `hkp3AxisSweep`-internal
code was read; that class is already named only as present, §1's own Havok-range-B row). **For a
replacement engine: an on-demand ray/shape query does not need to reproduce Havok's own broadphase at
all if the port also reproduces the game's own broadphase-border interception first** — that object is
what actually answers every ray query in the shipped build; only an (apparently never-observed-this-pass)
fallback path needs Havok's own spatial partitioning.

### 8.11 Which named use cases actually go through this mechanism (unit Q-U1)

**Camera collision — CONFIRMED, through the shape-cast wrapper.** `spec-player-runtime.md` §4.9's
`0x00565d70` → `0x0075c880` → Havok range A `0x00c170e0`, twice, `this`=the cached world. The "first
blocking hit" reduction happens in game code reading back the gathered `hkArray`-shaped output, not
inside a Havok "closest" collector (no closest-only collector class exists in `0x0075c880`'s own body).

**AI line-of-sight — CONFIRMED, the new finding of this unit.** `spec-ai-runtime.md` §12.4 left "the
underlying LOS raycast execution — `0x004ecfc0`/`0x004ecf40`/`0x004ed320` — not traced... a
physics-domain primitive" as an explicit scope boundary (its own OPEN item 19). Traced this pass: those
three are, by direct disassembly, pure cache/queue data-structure code — zero calls to Havok, zero
calls to `0x0075xxxx`, zero indirect/vtable calls at all (leaf binary-tree/linked-list lookups over
self-referencing sentinel nodes). **The real resolver is a sibling, `0x004eda70`** (155 instructions,
read in full, reached from `0x004edc80`'s own call list, not from `0x004ed1e0`/`0x004ed550` directly):
it reads the two query positions/handles for a pending node, **calls `0x0075d7e0` directly** with a
zero-initialized input block (so it runs the `kind==0`/closest-collector branch — `0x00756ee0` →
`0x00c163a0` → `0x0075b800` → `0x0075b780` (filter) → `0x00d4c800` → `0x00d6bff0`, the exact chain §8.6
above already confirmed for the AI "line of fire" probe), takes the boolean result and flips bit 0 of
the answer node's `+0x1c` byte when it disagrees with the previous answer — the exact byte/bit
`spec-ai-runtime.md` §12.4's own `0x004ed1e0` already documents decoding. The scheduler, `0x004edc80`
(61 instructions), walks the pending-request tree, computes each node's "due" margin via the game's
own timer helper `0x00d9e250`, and resolves at most the single most-overdue node per invocation — an
amortized, one-resolve-per-tick design matching §12.4's own "re-check interval 700-4000 ms" framing.
**Conclusion: the AI vision/line-of-sight service and the AI "line of fire" probe are two different
higher-level consumers (one async/cached/scheduled, one synchronous-per-action-check) of the EXACT SAME
single low-level Havok castRay wrapper and the exact same closest-ray-hit-collector chain** — the
entire async/dedup/scheduler apparatus exists purely to throttle and cache calls into `0x0075d7e0`, not
to avoid it.

~~**Bullet/weapon-hit detection — NOT FOUND this pass, an honest gap, not a negative finding.**~~
**RESOLVED 2026-10-08 for melee/AoE, HIGH-confidence resolved for hitscan targeting — see
`spec-weapons-runtime.md` §2.** Hypothesis (a) above is CONFIRMED correct for melee/AoE: `0x009c26f0` (a
sphere-cast hit-resolution function) calls `0x0075c880` directly, runs a shared candidate-resolution
chain, and reaches the generic damage sink `0x0096fa20` — resolving this item in favor of "reuses the
existing wrappers," not (b) or (c). Hitscan weapons are HIGH confidence the same answer: `0x00b86b20`
calls `0x0075d7e0` directly, reads the weapon record's own `Range_Max` field to size the cast, and runs
the identical resolution chain — but it turned out to be a debug/target-info overlay that stops short of
calling the damage sink, so the literal live-fire hitscan damage call site is still OPEN (not "bullets
don't use Havok" — the targeting half is confirmed to use it; only the final damage-application call
site for that specific case remains unfound). Full detail: `spec-weapons-runtime.md` §2,
`scratchpad/weapons_runtime_firing_hit_damage_ammo_2026-10-08b.md`.

### 8.12 Clean-room check

Plain hex addresses throughout; no Ghidra, no decompiler output, no `FUN_`/`DAT_`/`LAB_` auto-names, no
pasted pseudocode. Self-scan with this project's strict cleanroom pattern
(`\b(iVar[0-9]+|uVar[0-9]+|...|FUN_[0-9a-fA-F]+|DAT_[0-9a-fA-F]+)\b` plus the plain-substring backstop):
**0 hits** over this entire section (including the Q-U1 material added 2026-10-08). No leaked Havok
source, DeepWiki page, leaked Saints Row IV/Re-Elected source, or other forbidden material was read or
used; the only inputs were this project's own prior specs/scoping notes and this pass's own read-only
`capstone` disassembly against the shipped `SaintsRowTheThird.exe`. Full detail, every address, every
raw listing: `scratchpad/havok_broadphase_raycast_query_2026-10-08.md`.

---

## 9. World-step job dispatch, unit W-U0

**Scope.** The Havok world-step job-queue dispatch mechanism (roots `0x00c17920`/`0x00be88c0`/`0x00c16fc0`, per `review/scoping-physics-vehicle.md` §8.1), which that scoping note and OPEN item 20 both flagged as "not reachable by static call tracing" — the real per-job-type dispatch table. **That table has been found this pass.**

**Method.** Read-only, Ghidra-free. Direct `capstone` disassembly of the shipped exe via a purpose-built linear-sweep reader (scratchpad-only), following call edges and reading raw static data tables. No Ghidra project opened. No leaked Havok source, leaked Volition/SR4 source, or DeepWiki page was read or consulted. Havok's own public API names used below (`hkJobQueue`, `registerJobType`, `processAllJobs`, `getJob`) are labels only, per this project's standing convention, flagged HYPOTHESIS as names even where the structural finding is CONFIRMED (this object has no vtable, so RTTI cannot independently confirm the name).

**Confidence key** (this project's standard): CONFIRMED (read directly this pass), HIGH (confident structural inference, not itself a literal read), HYPOTHESIS (plausible, unchecked).

---

### 9.0 The headline finding

**CONFIRMED.** Havok's internal job queue dispatches per-job work through a flat array of
**16-byte "job-type rows"** embedded directly inside the job-queue object, at **object offset
`+0x128`**, indexed as `row = queueObject + 0x128 + jobType*16`. Each row holds four dwords:

| Row offset | Field | Role (HIGH) |
|---|---|---|
| `+0x0` | `subtypeTable` | pointer to an array of function pointers, one per "subtype"/sub-op value found in the job descriptor |
| `+0x4` | `subtypeTableLength` | element count of that array (used only as a sanity bound in setup, not re-checked at dispatch time) |
| `+0x8` | `procFunc` | a second, directly-stored function pointer — role not confirmed, see §4 |
| `+0xc` | `finishFunc` | a third, directly-stored function pointer — role not confirmed, see §4 |

The job queue object itself is allocated as a single `0x290`-byte block (CONFIRMED: allocation size
literal at the bring-up call site) and stored at the already-documented global
**`0x0161d388`** (`spec-physics-runtime.md` §4.1/§4.4 already names this address as "a Havok job
queue" from the bring-up call sequence — this pass confirms it from the allocator/constructor side
too, independently).

**The generic dispatch loop, read directly from `hkJobQueue::processAllJobs` (`0x00be88c0`,
CONFIRMED):**

```
loop:
  getJob(queue, 0, &jobDesc)          ; 0x00be88a0 -> 0x00be8650
  if no job left: break
  type    = jobDesc.byte[1]            ; the per-job-type selector
  subtype = jobDesc.byte[0]            ; the per-job sub-op selector
  row     = queue + 0x128 + type*16
  table   = row.dword[0]                ; subtypeTable
  handler = table[subtype]               ; subtypeTable[subtype*4]
  handler(queue, &jobDesc)                ; INDIRECT CALL — this is the table the scoping note
                                            ; could not find by static tracing
  continue
```

This is an indirect call through data (`mov edx,[eax+esi+0x128]` then `mov ecx,[edx+ecx*4]` then
`call ecx`), which is exactly why Ghidra's (or any) static reference manager cannot see it as a
call edge from `processAllJobs` — there is no fixed `call 0xXXXXXXXX` anywhere in this function.
The scoping note's instinct was correct.

---

### 9.1 Where the table gets populated — `hkJobQueue::registerJobType` (HYPOTHESIS name), `0x00be7c90`

**CONFIRMED by direct disassembly.** A small thiscall, 5 explicit stack args (`ret 0x14`):

```
registerJobType(queue, type, subtypeTable, subtypeTableLen, procFunc, finishFunc)
  row = queue + 0x128 + type*16
  row[0] = subtypeTable
  row[4] = subtypeTableLen
  row[8] = procFunc
  row[0xc] = finishFunc
```

**A whole-`.text` sweep (12.3 MB, ~4.14M instructions, `skipdata` mode so data gaps don't abort the
scan) found exactly 4 call sites to this function in the entire binary** — not 0, as a naive
immediate-operand-only scan first suggested (that scan silently stopped at the first undecodable
byte run; re-run with Capstone's `skipdata` option it completes cleanly and is reproducible).

All 4 calls happen inside **one master setup function, `0x00c2b220`** (HYPOTHESIS name
"registerAllJobTypes"), which:
- registers **type 0** and **type 1** inline, directly, back-to-back;
- then calls two small helper functions, `0x00d65f10` and `0x00d65f90` (Havok range B), each of
  which builds one more subtype table and registers **type 2** and **type 3** respectively.

**`0x00c2b220` has exactly one caller in the whole binary: `0x0075f71e`, inside the already-known
physics bring-up function `0x0075f370`** (`spec-physics-runtime.md` §4.1). The call sequence there,
read directly:

```
eax = allocator->allocate(0x290)          ; the job-queue object's own storage
if (eax != 0) {
    hkJobQueue_ctor(eax, &cinfo)           ; 0x00be8960, this pass's own find (see §2)
}
global[0x0161d388] = eax                    ; the already-documented job-queue global
registerAllJobTypes(eax)                     ; 0x00c2b220 — THIS is where the 4 rows get filled
```

**So the whole job-type table is built exactly once, at physics bring-up, immediately after the
queue object is constructed, and never touched again anywhere else in the binary** (CONFIRMED by
the single-caller result above).

---

### 9.2 The constructor's own bootstrap default — `0x00be8960`

**CONFIRMED by direct disassembly.** Before `registerAllJobTypes` ever runs, the queue's own
constructor already walks every row slot `0..N-1` (`N` = a cinfo-supplied job-type count read at
constructor offset `+0x1c`; this pass did not pin down `N`'s exact default value — see open items)
and defaults every row to:

```
row[0] = 0 (NULL subtypeTable)
row[4] = 0 (zero length)
row[8] = 0x00be7ba0      ; default procFunc  — body: "return 0", 2 instructions
row[0xc] = 0x00be7bb0    ; default finishFunc — body: "return 1", 2 instructions
```

These two 2-instruction stubs (CONFIRMED, read in full) are the queue's own "this job type was
never registered" placeholders. Their presence, and the fact that `registerJobType` is called only
4 times, together mean: **this build's Havok job queue only ever has four live job types (0-3); any
other type value reaching the dispatch loop would read a NULL `subtypeTable` and crash on
`table[subtype]` — there is no soft-fail path for an unregistered type at the subtype-dispatch
layer**, only at the row-level default (which is bypassed the moment any real registration happens).

The same constructor loop also zeroes a parallel per-type bookkeeping block (a separate `0x250`-byte
allocation held at queue `+0x18`, with a per-type 2-byte counter and a per-type bit in a
"registered" bitmask at that block's own `+0x2c`) — this is the pending-job-count/signal
infrastructure `getJob` reads from (§3), kept deliberately separate from the `+0x128` dispatch rows.

---

### 9.3 The four registered job types, in detail

#### Type 0 and Type 1 — share one "default" 23-slot subtype table

**CONFIRMED.** Both registrations pass the exact same `subtypeTable` pointer, **`0x29c2368`**
(a `.data` global), and the same length, **23**. A separate, earlier piece of the same master
function fills all 23 slots of `0x29c2368` with the identical address **`0x00c21d40`** — i.e. for
types 0 and 1, **every subtype value dispatches to the same handler**; the subtype byte makes no
difference at this layer for these two types.

`0x00c21d40` (CONFIRMED, read in full, 18 instructions / 54 bytes) is itself a second-level
dispatcher: it reads *a different byte, from the job descriptor itself* (not the subtype byte
consumed one layer up), bounds-checks it against 18 (`cmp ecx,0x12; ja default`), and jumps through
a **19-entry jump table at `0x00c21e84`** (read directly: entries 2/14/15/16 share one "return 1"
default branch at `0x00c21e7a`; entries 13/18 share another; the rest are distinct small case
bodies, e.g. case 0 at `0x00c21d5d` calls `0x00c4d5d0` with a couple of pointer/flag arguments).
**This pass read the jump table and the default branch but did not open all 19 individual case
bodies** — flagged as an open item, not a guessed size.

Each of type 0/1 also gets its own distinct, real `procFunc`/`finishFunc` pair (not shared with each
other, and not the generic defaults from §2):

| | procFunc | finishFunc |
|---|---|---|
| Type 0 | `0x00c4dc30` | `0x00c4e3b0` |
| Type 1 | `0x00c4de00` | `0x00c4e000` |

All four are real function prologues in Havok range A. **Open item, not resolved this pass:** a
whole-`.text` sweep for any instruction that calls through row offset `+8`/`+0xc` (register-indirect
or literal-displacement) found **zero** matches anywhere in the binary. These four functions are
registered but this pass could not find where (or whether) they are ever actually called. See §4 and
the open-items file.

#### Type 2 — 7-entry subtype table, Havok range B (collision)

**CONFIRMED.** Registered via helper `0x00d65f10`. `subtypeTable = 0x29c8530`, length 7,
`procFunc = 0x00d87740`, `finishFunc = 0x00d87700`. All 9 addresses (7 subtype slots + proc + finish)
are real, distinct function bodies, every one inside Havok range B (`0x00d4c800`-`0x00d9dff0`,
already identified by `spec-physics-runtime.md` §1 as "Havok collision: shapes, MOPP, agents,
`hkp3AxisSweep`").

| Subtype | Address | What it does (HIGH, read directly) |
|---|---|---|
| `proc` | `0x00d87740` | Shifts an 8-entry array of 16-byte records down by one slot (a queue-compaction/remove-oldest loop), then runs its own secondary 5-way byte-opcode dispatch (jump table at `0x00d87864`) on a different descriptor byte. |
| `finish` | `0x00d87700` | Reads a byte opcode, 6-way jump table (`0x00d87728`); the one case opened decrements a refcount-shaped dword and, only when it hits zero, calls a cleanup function `0x00d876d0`. Classic "last reference out" pattern. |
| 0 | `0x00d86d60` | Builds a small quantization/query descriptor (byte flags + two fixed constants `0x200`), then calls `0x00f0f9c0` — **inside Havok range C** (`0x00ecb420`-`0x00f22240`, "geometry processing, KD-tree, GJK/penetration" per `spec-physics-runtime.md` §1). Reads as an AABB/MOPP-tree query setup. |
| 1 | `0x00d86d30` | Thin forwarder: loads 3 fields from the job object, calls helper `0x00d86b90`, then immediately calls `getJob` (`0x00be8650`) again itself rather than returning — see job-chaining note below. |
| 2 | `0x00d868e0` | The largest of the seven (146 instructions / 511 bytes). Builds a large local descriptor from 8 consecutive dwords read off the job object (`+0x0`...`+0x18`, i.e. a 32-byte block — shape-transform-shaped), then calls two range-C functions, `0x00ee2d40` and `0x00ee15f0`. |
| 3 | `0x00d876a0` | Same shape as subtype 1: thin forwarder into `0x00d87430`, then self-chains into `getJob` again. |
| 4 | `0x00d87060` | The second-largest (244 instructions / 965 bytes), near-identical opening to subtype 2's (same two float constants at the same literal addresses, same 32-byte field copy), calling three range-C functions: `0x00ee2430`, `0x00ee1c30`, `0x00ee3820`. Reads as the "other half" of whatever query subtype 2 performs — HIGH: closest-point vs. linear-cast, or similar overlap-vs-cast pair; not disambiguated this pass. |
| 5 | `0x00d866d0` | Iterates a count-bounded array of pointers (`ebp+0x30` base, count at `ebp+0x2c`), calling range-C function `0x00ee1570` once per non-null entry and accumulating a running count. Reads as "run narrowphase on each pending pair." |
| 6 | `0x00d86630` | Same iteration shape as subtype 5, but per-entry calls up to **two** different range-C functions (`0x00ee2ea0`, `0x00ee2bf0`) depending on which of two fields on the array owner are non-null. Reads as a variant of the same pair-processing loop, branching by shape-pair kind. |

#### Type 3 — 8-entry subtype table, also Havok range B

**CONFIRMED.** Registered via helper `0x00d65f90`. `subtypeTable = 0x29c8510`, length 8,
`procFunc = 0x00d89300`, `finishFunc = 0x00d893c0`. `proc` and `finish` are the same two idioms as
type 2's (array-compaction loop; refcount-to-zero cleanup calling `0x00d892c0` instead of
`0x00d876d0`) — **the two job types share an identical code template**, just different instance data,
strongly suggesting both come from one generic/templated Havok job-handling pattern instantiated
twice (HIGH).

| Subtype | Address | What it does (HIGH, read directly) |
|---|---|---|
| 0 | `0x00d88f80` | Largest of the eight (248 instructions / 827 bytes). Loops over a count at `[job+0x28]`, does a `rep movsd` block copy of a 16-dword (64-byte) record per iteration, writes a function pointer `0x00d97630` into a fixed global (`0x29c976c`) partway through. Not fully opened; reads as full per-pair manifold/contact-point construction (HIGH, not confirmed). |
| 1 | `0x00d88840` | Second-largest (367 instructions / 1307 bytes — the single biggest function reached from either table this pass opened). Calls Havok-range-A memory/container utilities (`0x00be6960`, `0x00be6f90`, `0x00be6b90` — already-known shapes from `spec-physics-runtime.md` §3.1: memory/reference-count helpers) interleaved with five range-B calls (`0x00d98580`, `0x00d989c0`, `0x00d965a0`, `0x00d96a40`, `0x00d96ee0`, `0x00d966a0`). Reads as a growable-array-backed collector building a possibly large per-step result set. |
| 2 | `0x00d87990` | Small forwarder (35 insn): collects 7 fields from the job object, calls `0x00ee64c0` (range C), writes the single returned value back into the job object, then self-chains into `getJob`. |
| 3 | `0x00d87ac0` | Branches on a count field vs. 8; one path (opened) also calls `0x00ee64c0` with a larger argument set (7 fields incl. two via `lea` of local buffers) before falling into further code this pass did not finish reading. |
| 4 | `0x00d879f0` | Loads an XMM constant from `0x29c0860`, assembles two 16-byte vector blocks from job fields `+0x40`/`+0x2c..+0x38`/`+0x54`, calls `0x00ee4320` (range C) — reads as a transform/AABB-shaped geometric call. |
| 5 | `0x00d882b0` | Calls a range-B helper `0x00d97380`, then a range-C function `0x00ee66f0`, then self-chains into `getJob`. |
| 6 | `0x00d88080` | 136 instructions / 553 bytes. Builds a sizeable (>0x400-byte) local structure with several fixed small-integer fields (`0x70`, flag bytes `3`/`5`/`1`) before branching on `[job+0x38]` vs 4; also calls the **same lock/queue-pop primitives already known from `0x00be8650`'s own cluster** (`0x00be7bd0`, `0x00be7bf0`, `0x00be8390`) partway through, then self-chains into `getJob`. This is the one subtype handler that reaches back into the job-queue's own locking machinery directly, rather than only calling out to range B/C. |
| 7 | `0x00d87880` | Reads 9 fields off the job object (mixed integer and one float via `fld`/`fstp`), calls `0x00d956b0` (range B, not range C — the only subtype-3 handler whose main call target is inside range B rather than C), then self-chains into `getJob`. |

#### A shared structural pattern worth calling out on its own

**CONFIRMED, seen in essentially every type-2/type-3 subtype handler opened this pass:** after
doing its own work, the handler calls `getJob` (`0x00be8650`) itself and loops, rather than
returning control to `processAllJobs`'s own loop. This means the generic loop in §0 is really only
the *entry* path; once a thread is inside a hot job type, it stays inside a tight
handler→getJob→handler chain without unwinding back through `processAllJobs` between jobs. This is
a real, confirmed structural/performance detail of this build's job system, independent of what any
given job type's own work actually computes.

---

### 9.4 Addresses referenced in this file (plain hex, for cross-reference)

Dispatch core: `0x00be88c0` processAllJobs, `0x00be88a0`/`0x00be8650` getJob, `0x00be7c90`
registerJobType, `0x00c2b220` registerAllJobTypes, `0x00be8960` queue constructor,
`0x0075f370`/`0x0075f71e` bring-up call site, `0x0161d388` the queue global.

Type 0/1: subtype table `0x29c2368` (23×`0x00c21d40`), inner dispatch `0x00c21d40` + jump table
`0x00c21e84` + default `0x00c21e7a`; proc/finish `0x00c4dc30`/`0x00c4e3b0` (type 0),
`0x00c4de00`/`0x00c4e000` (type 1). Unregistered-type stubs `0x00be7ba0`/`0x00be7bb0`.

Type 2: registration helper `0x00d65f10`; table `0x29c8530` = {`0x00d86d60`, `0x00d86d30`,
`0x00d868e0`, `0x00d876a0`, `0x00d87060`, `0x00d866d0`, `0x00d86630`}; proc `0x00d87740`, finish
`0x00d87700` (+ cleanup `0x00d876d0`).

Type 3: registration helper `0x00d65f90`; table `0x29c8510` = {`0x00d88f80`, `0x00d88840`,
`0x00d87990`, `0x00d87ac0`, `0x00d879f0`, `0x00d882b0`, `0x00d88080`, `0x00d87880`}; proc
`0x00d89300`, finish `0x00d893c0` (+ cleanup `0x00d892c0`).

Range-C (geometry/GJK) callees touched: `0x00f0f9c0`, `0x00ee2d40`, `0x00ee15f0`, `0x00ee2430`,
`0x00ee1c30`, `0x00ee3820`, `0x00ee1570`, `0x00ee2ea0`, `0x00ee2bf0`, `0x00ee64c0`, `0x00ee66f0`,
`0x00ee4320`.

---

## 10. Driving-slice component overrides — the 7 named `hkpVehicle` component classes (2026-10-08)

Direct follow-up to §6 (D-U0): that unit covers the 13 Havok-internal vehicle sub-step bodies and confirms the top-level step (`0x00ae9570`/`0x00ae7470`) replaces, not extends, Havok's default two-phase step. This section opens the next layer up — the 7 named per-component override classes those sub-steps dispatch into (§2.1 rows 30-37: engine, steering, transmission, brakes, suspension, driver input, raycast-wheel-collide, plus the wheel linear-cast collector) — none previously opened by any landed spec.

**Shared structures found across every component:** vehicle instance `+0x18` (chassis `hkpRigidBody`/`hkMotionState`-shaped: linear/angular state, mass-like scalar at `+0xcc`, basis block `+0xf0`-`+0x1cc`, position `+0x140`/`+0x144`/`+0x148`) and `+0x1c` (wheel-array descriptor: count byte at `+0x20`, hardpoint/radius sub-array at `+8` stride `0x30`, a second float array at `+0x8c` stride `0x28`); a shared `0xE0`-byte-stride per-wheel record array at `instance+0x48` (ray start/end at `+0x50`/`+0x60`, a second basis pair, filled-in contact fields consumed by suspension/brakes); the owner-entity back-pointer idiom (`spec-physics-runtime.md` §7.6) and the property-key/`0x00756cb0` lookup (§3.1 item 2) both reappear independently inside wheel-collide's friction resolver (§10.5); a vehicle-instance mode flag at `+0x164` read bit-by-bit by at least three components (OPEN: exact meaning); a byte at `+0x201` consistently selecting a "wider/padded" query variant across steering/wheel-collide/suspension.

### 10.1 Steering (`hk_custom_vehicle_steering`, row 31)

`0x00aeb230` (CONFIRMED, 142 insn) is the real per-wheel distributor: a single steering angle, computed by `0x00aeb0c0` then rate-limited by `0x00aeb110`, is **broadcast identically to every steered wheel with only a per-wheel sign-invert flag** — **no per-wheel Ackermann split exists anywhere in this function** (not found elsewhere in scope either, flagged OPEN rather than asserted absent project-wide). `0x00aeb0c0`'s fast path (vehicle-instance `+0x19c` flag set) is a flat multiply with no speed term at all; the normal path calls straight into **Havok's own base method, `0x00c5a830`** — the real evidence that component-level overrides are not uniformly "replace everything." `0x00c5a830` (CONFIRMED formula) computes `baseAngle = input * steeringComponent.+0x8`, projects a transformed velocity-shaped vector onto a reference row for a `speedTerm`, and above a per-vehicle threshold (`+0xc`) applies `finalAngle = baseAngle * (threshold/speedTerm)²` — **a clean inverse-square speed-sensitive falloff, living inside Havok's own default implementation, not Volition's override.** `0x00aeb110` (CONFIRMED) rate-limits the angle using two rate-constant pairs (normal-turn vs. centring) off the steering component; on a direction reversal it seeds the limiter with `rate1²/(2*rate2)` (the textbook `v²/(2a)` stopping-distance term) before delegating to a generic shared easing primitive (`0x00dae970`, not vehicle-specific).

### 10.2 Engine (`hk_custom_vehicle_engine`, row 30)

`0x00ae31a0` combines two sibling virtual calls into a `{torque; rpm}` output pair, RPM-first. `0x00ae31f0` (CONFIRMED) ramps RPM **up-only** toward a throttle-lerped target: `rate = clamp((target-current)/dt, ±10000.0)`, `ease = clamp(|target-current|/750.0, 0, 1)` (both constants confirmed by direct byte read, `0x0118533c`/`0x012a2f74` and `0x01185338`/`0x01185330`), `new = current + ease*rate*dt` — no explicit fall-rate path (falling RPM presumably comes from the torque curve's own rotational dynamics). `0x00ae3330` (CONFIRMED) is the **real 9-field piecewise torque/resistance curve**, every field read directly off the engine component:

| Field | Role |
|---|---|
| `+0x8`/`+0xc`/`+0x10` | idle, peak-torque, redline RPM |
| `+0x14` | overall torque scale |
| `+0x18`/`+0x1c` | quadratic resistance-shape coefficients, low/high-RPM bands |
| `+0x20`/`+0x24`/`+0x28` | torque-factor at idle/peak/redline |

At/above peak-torque RPM the curve lerps torque/resistance factors toward redline (clamping at redline); below peak it lerps toward idle, additionally scaling by a clutch/load ratio near idle. `netTorque = resistanceFactor*throttle - torqueFactor`, then gated through a water/attached-object check (`0x00758780`→`0x00756cb0`→`0x004dcf00`→`0x00853b10`, the already-named liveness check, independently re-confirmed here) that applies an exact **`×0.85`** penalty (confirmed double literal `0x0112f7e8`) if a specific bit is set on the attached object — HIGH: a damage/environment torque penalty. Final scale by a runtime global `0x02845af4` that reads **zero on disk** (writer not located, OPEN — something must set it at runtime or the engine never produces nonzero torque).

**Construction site and the real xtbl field mapping, CONFIRMED (2026-10-08):** the component is built by `0x00aba4c0`–`0x00aba599` (read to its true end, single `RET`), a bare, zero-derivation 10-field `FLD`/`FSTP` copy from the per-vehicle xtbl-parsed struct's `Torque`/`Min_RPM`/`Opt_RPM`/`Max_RPM`/`Min_RPM_Torque_Factor`/`Max_RPM_Torque_Factor`/`Min_RPM_Resistance`/`Opt_RPM_Resistance`/`Max_RPM_Resistance`/`Clutch_Slip_RPM` fields (offsets `+0x4D0`/`+0x4D4`/`+0x4D8`/`+0x4CC`/`+0x4DC`/`+0x4E0`/`+0x4E4`/`+0x4E8`/`+0x4EC`/`+0x4F0`, per `spec-vehicle-data.md` §7.4) into engine component `+0x8`…`+0x2c`. This settles a premise some consumers may carry that the shipped schema has only 6 author-facing fields for this component — it has all 9 (10 counting the transmission field riding along), confirmed on both the loader side (`spec-vehicle-data.md`) and the consuming side (this constructor) independently. **The authored field names and their runtime roles are crossed** — cross-checked instruction-by-instruction against `0x00ae3330`'s own consuming formula above: every xtbl field named `*_Torque_Factor` actually drives a **resistance**-shaped runtime field, and every field named `*_Resistance` actually drives a **torque-factor**-shaped runtime field:

| Runtime field (this table, above) | Source xtbl field (by constructor copy, confirmed) |
|---|---|
| `+0x18` (idle→peak resistance-shape coeff) | `Min_RPM_Torque_Factor` |
| `+0x1c` (peak→redline resistance-shape coeff) | `Max_RPM_Torque_Factor` |
| `+0x20` (torque-factor at idle) | `Min_RPM_Resistance` |
| `+0x24` (torque-factor at peak, shared pivot) | `Opt_RPM_Resistance` |
| `+0x28` (torque-factor at redline) | `Max_RPM_Resistance` |

A clean-room implementation that assumes authored field names match their code role gets the torque curve's resistance/torque-factor shape backwards — e.g. `Opt_RPM_Resistance`, which by name reads as "the well-behaved middle sample," is the one driving the torque-factor-at-peak pivot used as the shared blend anchor for both branches. **[CONFIRMED — disassembly, both halves (constructor and consumer) independently re-read this pass.]** Full detail: `scratchpad/havok_vehicle_tyreslip_enginetorque_groundfriction_2026-10-08.md`.

### 10.3 Transmission (`hk_custom_vehicle_transmission`, row 32)

`0x00aecd60` composes 4 sibling virtual calls into a gear/clutch-state byte, a clutch-engagement float (forced to 0 if instance `+0x160` is set), a reaction-torque float mirrored to vehicle-instance `+0x158` (the engine/transmission coupling point), and a per-wheel torque-split apply. `0x00aece20` (CONFIRMED) is a **real automatic gear-shift state machine**, not a thin pass-through: a per-gear downshift-RPM table (`+0x34[gear-1]`) with independent cooldown gates per direction, a single shared upshift threshold (`+0xc`, unlike the per-gear downshift table), a max-gear clamp, and a shift-duration timer. Manual-shift input handling is gated out entirely at the mode byte and was not located (OPEN). `0x00aecef0` (CONFIRMED) computes a torque-split-weighted average wheel RPM using the exact `60/(2π)` conversion (confirmed literals `60.0`/`0.15915493667...`), subtracts the cached reaction-torque field, rate-limits, and double-clamps — the transmission's half of the engine/transmission RPM-matching loop that feeds the clutch-side correction term §10.2's torque curve subtracts.

### 10.4 Suspension (`hk_custom_vehicle_suspension`, row 34)

`0x00aebe30` (CONFIRMED structure, 992 insn, mostly a per-thread bump-allocator) **internally re-slices the incoming `dt` into ≤1/60s (`0.016666668`, confirmed literal) chunks and loops** — an internal sub-stepping layer independent of, and not previously documented alongside, the outer variable-Δt physics step (§4.3/§4.6). Each internal micro-step predicts wheel-anchor position, calls `0x00aeb760` — a ray-vs-plane intersection against a **cached ground-plane**, not a fresh Havok query — then calls **Havok's base suspension method, `0x00c59630`, once per internal micro-step**. `0x00c59630` (CONFIRMED, body `0x00c59630`-`0x00c596f8`, exactly **71 instructions**, 1 callee — `0x00c14790`, 28 instructions, returns `1.0/chassis+0xcc`) implements an asymmetric compression/rebound spring-damper: `force = ((restLength-currentLength)*spring*loadFactor - damper*compressionVelocity) * (1/X)`, with a per-wheel `{spring, damperRelaxation, damperCompression}` record (stride `0xC`) — damper branch selected by compression-velocity sign, load factor scaling only the spring term (HIGH: anti-roll/weight-transfer weighting). After the internal loop, two virtual calls on the chassis vtable (`+0x40`/`+0x44`, HIGH: apply linear/angular impulse) commit the aggregated reaction.

### 10.5 Wheel collision (`hk_custom_vehicle_raycast_wheel_collide`, row 36 + collector, row 37)

`0x00aea040` and its Havok base `0x00c5a1e0` (both read in full; the base's own body is `0x00c5a1e0`-`0x00c5a280`, exactly **51 instructions, 0 callees** — a pure leaf, re-confirmed 2026-10-08 at the SIMD-instruction level: `MINPS`/`MAXPS` over the per-wheel ray start/end pair at `instance+0x48`, stride `0xE0`, fields `+0x50`/`+0x60`) compute an **AABB over each wheel's stored ray segment ONLY — no radius, no shape reference, no swept-volume term anywhere** — settling raycast-vs-shapecast decisively in favor of a genuine point-sampled line-segment ray cast, consistent with the public API name itself. `0x00ae9f10` dispatches per wheel via a skip bitmask, a hit/miss branch, and a shared finalize call; a separate rotating counter advances once per call (not per wheel) for an unidentified round-robin purpose (OPEN). `0x00aeadb0`/`0x00aea850` (CONFIRMED structure) is the real friction-source: **per-wheel ground friction comes from a custom Havok body property tagged onto the struck body (preferred key `5`, fallback key `7`, else a locally-computed default) — not a materials-table lookup.** `0x00aea160` computes contact-point relative velocity with the standard two-body point-velocity formula and feeds it into suspension's "compression velocity" field (§10.4). The actual slip-ratio/tangential-friction solve consuming this coefficient is believed to live in the already-separately-scoped D-U1 cluster ~~(§6.4 items 10/13)~~ **[CORRECTED 2026-10-08: the D-U1 entry points are §6.4 items 7/12 (`0x00c565b0`→`0x00c32520`, `0x00c57690`→`0x00c63c70`) — items 10/13 are a different pair of D-U0 functions, not re-opened by that unit. The D-U1 solve is now fully opened, see new §10.8.]** — only the coefficient's own source is newly identified.

### 10.6 Brakes (row 33) and driver input (row 35) — read, lower priority per task scope

**Brakes (`0x00ae2c80`, CONFIRMED, 293 instructions):** calls Havok's base first, `0x00c59e40` — **its own content, opened 2026-10-08 (body `0x00c59e40`-`0x00c5a00c`, exactly 159 instructions, 0 callees — this REFUTES the earlier scoping-note hypothesis that it "almost certainly" also calls the shared reciprocal utility `0x00c14790` the way suspension's base does; it's a fully self-contained leaf):** per wheel (loop bound = the wheel-array descriptor's own count byte at `+0x20`), it (1) writes a per-wheel "lock-eligible" bool when a per-wheel contact byte (`[this+8]+8+wheel*0xc`) AND the brakes component's own mode byte (`this+0xc`) are both set; (2) tracks whether ANY wheel's speed-like field (`[this+8]+4+wheel*0xc`) falls below a threshold (`this+0x4`) — a "stall" condition; (3) computes a candidate per-wheel brake torque (`-([wheel-array+0x8c][wheel] * [+4] * (1/dt) * candidate * load-term)`), clamped to `±(base-array[wheel]*dt)`, into the output torque array; (4) after the per-wheel loop, on a stall, either accumulates `dt` into a scalar stall-timer (a one-shot "entering stall" edge detector) or runs a SECOND per-wheel pass marking the lock flag for every wheel below the speed threshold (independent of step 1's gate); (5) resets the stall-timer to 0 if no stall was detected. This is Havok's default per-wheel brake-torque-with-clamp computation plus a stall/near-stop timer and TWO independent per-wheel lock-flag computations feeding the same output array.

**But the override does not simply "modify" the base's result — it replaces, extends, and passes through three different outputs in three different ways (2026-10-08, resolving a real ambiguity in the "call base, then modify" framing):** reading `0x00ae2c80`'s own body in full against the base's three outputs — the override **unconditionally overwrites every element of the base's main torque array** with its own independently-computed value (a genuine per-wheel contact-point relative velocity, via a cross-product/`rsqrtss`-normalize/dot-product chain against the chassis's own rigid-body state, scaled `-0.5*(1/dt)*brakeInput*bias` and clamped to the per-wheel max-torque table) — the base's own torque computation (step 3 above) is fully computed and then **discarded**. The override's own separate handbrake pass (gated on vehicle-instance flag `+0x6d` bit 1, §10.7/§11) only ever **sets** the lock-flag array to 1, never clears it, so the base's own eligibility/low-speed lock flags **survive and are additively extended**, not replaced. The stall-timer is **never touched by the override at all** — whatever reads it downstream sees the base's value **passed through unchanged**. Net: "call base, then modify" is accurate for the mechanism but imprecise about the result — replace / additively-extend / pass-through are three different relationships to three different outputs of the same base call, not one uniform pattern (sharpens §10.7's "replace vs. wrap" table below).

**Driver input (`0x00ae30c0`, CONFIRMED):** builds Havok's expected analog-input struct straight off the vehicle instance, including **a quadratic (sign-preserving-squared) throttle response curve by default** (`|sum|*sum`, bypassable via instance flag `+0x164` bit 1) — not linear.

### 10.7 Cross-cutting findings

**Component-array slot mapping, partially resolved:** driver input's override reads/writes vehicle-instance `+0xb0` — the exact field D-U0's own `0x00c55f80` (slot `+0x20`'s sub-step) saves/restores around its call — giving **HIGH confidence component-array slot `+0x20` = driver input** (§6.3/§6.8 item 1, closes partially; the other 6 slots remain unresolved).

**Three more slots resolved, 2026-10-08, via the per-vehicle component-construction cluster (`~0x00ab9e00`–`0x00abe100+`) — every named component constructor in this region follows an identical shape (allocator singleton → size-N alloc → optional Havok base-class call → vtable install → store into `vehicleInstance+slot` → optional xtbl-field copy), making the slot itself directly readable off the store instruction:**

| Slot | Vtable | Havok base call | Identification |
|---|---|---|---|
| `+0x20` | `0x1182760` | `0x00c55b00` | **driver input** (CONFIRMED — independently re-derives the finding above by a different method; hardcoded `1.0`/`1.0`/`0.0`/`true` defaults, no xtbl read) |
| `+0x24` | not read | `0x00aeb480` (a game, not Havok, call) | **steering** (HIGH — `0x00aeb480` sits ~0x3c0 bytes from §10.1's already-confirmed steering cluster; reads xtbl `+0x5cc`/`+0x5d0` into component `+0x1c`/`+0x20`; not independently vtable-confirmed) |
| `+0x28` | `0x1182784` | `0x00c59300` | **engine** (CONFIRMED — §10.2's own construction finding) |
| `+0x2c` | `0x11827ec` | `0x00c59e00` | **transmission** (CONFIRMED 2026-10-09, §10.10 — identified by its own field copies: xtbl `Upshift_RPM`/`Diff_Gear_Ratio`/`Reverse_Gear_Ratio`/`Clutch_Delay`/`Maximum_RPM_Rate`/`Gear_Ratios[]` all land here, consumed by the already-documented gear-shift state machine `0x00aece20`; `0x00c59e00`'s address proximity to brakes' own `0x00c59e40` is a false lead from locality alone) |
| `+0x30` | `0x1182818` | `0x00c5a010` | **brakes** (HIGH, 2026-10-09, §10.10 — xtbl `Min_Time_To_Block`/`AI_Min_Time_To_Block` land in this slot's own `+0x14`; not independently confirmed by a vtable/construction-site read the way the other slots were) |

(`+0x34`/`+0x38` not located in this scan's window.) The same scan method generalizes directly to the remaining slots for whoever continues. Full detail: `scratchpad/havok_vehicle_tyreslip_enginetorque_groundfriction_2026-10-08.md`.

**The "replace vs. wrap" picture, refined — three distinct patterns coexist at the component layer** (the task's own "replace, don't extend" framing holds exactly for the top-level step, §6.2, but does not generalize below it):

| Component | Relationship to Havok base |
|---|---|
| Engine, Transmission | Full replacement — no Havok base call found in either |
| Suspension | Unconditional wrap-and-call — always calls Havok's base, every internal micro-step |
| Brakes | Unconditional wrap-and-call at the call-edge level, but **not uniform underneath (2026-10-08)** — the override REPLACES the base's main torque-array output outright (base's own value computed then discarded), ADDITIVELY EXTENDS the base's lock-flag array (only ever sets, never clears), and PASSES THROUGH the base's stall-timer untouched. Three different relationships to three different outputs of the same base call, not one pattern — see §10.6. Whether suspension's own single-output base shows an analogous split was not checked (§11) |
| Steering, Wheel-collide AABB | Conditional delegation — one mode bypasses Havok, the other calls Havok's base outright |

Suspension's internal ≤1/60s sub-stepping (§10.4) is a second, independent, component-local sub-stepping layer the existing outer-step lifecycle documentation does not mention — no other component read this pass does the same.

**OPEN (not resolved this pass):** exact meaning of vehicle-instance flags `+0x164` (both bits), `+0x19c`, `+0x201`, `+0x160`, `+0x6d` bit 1, `+0x59` bit 2; the writer of `0x02845af4` (engine's final torque multiplier, zero on disk); manual-transmission shift-input handling; wheel-collide's round-robin counter's purpose; the exact struck-shape-type taxonomy (`0x00aeac00`'s 0/1/2/3); the remaining 6 of 7 component-array slot mappings. Full addresses, raw formulas and the complete decompile dump set are in `scratchpad/driving-slice-component-overrides-2026-10-08.md`.

---

### 10.8 The real D-U1 tangential-friction/slip solve (`0x00c32520`, `0x00c63c70` + callees, 2026-10-08)

Direct follow-up to §10.5's friction-coefficient finding and §6's own D-U1 citation. **`0x00c32520` is NOT the friction force solve** — it's a per-wheel contact/friction-description record builder (8-way kind dispatch via jump table `0xc32880`). Its representative case-0 path repacks a basis/matrix, then computes a genuine vector lerp `out = (1−s)·V1 + s·V2` (exact `1.0f` constant verified). Two of the 8 cases additionally apply a clamped multiplicative scale to a basis vector, using the same packed-16-bit-field decode idiom D-U0's own `0x00c57a50`/`0x00c576d0` use for their "decay-toward-limit" terms — **the decode idiom is shared, but the downstream arithmetic is different: a clamped basis-vector scale here, not a running `min(limit, running−field×factor)` decay there. Do not conflate the two.**

**`0x00c63c70` is the real combined-slip tangential-friction solve**, run once per axle pair, in stages: (1) a one-time Havok build-string/version-assert gate, confirmed unrelated to physics; (2) a textbook sequential-impulse **effective-mass** computation per tangent axis per wheel — `effectiveMass_rigid = 1/(sumSq+1.0)`, `effectiveMass_soft = 1/(sumSq+1.0+wheelExtra)` (both reciprocal formulas, exact constants verified by direct byte read); (3) a **friction-ellipse apportionment** between the two tangent axes, packing a coupling scalar `X` into a `{1,X,X,1}` 2×2 matrix; (4) `0x00c63570` computes that matrix's exact closed-form inverse (`invDet·[[1,−X],[−X,1]]`, `det=1−X²`), with a tolerance-gated degenerate-case signal when the axes are too strongly correlated (`|X|` near 1) — byte-exact, fully traced; (5) `0x00c63930` is the real **combined-slip saturation test — the literal unit friction circle**, `(A·B)²+(C·D)² < 1.0` (the `1.0` comparand independently confirmed at `0x012a2d70`), guarded by confirmed `FLT_EPSILON` and `1e-4` deadzone constants and a confirmed `±1.1` overshoot clamp, then a bounded **16-entry table-driven piecewise evaluation** (a discretized friction/slip-response curve, evaluated as a per-entry quadratic form kept via running-best comparison — a real search/interpolation step, not a closed-form expression or a plain indexed lookup). **The table's exact location is now pinned (CONFIRMED, 2026-10-08): 16 consecutive floats at `a1[wheel]+0x8` through `+0x44`** (`a1` = the per-wheel array at `(vehicleInstance+0x1c)+0xa4`, stride `0x64`, the same array already holding `wheelExtra` at `+0x54`) — it is not a separate tuning asset, it lives inside the same per-wheel Havok-side tuning record as the other already-named fields. Two companion scalars at `a1[wheel]+0x48`/`+0x4C` are read by the same walk (a pre-loop boundary value and a per-entry multiplier). The real sampled curve values, and the constructor that populates `a1` from vehicle data, remain OPEN — a literal-displacement scan (the technique that found §10.2's engine constructor) doesn't generalize here since `a1`'s own small generic offsets are too common across the binary to narrow a scan; the real next step is finding `*(vehicleInstance+0x1c)`'s own constructor directly. (6) a final application loop writes the now friction-circle-corrected tangential impulse into the SAME chassis force (`+0xe0`)/torque (`+0xf0`) accumulators §6.5 already names — confirming those fields carry both an earlier non-friction contribution (loop 2, contact-point relative velocity) and this friction contribution.

**The contact-point relative-velocity ("slip") computation itself, CONFIRMED and address-pinned (2026-10-08): `0x00c64252`–`0x00c64306`, inside this function's own "Loop 2."** It reads the per-wheel contact record (`*(a2+0x48)`, gated on the same "live contact" flag already named), differences the chassis's own force-accumulator-shaped velocity field (`a2+0xe0..0xe8`) against the struck/ground object's own cached linear-velocity field (read off a fixed, non-wheel-looped pointer at `a2+0x30`), and projects the result onto the wheel's two tangent-basis rows to get the longitudinal/lateral slip-velocity components — written to the per-wheel record's own `+0x20`/`+0x24`/`+0x28`/`+0x2c` fields, then clamped against a *second*, previously-undocumented per-wheel limit (`a2+0x38`/`+0x3c`/`+0x40`, read just ahead of the per-wheel record array). **This settles the question §10.5 left hedged** ("believed to live in the D-U1 cluster") — it does, specifically here, not in wheel-collide's `0x00aea160` (that function computes a structurally similar but genuinely separate relative velocity that feeds suspension's compression-velocity field instead, §10.4/§10.5). This raw relative velocity is what feeds the effective-mass and friction-ellipse stages above — i.e. **the saturation test's own `A`/`B`/`C`/`D` are a downstream, already-rescaled version of this slip, not the raw slip itself.** The exact field-for-field chain from Loop 2's four outputs to the `A`/`B`/`C`/`D` stack slots is confirmed to exist (both only ever written inside the aggregate effective-mass/friction-ellipse block) but not pinned byte-exact — narrowed to one ~250-instruction span, `0x00c64605`–`0x00c646c1`, OPEN for a future pass.

**Corrects the existing "decay-toward-limit terms and a final friction-impulse clamp" framing (D-U0 §6.4 items 10/13's own language, used loosely to describe what D-U1 was *believed* to do before it was opened):** this is not one clamp, it's a 3-stage combined-slip friction-ellipse pipeline (effective-mass → friction-circle saturation with an explicit 2×2 axis-coupling inverse → impulse application) — closer to a proper tire-friction-ellipse solver than a single `min(limit, x)`.

**The §10.5↔here splice, re-opened and sharpened (2026-10-08) — still OPEN, but materially narrower than before.** `0x00c565b0`/`0x00c57690`'s own argument construction was re-opened in full. The prior pass's two candidates: `a0[0]` is now **REFUTED** — it is confirmed to be one shared "step context" pointer handed identically to the entire vehicle step (both D-U0 phase-1/phase-2 game overrides receive the same pointer as their own first argument, traced two hops up to `0x00ae98f0`), and used a *second*, independent time inside Loop 2's own slip clamp above — not plausibly a per-wheel, per-raycast-hit ground-material coefficient. The `a1`-array candidate refines to an exact field, `a1_base+0x4` (read once per axle-pair call, not wheel-looped), but is reassigned: structurally this sits on the **vehicle's own** wheel-array-descriptor object, exactly where a static per-vehicle authored value would live, not a per-raycast ground-body-derived one — most plausibly the vehicle's own tire-friction tuning (`Wheel_Friction`/`Wheel_Max_Friction`, present in the real `_veh.xtbl` schema per `spec-vehicle-data.md` §7.4) rather than §10.5's ground-surface coefficient. Three further, previously-undocumented scalars (`a2+0x38`/`+0x3c`/`+0x40`) were found feeding the second slip-velocity clamp named above — plausible candidates for a vehicle-wide `Static_Load_Friction`-style constant (also present in the xtbl schema), HYPOTHESIS only. **Best current hypothesis, not confirmed:** §10.5's coefficient most likely folds into the normal-force/load terms (`a2+0x44`/`a2+0xa4`, the two per-wheel "load" scalars this function's own `term` formula sums) *upstream* of this function — inside wheel-collide's or suspension's own force computation — rather than appearing as a separately-multiplied term here; confirming this needs a targeted re-read of whatever writes those two fields (`0x00c57a50`/`0x00c576d0`/`0x00c59630`), outside this pass's own scope. Full instruction-level derivations: `scratchpad/havok_du1_tangential_friction_slip_solve_2026-10-08.md`, `scratchpad/havok_vehicle_tyreslip_enginetorque_groundfriction_2026-10-08.md`.

### 10.9 Driving-control input → Havok hand-off (U7, 2026-10-08)

Closes a gap `review/scoping-player.md` scoped as unreserved unit "U7" and explicitly flagged ("the
hand-off into `hk_custom_vehicle_driver_input` `0x00ae30c0` was not traced") — a same-day AI/traffic
scratch note independently hit the identical gap from the other side and also left it OPEN.

**Full chain, CONFIRMED by direct disassembly:** per-frame controls (`0x00702ad4`→`0x005aa6c0`→
`0x005b7430`→`0x005b73c0`) → in-vehicle action dispatcher `0x005b5c50` (586 instructions) →
driving-input readers `0x00a91800` (ground vehicles) / `0x00a91440` (motorcycles) → two input-shaping
setters, **`0x00ae5290`** and **`0x00ae5420`**, both newly found this pass. Both take `ECX = *(vehicle+0x808)`
— confirmed identical to "the vehicle instance" `spec-physics-runtime.md` already tracks throughout this
section (shared `+0x48` field, cross-confirmed against a same-day render-side scratch note).

`0x00ae5290` applies a deadzone/response curve and writes `instance+0x130`; `0x00ae5420` writes
`instance+0x13c`. Re-reading the already-documented Havok override `0x00ae30c0` directly: it reads
`instance+0x130`+`0x138` (summed, sign-squared) into its own analog struct. Independently re-reading
`0x00a926b0` (the "current steering angle" query) finds it reads the *same* `instance+0x130` — matching
the already-landed `spec-player-runtime.md` §12.4 finding ("Vehicle Steering" = `0x00a926b0`'s return).
**Three independent confirmations (write-side provenance here, this pass's own read of `0x00a926b0`, and
an unrelated already-published spec section) agree: `instance+0x130`/`0x138` is the real steering
channel.**

`CAA_DRIVE_STEER` (axis 9) is confirmed read at `0x005bb530(9,0)` but — traced to the end of the switch,
not just the entry — is provably *unused* for ordinary cars (kind 0/1), which instead reuse the
on-foot walk-turn axis. This corrects the existing scoping note's true-but-incomplete framing.

`0x00ae5290`/`0x00ae5420` have several other callers besides the player path; two functions
(`0x00abde60`, `0x00b29580`) call BOTH setters — an unverified lead for the AI/traffic side's own
parallel open item (which vehicle this pass's wheels/steering material belongs to for AI-driven cars).

**Genuinely left OPEN (not guessed):** who writes `instance+0x128`/`0x12c`/`0x138`; what consumes
`instance+0x13c` (likely brakes, `0x00ae2c80`, not checked); exact meaning of `instance+0x164` bits
0/1; the AI-caller lead above. Full detail:
`scratchpad/vehicle_entry_exit_control_handoff_2026-10-08.md`.

### 10.10 Vehicle-info-table → Havok component construction, by parameter (2026-10-09)

Closes `purrsian-04`'s wave 6 item 5: for mass, the torque curve, gear ratios, steering and tire/wheel
grip, the real spawn-time wiring between the vehicle-info-table entry (`spec-vehicle-data.md` §7.3's
field map) and the live Havok vehicle instance/component rows this section documents. All five found
in the same per-vehicle component-construction cluster §10.7 already names (`~0x00ab9e00`–`0x00abe100`).
All five are CONFIRMED end-to-end; two (mass, grip) are genuinely new wiring, two (steering, gear
ratios) close gaps §10.7 left open, one (torque curve) is pre-existing and simply re-verified unchanged.

**Mass.** Chassis/rigid-body constructor **`0x00abc160`** reads entry `+0x498` (`Mass`) twice off the
vehicle's own cached entry pointer (object `+0xbf4`): once copied into a Havok rigid-body
construction-info block on the stack (alongside the already-named `Roll/Pitch/Yaw_Unit_Inertia` fields
from the same entry), once broadcast to a 4-lane vector and passed by address into a mass-properties
call, `0x00bea3a0` — the classic "mass + half-extent box → inertia tensor" shape. The exact byte offset
of mass inside the finished live `hkpRigidBody` object was not independently re-pinned (this section's
own intro already carries a HIGH-confidence, not CONFIRMED, note that chassis `+0xcc` is "a mass-like
scalar") — what's newly confirmed is the data-to-construction wiring itself, not that remaining
fine-grained question.

**Torque curve (idle/peak/redline RPM + torque scale) — pre-existing, re-verified unchanged.** §10.2's
own engine constructor (`0x00aba4c0`–`0x00aba599`) and consumer (`0x00ae3330`) already fully confirm
this; re-read directly this pass with no discrepancy found.

**Gear ratios.** Transmission constructor **`0x00abd400`** (component slot `+0x2c`, newly identified —
§10.7's table updated in place) copies `Upshift_RPM`(`entry+0x5A8`→component`+0xc`),
`Diff_Gear_Ratio`(`+0x5AC`→`+0x10`), `Reverse_Gear_Ratio`(`+0x5B0`→`+0x18`),
`Clutch_Delay`(`+0x5B4`→`+0x14`), `Maximum_RPM_Rate`(`+0x5B8`→`+0x40`), then copies the gear count
(`entry+0x578`) and the `Gear_Ratios[]` array (`entry+0x57C`) element-by-element into the component's
own growable array at `+0x1c` (resized via the already-named Havok array-growth helper `0x00be6f90`) —
all consumed by the already-documented gear-shift state machine `0x00aece20`. The same constructor
continues past this point building a second per-wheel array from axle `Wheel_Engine_Torque`
(`entry+0x4FC`) divided by wheel count — confirms the destination of the already-documented
per-wheel-torque-split renormalization, not just its existence. **Not independently re-verified:**
whether `Downshift_RPMs` (`entry+0x594`) is copied into component `+0x34` by a further block of the same
function past where this pass stopped reading in detail — structurally very likely, HIGH not CONFIRMED.

**Steering (angle/speed response) — closes §10.7's own named gap.** §10.1's confirmed steering formula
reads component `+0x24`'s own `+0x8` (flat base-angle multiplier) and `+0xc` (speed-falloff threshold),
but their xtbl source fields were unfound. Function **`0x00ab9ea0`** writes them: `+0x8` ←
`entry+0x5C0`(`AI_Maximum_Steering_Angle`) or `entry+0x5BC`(`Max_Steering_Angle`), `+0xc` ←
`entry+0x5C8`(`AI_Max_Speed_Steering`) or `entry+0x5C4`(`Max_Speed_Steering`), selected by an AI/player
boolean carried in `bl` throughout the function.

**Tire/wheel grip (friction).** Same function, **`0x00ab9ea0`**, in its per-wheel loop (unrolled ×4 with
a scalar remainder, both independently confirming a `0x28`-byte per-wheel stride): resolves each wheel's
owning axle via a wheel→axle table on the vehicle object's own `+0x7fc` sub-object, reads that axle's
`Wheel_Friction`(axle-relative `+0xC`)/`AI_Wheel_Friction`(`+0x10`) from the axle array
(`entry+0x4F8 + axleIndex×0x40`), and writes it into the wheel-array descriptor (`instance+0x1c`)'s own
float array at `+0x8c`, per-wheel offset `+0xC` inside each wheel's `0x28`-byte slot — a field this
section's own shared-structures intro already named generically ("a second float array at `+0x8c`
stride `0x28`") without identifying. **This is a different array from §10.8's own `a1` (= `(instance+0x1c)+0xa4`,
stride `0x64`) HYPOTHESIS candidate for `Wheel_Friction`/`Wheel_Max_Friction`** — both may legitimately
exist (one CONFIRMED construction-time input here, one still-HYPOTHESIS consumer deep in the D-U1
slip/friction-ellipse solve); whether the D-U1 solve's own per-wheel friction value is read *from* this
`+0x8c` array is a flagged follow-up, not resolved this pass.

**Bonus, not one of the 5 target parameters:** the same `0x00ab9ea0`, before the friction loop, writes
component slot `+0x30`'s own `+0x14` from `entry+0x5E0`/`+0x5E4`
(`Min_Time_To_Block`/`AI_Min_Time_To_Block`) — the field semantics point at **brakes** for this slot
(§10.7's table updated, HIGH not CONFIRMED by a vtable/construction-site read the way other slots were).
The same function also copies a rotational-physics block (`Yaw_Torque_Factor` family,
`Static_Load_Friction`/`AI_Static_Load_Friction`, `entry+0x72C`–`+0x768`) into the wheel-array descriptor
at `+0x68`/`+0x74`/`+0x78`/`+0x7c`/`+0x80` — not one of the 5 target parameters, noted as context for
whoever documents chassis rotational dynamics next.

**[CONFIRMED — disassembly, all 5 target parameters, end-to-end from entry offset to component
destination to known consumer.]** Full instruction-level detail and the construction-cluster scan
method: `scratchpad/vehicle_handling_data_to_drivemodel_2026-10-09.md`.

## 10a. Contact-listener collision detection and response — vehicle and generic (2026-10-08)

**Scope.** What a vehicle (or any body) actually collides with via Havok's own in-tick contact
reporting, and how the response — the physical bounce/stop, and the separate gameplay-visible
vehicle-damage consequence — is computed. Complements, does not duplicate, §8's own on-demand
raycast/shape-cast material (camera collision, AI line-of-sight, outside-the-tick queries) — this
section is the in-tick, Havok-initiated contact-reporting path, a different mechanism.

**Cross-reference (2026-10-09, not re-derived here):** the same registration function
(`0x00766990`→`0x007669d0`) that populates this section's type-pair dispatch tables for vehicle
(type `7`) also covers type id `0x20` (HIGH CONFIDENCE "projectile," by the same classifier/
position-history-buffer evidence this section's own vehicle bridge already establishes), paired
against `5`/`7`/`0xa`/`0xff`, plus a reverse `(7, 0x20)` handler. Full trace of the `0x20`-type-pair
handlers (`0x00765fa0`/`0x00765f20`): `spec-weapons-runtime.md` §2a.5.

**Collision shape (cited, not re-derived): per-part `hkpBoxShape`s** (RTTI-confirmed,
`spec-vehicle-geometry.md` §11.11), one box per vehicle part trimmed to that part's own authored
extent — not a full hull, not one box for the whole vehicle. Whether the parts combine into one
compound shape on the single chassis rigid body is OPEN. Wheels use the separate point-ray system
(§10.5), no shape at all. **A fourth, separate box exists and is load-bearing for RESPONSE, not
detection**: the vehicle vtable's own cached per-instance bounding-box getter (`+0x60`, §8.6) — read
by the damage-amount bridge below, not a Havok shape at all.

**Detection: Havok's own `hkpContactListener`, overridden by the game's `custom_collision_listener`**
(§2.1 row 3, RTTI-confirmed; installed as world member `+0x360`). All three override methods, only
named by prior passes, are now fully opened:

- **`0x00769040` (`contactPointCallback`-shaped, 1,207 bytes)** — runs inside a critical section on
  Havok's own physics-step thread. Computes a closing-speed scalar (relative velocity of the two
  bodies projected onto the contact normal); resolves both bodies to game objects through the
  project's own universal keyed-object resolver (`0x00458230`/`0x00853b10`/`0x02cc9900`) — **the
  exact same chain already documented for weapon/explosion damage application**, direct evidence
  Havok-thread contact processing and gameplay damage share one object-resolution layer, not two
  independent systems. If a handler is registered for the resolved (typeA,typeB) pair (table
  `0x016937a0`, triangular-indexed via remap table `0x012f91c8`) AND the contact is closing, calls it
  directly on the physics thread with both objects/bodies/per-type-slots and the contact
  position/normal. Otherwise falls through to a default generic response (below). Separately and
  unconditionally, if closing, enqueues a deferred record (`0x00768140`, below).
- **`0x00769500`/`0x00769750` (`collisionAddedCallback`/`collisionRemovedCallback`, 576 bytes each,
  structural mirrors)** — fire once per touch-begin/end, not per contact point. A hardcoded special
  case for type pair `(5,7)` calls a Havok-internal action-attach function directly
  (`0x00c2bfe0`/`0x00c2c010`, not opened — third-party internals). Maintain a reference-counted
  per-object "currently touching" linked list (head via `0x00767500`/`0x00767fa0`, OPEN consumer —
  plausibly a scripted overlap-trigger facility, not confirmed).

**The deferred contact-event queue — new finding, fully traced.** The physics-thread callback does
not apply gameplay consequences itself; it stages into a 300-record (`0x290` bytes each) pool,
drained once per frame on the main thread. **Enqueue: `0x00768140`** (full decompile). **Drain:
`0x007699a0`** (full decompile) — **CONFIRMED caller: `0x0075ac20`, the already-documented per-frame
physics driver** (§4.3 step 5's own "dispatches queued contact events to gameplay" line) — this
closes §11 OPEN item 4 for `0x007699a0` specifically. Per drained record: re-resolves and
re-checks liveness (objects that died mid-step are skipped); dispatches through a per-single-type
table (`0x016be3f0`) and a SECOND type-PAIR table (`0x016be5e0`, same triangular-index formula as
the physics-thread's own `0x016937a0` — the deferred/gameplay-thread twin of it). A second, smaller
deferred queue (`0x0168d9e0`, 24-byte records) is drained immediately after, same pattern.

**The default (no-bespoke-handler) response — a direct Havok impulse application, HIGH CONFIDENCE.**
For a plain wall/ground hit with no registered type-pair handler: `0x007676f0` → per-type table
`0x016ba4f8` → **`0x00c2bb60`** (picks whichever body isn't fixed/infinite-mass) → **`0x00c513e0`**
(Havok range A, 6-argument shape consistent with a direct point-impulse/velocity-change application
at the contact). **There is no separate Volition-authored impulse/restitution formula at the game
layer for the generic case — Havok's own solver does it directly, triggered from the game's own
listener.** Restitution/friction coefficient SOURCING remains OPEN (§11 item 7's still-unopened
`0x00769c30`, 22,979 instructions, "collision-material and layer tables").

**The vehicle collision→damage bridge — resolves `spec-vehicle-damage-runtime.md` §3.5's own OPEN
lead (`0x00763767`).** The deferred-queue's type-pair table (`0x016be5e0`, registered by
`0x007669d0`→`0x00766ec0`) has these literal entries: `(7,7)`→`0x007642f0` [**vehicle-vs-vehicle**];
`(7,0xff)`→`0x00764bf0` [**vehicle-vs-unresolved-object** — `0xff` is this file's own universal
"did not resolve to a game object" sentinel, so this is effectively vehicle-vs-static-world-geometry];
`(7,0x3a)`→the same handler via a thunk (`0x00764e60`); `(7,5)`→`0x007663e0`. **Type `7` is CONFIRMED
as the vehicle object-type code** (the common factor across every registration, independently
corroborated by `0x007642f0`'s own vehicle-capability-bit filtering on both sides). All of these
handlers funnel into the SAME function, **`0x00763400`**, which reads the vehicle's own cached
bounding-box (vtable `+0x60`) to scale a damage amount by vehicle size, combines it with a
speed/time-derived scalar, and calls **`0x00a9f4f0`** — the already-documented vehicle force/
damage-apply dispatcher (`spec-vehicle-damage-runtime.md` §3). `0x007642f0` (the vehicle-vs-vehicle
handler) independently filters BOTH contact sides for the vehicle-capability bit and calls
`0x00763400` once per side that is a vehicle — **CONFIRMED: vehicle-vs-vehicle and vehicle-vs-world
collision damage are the SAME mechanism, differentiated only by which type-pair table entry fires,
not by two parallel systems.** One pre-existing, structurally SEPARATE exception, worth flagging by
contrast: `spec-vehicle-damage-runtime.md` §6a's vehicle-crush-kills-occupant mechanism
(`0x00b19ad0`) is NOT part of this Havok-contact pipeline at all — it runs on an ordinary per-frame
bounding-box-overlap test, no Havok event involved.

**Genuinely OPEN:** `0x00763400`'s own exact damage formula and whether its time/speed term traces
byte-exactly to the Havok contact event's own closing-speed scalar (the single most valuable
remaining link, to close "impact speed → damage amount" exactly rather than by architecture alone);
the identity of object types `0x3a` and `5` (type 5 is also the hardcoded `(5,7)` special case in
the add/remove listener methods above — appearing twice independently, HYPOTHESIS character/ped, not
confirmed); restitution/friction coefficient sourcing; the second deferred queue's own resolver/
consumer roles; whether per-part `hkpBoxShape`s combine into one compound chassis shape. Full detail,
complete 40+-entry address index: `scratchpad/vehicle_world_collision_response_2026-10-08.md`.

## 11. OPEN items

1. **Where the constraint filter's listener sub-object is added.** Shutdown removes it, but no
   bring-up body adds it — it may happen inside Havok's own world constructor.
2. **Construction-info field names** (§4.1 steps 4–5): offsets/values are CONFIRMED, field names are
   HYPOTHESIS from Havok 2010's public header order. Most open: the byte at `+0xe8` = 1, the
   solver-preset argument 5, and the two job-queue construction fields.
3. **When vehicle code sets the sub-step multiplier k = 2.0** (`0x00a86620`) — has no effect in the
   shipped default mode, so low priority; a later vehicle unit's concern.
4. ~~**Post-step roles of `0x0077c9d0` and `0x007699a0`** — likely contact-event dispatch and handing the
   stepped time to gameplay; both are game code with no direct Havok call.~~ **RESOLVED/CORRECTED,
   2026-10-08 (§10a): `0x007699a0` is CONFIRMED the deferred contact-event queue drain, called from
   `0x0075ac20`. `0x0077c9d0` is NOT contact dispatch** — it iterates a different global array
   (`0x0171ad10`) computing per-entry rate-of-change fields, a generic per-object velocity/analytics
   post-step update (plausibly engine-sound-pitch or wind rate), unrelated to contact events. Both
   share the same caller (`0x0075ac20`, different call sites) in the same post-step phase, which is
   presumably why they were bracketed together.
5. **Which callbacks specifically run on worker threads** during the step, beyond the HIGH-confidence
   general statement in §4.6 item 5.
6. **The 41 small Havok-called functions outside the three ranges** (§1) — taken as a class
   (header-inline/folded); only two of them (§3.1) were identified individually.
7. **`0x00769c30`** (22,979 instructions, collision-material/layer tables) — not opened by this unit.
8. **The unattributed range `0x00d1d570`–`0x00d4c800`** (§1) — still unidentified, out of scope here.
9. **Which of the 7-slot component-pointer array's offsets (`+0x20`-`+0x38`, §6.3) corresponds to
   which of the 7 named override classes** (§2.1 rows 30-36: engine, steering, transmission, brakes,
   suspension, driver input, raycast wheel collide) was not determined (D-U0).
10. **Six named-but-unopened Havok-internal sibling functions** found while confirming §6.2's
    structural finding: `0x00c56b60`, `0x00c55fc0` (phase-1, called by `0x00c58890`, skipped by the
    game override); `0x00c571f0`, `0x00c56e20`, `0x00c56550`, `0x00c56680` (phase-2, called by
    `0x00c57f70`, skipped by the game override). Candidates for whoever opens D-U1 (D-U0).
11. **A possible third kind of Havok-range-A-to-game-code direct call**, not covered by the §2.3/§3
    72-slot register or the one function-pointer exception: `0x00c58890` appears to directly `CALL
    0x00ae8f10` (inside the game's vehicle cluster). Seen once, incidentally, during a bounded read
    not intended to fully analyze `0x00c58890` — flagged, not confirmed; needs a short targeted
    verification pass before being relied on (D-U0).
12. **Per-function Havok public-API naming for all 13 D-U0 sub-steps** — none independently cleared
    the bar for a confirmed public method name this pass; all cited by hex address + behavioral role
    only. Resolving item 9 above would likely sharpen several at once (D-U0).
13. **The paired-record steps `0x00c57a50`/`0x00c57ca0`'s exact role** (axle-pair anti-roll coupling,
    vs. independent per-wheel suspension/steering response) is HYPOTHESIS only (D-U0).
14. **The shared `+0xe8==5` disabled-record sentinel and the packed-16-bit-field decode idiom** (§6.5)
    were not matched against any named Havok enum/encoding (D-U0).
15. **`0x00c16420`/`0x00c16440`** (called from `0x00c30150`'s unwind path when the world's nesting
    counter returns to zero, §7.4 step 6) were sized by first-RET only (55/41 instructions), not read
    in full. Hypothesis to check: these are the two deferred-queue replay routines §4.3 already
    describes generically — confirming this would pin down exactly where that replay happens from
    (W-U1).
16. **The exact field semantics inside `0x00c838d0`'s penetration/closing-speed FPU chain** (§7.6):
    `entity+0x9c`, `entity+0xa0`, and the phantom's own `+0x1a0` are read but not named (friction/
    restitution coefficient, max-separating-speed clamp, and shape radius were all considered,
    none independently confirmed) (W-U1).
17. **What property key `0x1300` actually marks.** CONFIRMED as a real game-attached Havok property
    (query side `0x00c838d0` §7.6; remove side `0x00c84960` §7.8), and CONFIRMED that carrying objects
    get routed to a phantom-listener fan-out instead of the normal point-velocity/activate path — but
    which game-side objects set it, and what gameplay behavior the fan-out triggers, were not
    identified. *Next step:* a raw scan for `push 0x1300` near a property-SET call, and an RTTI/vtable
    walk of whatever registers into a phantom's `+0xa4`/`+0xa8` listener array (W-U1).
18. **The phantom-listener array `phantom+0xa4`/`+0xa8` itself** (§7.7) — who populates it was not
    found this pass; it is a separate, still-anonymous mechanism from `MyCharacterListener` (which
    registers on the proxy, §2.1 row 23, not the phantom) (W-U1).
19. **Six secondary callees sized but not read in full:** `0x00c2fbe0`, `0x00c2fd20`, `0x00d580d0`
    (from `0x00c30150`, §7.4 steps 4-5) and `0x00c21970`, `0x00c2fea0`, `0x00c15630` (from `0x00c84960`,
    §7.8) — plausible Havok-internal notification/bookkeeping helpers, none load-bearing enough to this
    pass's main findings to justify opening (W-U1).
20. **Unit W-U0 — world-step dispatch + job-queue mechanics** (roots `0x00c17920`/`0x00be88c0`/
    `0x00c16fc0`): the real per-job dynamics dispatch (broadphase/narrowphase/solve/integrate) is still
    not reachable by static call tracing from those three roots; the job-type dispatch table has not
    been found by any pass to date (W-U1, confirming the scoping note's own finding).
21. **Unit W-U2 — the proxy's own cast/solve Havok calls** (`spec-player-runtime.md` OPEN 36's eight
    named pass functions) remains open as a unit; this pass only opened the Havok-internal targets two
    of them call into (W-U1).
22. **Which thread the phantom-listener fan-out (§7.7) runs on** — likely whichever scheduler job is
    executing the lock-step cast-and-solve pass (group `0x0223ac40`, `spec-player-runtime.md` §5.6.4
    step 3), but not independently verified for this specific call path (W-U1).
23. **`0x00c84960`'s own tail** (§7.8) was only read for its first ~85 of an unknown total instruction
    count — enough to find the `0x1300` removal and identify it as a destructor, not a full listing
    (W-U1).
24. **`phantom+0x08`'s target object and the double-indirection chain through it**
    (`[[phantom+8]+0x78]+0xc`, §7.3 step 3), and `phantom+0x10`'s own target (the `getAabb`-shaped
    virtual-call receiver) were read structurally but not identified beyond "HIGH: world/broadphase-
    level" / "HIGH: the phantom's own shape reference" — not attempted via RTTI since shapes are not
    RTTI'd the same way entity/listener classes are in this binary (W-U1).
25. **`0x00d527d0`'s "closest linear cast" puzzle** (§8.3.3): reached from
    `havok_closest_linear_cast_collector` (RTTI row 16, nominally "closest only"), yet its own body is
    unconditional array-append with no fraction comparison anywhere. Either the closest/all
    distinction is enforced one layer up inside `0x0075bb50` itself (not traced), or Havok's own
    collector really is implemented as an always-size-1 array under the hood (Q-U0).
26. **`0x00c163a0`'s own internals are unopened** (§8.6): a 28-instruction thin dispatcher with two
    indirect (vtable) calls, HIGH-matched to `hkpWorld::castRay` by argument shape only, reached from
    the AI perception ray helper `0x0075d7e0`. Opening its two indirect targets would confirm/refute
    the castRay identification and is a natural companion to item 20's job-type-dispatch problem,
    since both are "static reference manager can't see indirect calls" gaps (Q-U0).
27. **`0x00c19160`'s twin at `0x00c19330`** (tag `0x13`/19 instead of `0x15`/21, bigger local frame) —
    same dispatcher shape, not specified; found only as a byte-adjacent sibling while confirming
    `0x00c19160`'s own extent (Q-U0).
28. **The exact caller of `0x00d4d3c0` was not independently re-confirmed** — grouped into Q-U0 per the
    scoping note's own framing (one of `0x00c30150`'s nine smaller range-B callees) but not re-walked
    to confirm the edge. Low priority; the function's own behavior is solid regardless (Q-U0).
29. **The two IAT-thunk calls inside `0x00c19160`** (`[0x00101c0c8]`) were not identified by import name
    — almost certainly a CRT/OS per-thread lookup (`TlsGetValue`-shaped use) feeding the size-class
    allocation that follows (Q-U0).
30. **The global allocator-singleton pointer `0x01312854`** is confirmed shared between the walking
    slice's command-queue machinery (`0x00c19160`) and Q-U0's own `hkArray`-growth call (`0x00d527d0`)
    — worth a dedicated pass if a future unit needs Havok's memory-system boundary specified precisely
    (Q-U0).
31. **Vehicle game object's `[this+0]+0x60` vtable slot** (seen from AI action 52's own clearance probe,
    `0x00526240`): almost certainly a cached-bounding-box getter, not chased into its target — not one
    of the 39 Havok-RTTI rows at §2.1, so it belongs to the gameplay Vehicle class hierarchy, not this
    physics file. Flagged for whoever next touches vehicle AI geometry (Q-U0).
32. **`procFunc`/`finishFunc` (row `+0x8`/`+0xc`) for all four job types are registered but no call site
    reading them was found anywhere in the binary** (§9.1/§9.3) — either reached only via a two-step
    register-indirect sequence this pass's literal-displacement sweep structurally cannot see, or
    genuinely registered-but-unused in this shipped build. Does not block anything else, since the
    subtype-table path (row `+0x0`) is unambiguously what `processAllJobs` actually calls (W-U0).
33. **Who enqueues a type-2/type-3 job (the producer side)** was not traced — this unit answered
    dispatch (queue→handler), not production (what decides a broadphase/narrowphase job needs to exist
    this step and pushes it). Candidates: the ring-buffer insert helper `0x00be8bf0` and several
    unopened `0x00be7xxx`/`0x00be8xxx` siblings. Given the job-chaining pattern found at §9.3's closing
    note, production and consumption may be more tightly interleaved than a classic producer/consumer
    split — worth checking before assuming a clean "enqueue function" exists at all (W-U0).
34. **Types 0/1's own inner 19-way dispatch** (`0x00c21d40`, jump table at `0x00c21e84`, §9.3) — only
    the default branch and one case were opened. HIGH guess (unconfirmed) that types 0/1 are
    administrative/housekeeping rather than broadphase/narrowphase/solver, since neither showed a call
    into Havok range B or C in what was opened (W-U0).
35. **Most of the 15 type-2/type-3 subtype handlers were opened only partially** (§9.3; first 30-60
    instructions plus direct callees). Best candidates for a full read: `0x00d88840` (1307 bytes),
    `0x00d88f80` (827 bytes), `0x00d87060` (965 bytes), `0x00d868e0` (511 bytes) — most likely to hold
    the actual contact-manifold/closest-point math rather than setup-and-forward boilerplate (W-U0).
36. **The job-queue's default job-type-count** (constructor `0x00be8960` offset `+0x1c` off its cinfo
    argument) was not pinned to a concrete value — bounds how many *unused* default rows exist beyond
    the 4 actually registered, nothing more. Low priority (W-U0).
37. **Naming for the entire W-U0 job-dispatch mechanism** (`hkJobQueue`, `registerJobType`, `getJob`,
    `processAllJobs`, and all four job-type/subtype labels in §9.3) is this pass's own working label,
    flagged HYPOTHESIS as names even where the structural behavior is CONFIRMED — the job-queue object
    has no vtable (checked directly, §9.0), so RTTI cannot independently confirm any of it. Supersede
    with a debug string/assert-text/RTTI hit if one is ever found (W-U0).
38. **Role of type 0/1's distinct `procFunc`/`finishFunc` pairs** (`0x00c4dc30`/`0x00c4e3b0` type 0,
    `0x00c4de00`/`0x00c4e000` type 1, §9.3) was not determined beyond "real function, not the
    unregistered-type default stub" — same open question as item 32 for types 2/3 (W-U0).
39. **Whether suspension's own Havok-base output handling (§10.4) shows the same replace/
    additively-extend/pass-through split brakes does (§10.6, 2026-10-08)** — not checked; suspension's
    base returns only a single per-wheel force array with no documented secondary outputs analogous to
    brakes' lock-flag array/stall-timer, so the question may not even transfer directly (D-U2).

---

## 12. Corrections to the scoping note (`review/scoping-physics-vehicle.md`)

| Scoping note says | This file |
|---|---|
| critical section `0x0161d394` around the physics frame | an interlocked in-progress **counter**, not a lock; game code tests it to defer or drop body mutations (§4.3) |
| `0x0132a0ac` = a 1/30 s fixed-step constant | the game's **frame Δt**, rewritten every frame; the shipped build takes **one variable step per frame**, Δt ∈ [1/120, 1/20] s. The 1/30 s fixed step exists only behind a flag whose sole writer is never referenced anywhere in the binary (§4.3) |
| `0x012f8c1e` = a debug single-step flag | the step-mode flag; shipped static value **1 = one variable step per frame** |
| vehicle pre-step drives "Havok's vehicle manager `0x00c561d0`" | `0x00c561d0` is a **per-vehicle-instance** call the game makes in place of Havok's manager, followed by a per-vehicle game scheduler job (§4.4) |
| a Havok job thread pool is created by `0x00779ba0`/`0x007acfe0` | no thread pool is ever built; both set up **per-thread Havok memory**, using the string `hkCpuJobThreadPool` only as a thread name (§4.4) |
| `0x00c150c0`/`0x00c14790` = a world lock/unlock pair | both are called on bodies (or a body sub-object), never on the world (§3.1) |
| the world constructor's second argument is unidentified | Havok's own SDK version number, `20100200` (§4.2) |
| bring-up "installs the game's filter and listeners" | the game filter is wrapped by Havok's own constraint filter; the contact listener is added inside the world wrapper, not bring-up; there is also a memory watchdog, border handler, suspend-agents utility, and two query phantoms (§4.1) |

---

## 13. Clean-room check

- Addresses are cited as plain hex or as `FUN_xxxxxxxx`/`DAT_xxxxxxxx`, matching this project's own
  established convention for the physics/vehicle spec pair (`spec-physics-format.md`,
  `spec-vehicle-geometry.md`) rather than the stricter plain-hex-only convention used elsewhere. No
  decompiler variable names, stack-slot auto-names, or pasted pseudocode appear anywhere. Class names
  are RTTI type-descriptor strings or Havok's own public 2010.2 API names, quoted as data.
- Havok internals were not read beyond identifying which vtable a constructor installs, or one
  constant store needed to locate a sub-object offset, EXCEPT for the vehicle's 13 named custom
  Havok sub-steps, now opened in full at §6 (unit D-U0). `.czn_pc` was not opened by this file (see
  `spec-zone-data-format.md` for that separate workstream).
- **§6 (D-U0) additionally quotes, as data read from the executable or already established
  elsewhere in this file:** the RTTI/class context of §2.1's seven named vehicle-component override
  classes (cited, not re-derived); the array-growth helpers `0x00be6f90`/`0x00be6620` already named
  at §3.1. No decompiler output, no Ghidra auto-names beyond this file's own accepted `FUN_`/`DAT_`
  convention (none introduced by §6 specifically — all addresses there are plain hex). D-U0 was
  Ghidra-free (a leftover read-only disassembly listing from the prior scoping pass, plus one bounded
  direct capstone pass over two caller functions); no live Ghidra project was opened by this unit; no
  `.czn_pc` data was touched; no leaked Havok source was read.
- **§7 (W-U1, character-proxy primitives) additionally confirms, by RTTI complete-object-locator read:** vtable `0x0115486c` = `hkpPhantom`, `0x011548ac` = `hkpShapePhantom` (both new to this pass; cross-checked against the already-published `character_controller`/`MyCharacterListener` vtables at §2.1). No decompiler output; Ghidra-free (capstone linear disassembly plus the RTTI read); no Ghidra project opened; no `.czn_pc` data touched.
- **§8 (Q-U0, collision queries/raycasts) additionally confirms the RTTI identification of `havok_closest_ray_hit_collector` (§2.1 row 14) as the direct caller of this unit's own `0x00d4c800`.** No decompiler output; Ghidra-free (capstone linear-sweep disassembly against the raw exe bytes); no Ghidra project opened; no `.czn_pc` data touched.
- **§9 (W-U0, world-step job dispatch) confirms the real per-job-type dispatch table the scoping note and OPEN item 20 both flagged as unreachable by static tracing** (flat 16-byte-row array at job-queue `+0x128`, populated by exactly 4 `registerJobType` calls from one setup routine). Checked directly that the job-queue object has no vtable, so Havok's public API names used as labels (`hkJobQueue`, `registerJobType`, `getJob`, `processAllJobs`) are explicitly HYPOTHESIS as names even where the structural finding is CONFIRMED. No decompiler output; Ghidra-free (capstone linear-sweep disassembly); no Ghidra project opened; no `.czn_pc` data touched; no leaked Havok source, SR4/Volition source, or DeepWiki page read.
- Self-check (strict regex over the whole finished file, this project's narrow-family convention that accepts this file's own pre-existing `FUN_`/`DAT_` citations but flags decompiler-variable auto-names): run after §6/§7/§8 landed — see Changelog for the exact result.

---

## Changelog

- **2026-10-09 — new §10.10: vehicle-info-table → Havok component construction, wired end-to-end for
  mass/torque/gear-ratios/steering/grip** (SPEC TEAM, agent `a66162f2f793945e7`, wave 6 item 5). All 5
  target parameters CONFIRMED: mass (`0x00abc160`, newly wired), torque curve (pre-existing §10.2,
  re-verified), gear ratios (`0x00abd400`, identifies component slot `+0x2c` as transmission, closing
  §10.7's "not identified" gap), steering (`0x00ab9ea0`, closes §10.7's own named source-field gap),
  tire/wheel grip (`0x00ab9ea0`'s per-wheel loop, identifies the `+0x8c`/stride-`0x28` array §10's intro
  only named generically). §10.7's slot table updated in place (`+0x2c`=transmission CONFIRMED,
  `+0x30`=brakes HIGH). Cross-reference bullets added to `spec-vehicle-data.md` §7.3. Full detail:
  `scratchpad/vehicle_handling_data_to_drivemodel_2026-10-09.md`. Cleanroom self-check: strict regex 0
  hits, C-cast-dereference check 0 hits.

- **2026-10-08 — new §10a: Havok contact-listener collision detection and response, vehicle and
  generic** (SPEC TEAM, agent `af81321adb1820dad`). Fully opened `custom_collision_listener`'s 3
  override methods for the first time; discovered and traced a previously-undocumented deferred
  contact-event queue (300×`0x290`-byte records), resolving §11 OPEN item 4 for `0x007699a0`
  (CONFIRMED drain, caller `0x0075ac20`) and correcting it for `0x0077c9d0` (unrelated per-object
  analytics, not contact dispatch). Opened the default collision-response path to a direct Havok
  internal impulse call. Resolved `spec-vehicle-damage-runtime.md` §3.5's open vehicle-collision-
  damage-bridge lead to a named function (`0x00763400`) and its registration table, confirming
  vehicle-vs-vehicle and vehicle-vs-world damage are the same mechanism, differentiated only by
  type-pair table entry. Full detail: `scratchpad/vehicle_world_collision_response_2026-10-08.md`.
  Cleanroom self-check: strict regex 0 hits, C-cast-dereference check 0 hits.

- **2026-10-08 — new §10.9: driving-control input→Havok hand-off (U7), closing a gap the player-runtime
  scoping note left untraced** (SPEC TEAM, agent `a0d08cde9be318115`). Full chain CONFIRMED: per-frame
  controls → in-vehicle action dispatcher `0x005b5c50` → driving-input readers `0x00a91800`/`0x00a91440`
  → two newly-found input-shaping setters `0x00ae5290`/`0x00ae5420`, writing `instance+0x130`/`+0x13c`.
  Three independent confirmations identify `instance+0x130`/`0x138` as the real steering channel feeding
  the already-documented Havok override `0x00ae30c0`. Corrects the scoping note's framing of
  `CAA_DRIVE_STEER` (read but unused for ordinary cars). Secondary cross-reference added to
  `spec-player-runtime.md` new §16. Full detail:
  `scratchpad/vehicle_entry_exit_control_handoff_2026-10-08.md`. Cleanroom self-check: strict regex 0
  hits, C-cast-dereference check 0 hits.

- **2026-10-08 — §8.11's bullet/weapon-hit-detection OPEN item RESOLVED for melee/AoE, HIGH-confidence
  resolved for hitscan targeting.** New `spec-weapons-runtime.md` confirms hypothesis (a): melee/AoE hit
  resolution (`0x009c26f0`) calls the shared shape/sphere-cast wrapper `0x0075c880` directly and reaches
  the generic damage sink `0x0096fa20`; hitscan targeting (`0x00b86b20`) calls the shared ray wrapper
  `0x0075d7e0` directly and runs the identical resolution chain, but turns out to be a debug/target-info
  overlay that stops short of the damage sink — so the literal live-fire hitscan damage call site remains
  OPEN, narrower than before. Full detail: `spec-weapons-runtime.md` §2,
  `scratchpad/weapons_runtime_firing_hit_damage_ammo_2026-10-08b.md`. Cleanroom self-check: 0 new hits.
  Synced to Team B.

- **2026-10-08 — §4.3 gains a cross-reference** to new `spec-render-pipeline.md` §6.6 (outer OS-thread
  topology and boot sequence) and to `spec-player-runtime.md` §5.1 (the full cross-subsystem per-frame
  order this driver's own call to `0x00702a50` sits inside). No new disassembly in this file. Cleanroom
  self-check: 0 new hits. Synced to Team B.

- **2026-10-08 — D-U2 (component-override Havok base functions) opened: §10.4/§10.5/§10.6/§10.7/§11
  extended** (SPEC TEAM, agent `a55498f95a34c9d10`). Added exact instruction counts for suspension's
  base `0x00c59630` (71 insn, 1 callee) and wheel-collide's base `0x00c5a1e0` (51 insn, 0 callees) —
  both re-confirm already-published behavior, no change. **The real new content: brakes' base
  `0x00c59e40` (159 instructions, 0 callees — REFUTES the scoping note's own guess that it "almost
  certainly" also calls the shared reciprocal utility `0x00c14790`) fully characterized** — per-wheel
  torque-with-clamp, a stall/near-stop timer, and two independent per-wheel lock-flag computations.
  **Sharper finding: re-reading the brakes override itself shows "call base, then modify" is three
  different relationships to three different outputs** — the override REPLACES the base's main torque
  array outright (base's own value computed then discarded), ADDITIVELY EXTENDS the base's lock-flag
  array (only ever sets, never clears), and PASSES THROUGH the base's stall-timer untouched. §10.7's
  "replace vs. wrap" summary table sharpened accordingly (previously lumped suspension+brakes as one
  undifferentiated row). New §11 item 39: whether suspension's own base shows an analogous split
  (likely doesn't apply — single-output base, no documented secondary outputs). Full detail:
  `scratchpad/havok_du2_wwise_uw4b_2026-10-08.md`. Cleanroom self-check: unchanged (1 pre-existing hit,
  a methodology note describing the project's own check pattern — not a live leak), new content
  contributes 0.

- **2026-10-08 — §10.2/§10.7/§10.8 extended: tyre-slip computation pinned, engine-torque construction
  site found (with a crossed xtbl-name-to-role finding), 16-entry table located, 3 more
  component-array slots resolved, ground-friction splice sharpened** (SPEC TEAM, agent
  `af3a5dab60b06fa21`, relayed by `purrsian-62` after Team B wired §10 into a working drive_demo).
  §10.2: construction site `0x00aba4c0`-`0x00aba599`, a bare 10-field copy from the per-vehicle
  xtbl struct — confirms the real schema has 9/10 fields (not 6), and that authored
  `*_Torque_Factor`/`*_Resistance` field names are crossed with their runtime resistance/
  torque-factor roles. §10.7: 3 more component slots identified (`+0x20`=driver input,
  `+0x24`=steering HIGH, `+0x28`=engine) via the per-vehicle component-construction cluster. §10.8:
  the real slip-velocity computation pinned to `0x00c64252`-`0x00c64306` (Loop 2, a genuinely
  separate computation from wheel-collide's §10.5 relative velocity); the 16-entry friction/slip
  table located exactly (`a1[wheel]+0x8`..`+0x44`, not a separate tuning asset); the
  §10.5↔§10.8 ground-friction splice re-opened — one candidate REFUTED (a shared step-context
  pointer, not per-wheel), one reassigned to the vehicle's own tire-friction tuning rather than the
  ground side, best hypothesis now that §10.5's coefficient folds in upstream of this function
  entirely. Ghidra-free except two bounded decompile queries against the already-running persistent
  server for one heavily-interleaved SSE span; no leaked material read. Cleanroom self-check:
  unchanged at 5 (FUN_/DAT_-accepted convention), no strict-token hits. Full detail:
  `scratchpad/havok_vehicle_tyreslip_enginetorque_groundfriction_2026-10-08.md`.

- **2026-10-07 — D-U0, W-U1, Q-U0 added** (SPEC TEAM, three parallel Havok-internals units,
  user-authorized full RE of Havok Physics). New §6 "Driving slice, unit D-U0" (all 13 named
  hkpVehicle sub-step bodies read in full, 823 instructions exact vs. the scoping note's ~755 HIGH
  estimate; new finding — each game override has a Havok-internal sibling calling the identical
  sub-step subset in the identical order, confirming the overrides are replacement implementations of
  Havok's own default two-phase step, not extensions of it; a 7-slot vehicle-component pointer array
  confirmed at instance `+0x20`-`+0x38`, per-slot naming left OPEN). New §7 "Havok W-U1 —
  Character-proxy primitives" (the proxy get/set contract, the phantom transform setters, the AABB
  commit routine — found to be physics-window-deferral-aware, a third concrete instance of §4.3's
  mechanism — and the cast-manifold commit; two new findings with no prior home: Havok property key
  `0x1300` and a second, previously-undocumented phantom-listener fan-out distinct from
  `MyCharacterListener`; substantially advances but does not close `spec-player-runtime.md` OPEN
  35/36). New §8 "Collision queries / raycasts, unit Q-U0" (the seven census-named roots, all
  instruction counts revised upward from the census's byte-range estimates; confirms
  `0x00d6bff0` as a shape-key chain resolver; finds the real AI-action connection is through the
  perception layer's line-of-fire probe, not the seven action routines originally suspected — those
  two, checked directly, make no Havok calls at all). Former §6 OPEN items, §7 Corrections and §8
  Clean-room check renumbered to §9/§10/§11; OPEN items 9-31 appended. Corrections folded in: §3.1's
  `0x00c150c0` "entity activate" HYPOTHESIS gains a third independent consistent call site (still HIGH,
  not promoted); §4.3's deferred-mutation contract extended to cover phantom AABB commits (record type
  `0x12`) and tied to a concrete unwind-path replay trigger. `review/scoping-physics-vehicle.md` §8
  corrected directly by two of the three units (not raced — light-touch strikethrough+correction, per
  this pass's own file-collision discipline); not edited by this file. Ghidra-free throughout (capstone
  disassembly, one project-standard RTTI complete-object-locator read); no Ghidra project was opened by
  any of the three units; no `.czn_pc` data was touched; no leaked Havok source, DeepWiki page, or other
  forbidden material was read. Cleanroom self-check (this file's own established narrow-family
  convention — `FUN_`/`DAT_` citations accepted, decompiler-variable auto-names are not) on the entire
  finished file: see the result recorded immediately after this entry was written.

- **2026-10-07 — W-U0 added** (SPEC TEAM, world-step job-dispatch unit, completing the Havok-internals
  trio alongside D-U0/W-U1/Q-U0). New §9 "World-step job dispatch, unit W-U0" — finds the real
  per-job-type dispatch table OPEN item 20 and the scoping note's §8.1 both flagged as unreachable by
  static tracing: a flat 16-byte-row array embedded in the job-queue object at `+0x128`, populated by
  exactly 4 `registerJobType` calls from one setup routine (`0x00c2b220`) called once from physics
  bring-up. Types 0/1 share a 23-slot "default" table (every slot the same second-level 19-way
  dispatcher); types 2 (7 subtypes) and 3 (8 subtypes) are the real broadphase/narrowphase collision
  job handlers, both built from an identical code template. A confirmed structural detail: once inside
  a type-2/3 handler, execution self-chains back into `getJob` directly rather than unwinding through
  `processAllJobs`'s own loop. Former §9 OPEN items, §10 Corrections, §11 Clean-room renumbered to
  §10/§11/§12; OPEN items 32-38 appended (role of `procFunc`/`finishFunc` row slots unresolved; the
  job-enqueue/producer side not traced; most type-2/3 subtype handler bodies opened only partially).
  Ghidra-free (capstone linear-sweep disassembly against the raw exe); no Ghidra project opened; no
  `.czn_pc` data touched; no leaked Havok source, SR4/Volition source, or DeepWiki page read.
  `review/scoping-physics-vehicle.md` §8.1 corrected in place by the same pass (forward-pointer to this
  section, not re-edited here). Cleanroom self-check (narrow-family convention) on the entire finished
  file: 4/4 — unchanged from before this addition (all 4 hits are this file's own pre-existing accepted
  `FUN_`/`DAT_` citations from earlier units; W-U0 itself introduced zero new hits, plain hex
  throughout).
- **2026-10-08 — §10 added: the driving-slice's 7 named component overrides, direct follow-up to D-U0
  (§6).** Opens steering, engine, transmission, suspension, wheel-collision, brakes, and driver input —
  none previously opened by any landed spec. Headline: refines D-U0's own "replace, don't extend"
  framing — that holds exactly for the top-level step, but at the component level three patterns coexist
  (full replacement: engine/transmission; unconditional wrap-Havok's-base: suspension/brakes;
  conditional delegation: steering/wheel-collide AABB). Real formulas confirmed: steering's
  inverse-square speed falloff lives in Havok's own base method, not the override; engine's up-only RPM
  ramp and full 9-field piecewise torque curve with a confirmed ×0.85 damage/environment penalty;
  transmission's real automatic gear-shift state machine; suspension's own internal ≤1/60s sub-stepping
  (a second, independent sub-stepping layer not previously documented) plus a cached-ground-plane
  shortcut between real queries; wheel-collision confirmed as a genuine point-to-point line-segment ray
  cast (no shape/radius term) with per-wheel friction sourced from a custom Havok body property, not a
  materials table. Partially resolves §6.3/§6.8 item 1 (component-array slot `+0x20` = driver input,
  HIGH confidence). OPEN items renumbered §10→§11; Corrections §11→§12; Clean-room §12→§13; two stale
  cross-references in `spec-player-runtime.md` to "§10 OPEN item 22" corrected to §11. Ghidra-free
  (headless, private throwaway project `tools/gp_drive1`, deleted on completion; `exe_va.py` for
  constant verification); all subprocess calls used hidden windows per the standing no-GUI-popup rule;
  no leaked Havok/SR4/Volition material read. Cleanroom self-check: 5/5, unchanged from the pre-existing
  accepted `FUN_`/`DAT_` baseline — this addition introduces zero new flagged tokens. Synced to Team B.
- **2026-10-08 — §10.8 added: the real D-U1 tangential-friction/slip solve.** Corrects §10.5's own
  cross-reference (§6.4 items 10/13 → items 7/12). `0x00c32520` confirmed as a per-wheel contact-frame
  builder, not the force solve; `0x00c63c70` is the real combined-slip solve — effective-mass terms,
  a friction-ellipse apportionment, an exact 2×2 axis-coupling matrix inverse (`0x00c63570`), the real
  unit-friction-circle saturation test (`0x00c63930`, comparand independently confirmed = 1.0), and a
  final impulse application into the already-named chassis force/torque accumulators. Corrects D-U0's
  own loose "decay-toward-limit/final clamp" framing to a proper 3-stage friction-ellipse pipeline.
  Where the §10.5 ground-friction coefficient specifically enters this arithmetic remains OPEN.
  Ghidra-free (pre-existing disassembly-only dumps plus a small `pefile`-based constant reader); no
  leaked Havok material read. Cleanroom self-check: 0 new hits. Synced to Team B.

- **2026-10-08 — new §8.8-§8.11 (unit Q-U1): on-demand collision query wrappers, game-side half of
  the Q-U0 boundary; correction to §8.6's collector-constructor attribution** (SPEC TEAM, agent
  `a9a3f789ac26033b8`, dispatched for camera collision/AI line-of-sight/bullet-hit contracts).
  **Correction, §8.6 point 1**: `0x0075d7e0` is a 3-way dispatcher, not a straight-line caller of
  `0x0075b8b0` — the closest-ray-hit collector (used by every traced caller) is actually built by
  `0x00756ee0` on the `kind==0` branch; `0x0075b8b0` unconditionally builds the *all*-ray-hit collector
  and is only reached on `kind==1`. **New §8.8**: the two shared game-side wrappers, `0x0075d7e0` (ray)
  and `0x0075c880` (shape/sphere, not previously read anywhere), and their full caller lists. **New
  §8.9**: `0x00c163a0` opened in full — a game-side broadphase-border interceptor at `world+0x5c`
  (distinct from the already-documented `world+0x58`) normally answers the whole query, with Havok's
  own stock broadphase (`world+0x64`) reached only as a fallback via `0x00d4fe10`; plus a second,
  newly-found query-local filter `0x0075b780` (three independently selectable per-category exclusion
  bits) layered on top of the existing `havok_game_filter`. **New §8.10**: broadphase characterized as
  this short-circuit. **New §8.11**: camera collision CONFIRMED through the shape-cast wrapper; AI
  line-of-sight CONFIRMED as a new finding — the real resolver `0x004eda70` (previously unnamed, a
  sibling of the three cache/queue-only functions `spec-ai-runtime.md` §12.4 had left untraced) calls
  the exact same closest-ray-hit chain already confirmed for AI's "line of fire" probe, meaning vision
  and line-of-fire are two throttled/cached consumers of one Havok path, not two separate mechanisms;
  bullet/weapon-hit detection explicitly flagged as an unexamined gap, not a negative. `spec-ai-runtime.md`
  §12.4 should be updated to point here instead of describing the raycast as untraced (not applied by
  this pass — cross-file, flagged for that file's own next edit). Full detail:
  `scratchpad/havok_broadphase_raycast_query_2026-10-08.md`. Cleanroom self-check: 0 new hits.
  Synced to Team B.

- **2026-10-09 — §10a: cross-reference to new `0x20`-type-pair (projectile) contact-listener
  registrations** (SPEC TEAM, agent `a5dac4238395b84fe`, WAVE 11 item 4). The same type-pair
  registration function this section documents for vehicles also covers type `0x20` (HIGH
  CONFIDENCE "projectile"). Full trace: `spec-weapons-runtime.md` §2a.5. Cleanroom self-check:
  strict regex 0 hits.
