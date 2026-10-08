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
version, and the two boundary-pair functions (`FUN_00bf81a0`, `FUN_00c1c4f0`); this file's §1–§2 refine
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
| **Havok range B** | `0x00d4c800` | `0x00d9dff0` | 1,000 | 91,547 | Havok collision: shapes (incl. `hkpBoxShape` constructor `FUN_00d4d7f0`), MOPP, agents, `hkp3AxisSweep` |
| (game) | `0x00d9dff0` | `0x00ecb400` | — | — | game timer helper, CRC-32, the job scheduler (§4.4), CRT |
| (thunks) | `0x00ecb400` | `0x00ecb412` | 3 | 3 | XINPUT1_3, DINPUT8 thunks |
| **Havok range C** | `0x00ecb420` | `0x00f22240` | 552 | 99,809 | geometry processing, KD-tree, GJK/penetration, plus the product-keycode check `FUN_00f21bd0` |
| (game: Wwise and on) | `0x00f22240` | — | — | — | first function is a Wwise RTTI-identified destructor |
| **Havok total** | | | **4,038** | **377,900** | |

**No game code lives inside the three ranges (HIGH).** 42 functions inside them have only non-Havok
call neighbours; every one is an API leaf the game calls that itself calls nothing large (e.g. the
`hkpBoxShape` constructor, 10 game callers). None carries a game string.

**Havok code lives outside the three ranges too (CONFIRMED by RTTI).** Havok's header-inline virtual
methods were compiled into Volition's own object files, so 121 slot targets of Havok-class vtables lie
outside A/B/C: identical-code-folded stubs shared program-wide (`FUN_00754410` — the project's known
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

## 6. OPEN items

1. **Where the constraint filter's listener sub-object is added.** Shutdown removes it, but no
   bring-up body adds it — it may happen inside Havok's own world constructor.
2. **Construction-info field names** (§4.1 steps 4–5): offsets/values are CONFIRMED, field names are
   HYPOTHESIS from Havok 2010's public header order. Most open: the byte at `+0xe8` = 1, the
   solver-preset argument 5, and the two job-queue construction fields.
3. **When vehicle code sets the sub-step multiplier k = 2.0** (`0x00a86620`) — has no effect in the
   shipped default mode, so low priority; a later vehicle unit's concern.
4. **Post-step roles of `0x0077c9d0` and `0x007699a0`** — likely contact-event dispatch and handing the
   stepped time to gameplay; both are game code with no direct Havok call.
5. **Which callbacks specifically run on worker threads** during the step, beyond the HIGH-confidence
   general statement in §4.6 item 5.
6. **The 41 small Havok-called functions outside the three ranges** (§1) — taken as a class
   (header-inline/folded); only two of them (§3.1) were identified individually.
7. **`0x00769c30`** (22,979 instructions, collision-material/layer tables) — not opened by this unit.
8. **The unattributed range `0x00d1d570`–`0x00d4c800`** (§1) — still unidentified, out of scope here.

---

## 7. Corrections to the scoping note (`review/scoping-physics-vehicle.md`)

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

## 8. Clean-room check

- Addresses are cited as plain hex or as `FUN_xxxxxxxx`/`DAT_xxxxxxxx`, matching this project's own
  established convention for the physics/vehicle spec pair (`spec-physics-format.md`,
  `spec-vehicle-geometry.md`) rather than the stricter plain-hex-only convention used elsewhere. No
  decompiler variable names, stack-slot auto-names, or pasted pseudocode appear anywhere. Class names
  are RTTI type-descriptor strings or Havok's own public 2010.2 API names, quoted as data.
- Havok internals were not read beyond identifying which vtable a constructor installs, or one
  constant store needed to locate a sub-object offset. The vehicle's custom Havok sub-steps and
  `.czn_pc` were not opened.
