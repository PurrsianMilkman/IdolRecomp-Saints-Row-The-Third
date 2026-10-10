# Scoping note — physics simulation and vehicle handling, and where Havok sits

**Date:** 2026-10-03 · **Type:** scoping note only — no spec content, nothing here is cleared for implementation.

**Question:** which engine pieces make up physics simulation and vehicle handling; how big they are; what is already specified; in what order to specify the rest. **The main extra question:** exactly where Havok sits, meaning:
- which game functions call into it;
- what data crosses the boundary;
- how much of the system is the game's own code and how much is middleware.

**Method:**
- Read-only Ghidra headless runs against a private copy of the project (`tools\gp_physscope`, deleted afterwards).
- Four small read-only survey scripts, kept in the session scratchpad and not in the repo:
  - an RTTI census (type descriptor → complete-object locator → vtable, plus class-hierarchy base lists);
  - string-anchor sampling per address bucket;
  - a call-in census across candidate middleware ranges;
  - callee-closure sizing from root functions.
- `CrreishDump.java` `func`/`xref`/`str` for individual listings.
- Spec documents read: `spec-physics-format.md` §1–§4, `spec-vehicle-data.md` §7, `spec-vehicle-geometry.md` (the `0x00ab0a10` probe), `spec-lua-api-behaviour.md` (vehicle resolver, force flags, airplane bindings), `HANDOFF.md` §28.7, and the model note `review/scoping-ui-render.md`.

**Size figures.** Instruction counts are over defined function bodies. Function starts that the shared project never defined are missed, so treat totals as about ±10 %.

**Range boundaries** were placed where string anchors and call patterns change. They are accurate to within a few hundred bytes, not to the byte.

**Confidence labels** are the project's usual ones:
- **CONFIRMED**: read directly in this pass.
- **HIGH**: inferred from structure.
- **HYPOTHESIS**: a plausible reading, not checked.

**Naming.** Class names below are the binary's own RTTI type-descriptor strings, and Havok class names are third-party public API names. Both are shipped data, quoted the same way `scoping-ui-render.md` quotes `vint_*` classes.

---

## 0. Verdict up front

**Physics is overwhelmingly Havok by size, but vehicle handling is not a thin wrapper.**

1. **Havok is present and statically linked** (CONFIRMED, re-checked).
   - It is Havok 2010.2.0-r1, in three address ranges.
   - Together they hold about **4,050 functions and ~377k instructions**. That is roughly 35–40 times the whole UI render core.
   - Inside those ranges the game calls only **326 distinct Havok functions** (about 8 %) directly.
2. **The game's own physics-facing code is about 70k instructions in ~470 functions** (callee closures), inside two clusters totalling ~180k instructions.
   - About 51k of it is general integration: world lifecycle, the step, bodies, filters, contact listeners, queries, the character controller, ragdoll and buoyancy.
   - About 21k is **vehicle physics**.
3. **Vehicle handling is Havok's vehicle framework, with Volition overriding almost every component.**
   - Overridden: engine, steering, transmission, brakes, suspension (one 992-instruction override), wheel collision, driver input, an extra downforce action, and separate motorcycle, aircraft and watercraft instances.
   - The game's custom vehicle step **calls about 13 of Havok's own internal vehicle sub-steps one at a time**, with its own logic between them.
   - That makes it the hardest seam to replace. A replacement engine must reproduce what those Havok sub-steps compute, not only Havok's public API. Handling feel lives there.
4. **Already specified:**
   - the data layer: `_veh.xtbl` and the runtime vehicle-info table;
   - the collision-mesh container (`.clmesh_pc`);
   - the Lua-facing control surface;
   - Havok's identity.

   **Nothing in the runtime simulation path is specified:** world setup, the step, bodies, filtering, contacts, the vehicle components, the character controller.

**Rough size:**

| Part | Functions | Instructions | Needs RE? |
|---|---|---|---|
| Havok (three ranges) | ~4,050 | ~377k | No. Identify and replace; document only the call-site contract. |
| Game general physics integration (closure) | ~277 | ~51k | Yes |
| Game vehicle physics (closure) | ~196 | ~21k | Yes, and it is the deepest coupling to Havok internals |
| Game vehicle object, damage, customisation (not Havok-facing) | ~850 | ~73k | Partly. Mostly gameplay; Lua-facing parts are specified. |

---

## 1. The pipeline: from game frame to Havok and back

```
game frame 0x00702a50
  └─ 0x0075ac20  physics frame  (critical section 0x0161d394; 1/30 s constant at 0x0132a0ac)
       ├─ game pre-step: 0x0079a380 (buoyancy side), 0x00785cb0 (ragdoll side)
       ├─ vehicle pre-step 0x00ac3f40 ──► Havok vehicle manager 0x00c561d0
       ├─ 0x00755c80  THE STEP
       │    ├─ Havok 0x00c17920 (world, job queue, Δt)            begin multithreaded step   [HIGH]
       │    ├─ game scheduler 0x00dc2160 / 0x00dc21f0 × N, worker 0x00754dd0  (game's own job system)
       │    ├─ Havok 0x00be88c0 (job queue) on the calling thread
       │    ├─ Havok 0x00c16fc0 (world, job queue)                 finish multithreaded step  [HIGH]
       │    └─ wait 0x00dc22c0
       │         ... inside the step, Havok calls back into game code through vtables:
       │         actions (applyAction):   hk_custom_vehicle_instance 0x00ae9b40 → 0x00ae9a60 → 0x00ae98f0
       │                                    ├─ 0x00ae9570  ──► Havok vehicle sub-steps 0x00c55f80…0x00c58770
       │                                    └─ 0x00ae7470  ──► Havok vehicle sub-steps 0x00c565b0…0x00c57ca0
       │                                  component overrides: engine/steering/transmission/brakes/suspension/wheel-collide
       │                                  hk_custom_vehicle_extra_downforce 0x00ae3850, havok_const_accl_action, …
       │         collision filter:        havok_game_filter 0x00778950…0x00779260
       │         contact callbacks:       custom_collision_listener 0x00769040 / 0x00769500 / 0x00769750
       │         constraint callbacks:    constraint_instance_breakable_listener 0x00776740 / 0x00777fe0
       │         water / phantom:         hk_rigid_body_in_water_listener 0x00796940, hk_phantom_in_water_listener 0x00796990
       └─ game post-step: 0x00756790, 0x0077c9d0, 0x00755bb0, 0x00758f00, 0x007699a0, then 0x0075abd0
             (results read back out of Havok bodies by gameplay code through the world handle 0x0161d378)
```

### 1.1 Lifecycle (game side, CONFIRMED as call sequences)

**`0x0075f370` — physics bring-up** (311 instructions, called once from `0x005d25f0`). In order, it:
1. initialises Havok memory (`0x00788a60`, `0x00779da0`; strings `havok bmem 0/1`, `havok_memory.cpp`);
2. builds the world construction info;
3. constructs the world through `0x007572f0` → Havok **`0x00c1c4f0`** (1,172 instructions; the only function using the `hkpWorld.cpp` assert path, at two sites). HIGH: this is the world constructor. It is also the second of the two addresses `spec-physics-format.md` §3 left NEEDS-EXE; see §6;
4. stores the world pointer at **`0x0161d378`**, and a Havok job queue at `0x0161d388`;
5. calls Havok setup helpers (`0x00c2b220`, `0x00c2b380`, `0x00c2b750`, `0x00c2b870`, `0x00c88750`, `0x00be8910`/`0x00be8960`);
6. installs the game's filter and listeners and runs the game sub-inits: `0x00779f00`, `0x0077a2c0` (memory/ragdoll), `0x0079af20` (character), `0x007669d0`, `0x00757100`, `0x007920c0` (water).

Two more lifecycle facts:
- It writes a global float at `0x0131e81c`. The same global is used in `spec-physics-format.md` §4.4.6 and in `spec-vehicle-geometry.md`'s box-shape probe. **HYPOTHESIS:** this is Havok's default convex radius.
- **`0x007590d0` — physics shutdown** (66 instructions, called from `0x005d39e0`) releases the world, job queue and helpers.

The job thread pool is created by `0x00779ba0` and also by `0x007acfe0` (the boot/city-load path), both using the string `hkCpuJobThreadPool`.

### 1.2 Per-frame step (HIGH; the call order is CONFIRMED, the Havok roles are inferred)

**`0x0075ac20`** (~111 instructions, called from `0x00702a50`):
- holds the critical section `0x0161d394` around the whole physics frame;
- reads timing globals: `0x0132a0ac` = 0.0333…, i.e. 1/30 s; `0x012f8c18` = 3 (**HYPOTHESIS:** a sub-step cap); `0x012f8c14` = 1.0;
- reads the debug single-step flag `0x012f8c1e`, which is toggled by the console handler `0x00754c30` (`Single Step is %s`);
- runs the pre-step / vehicle / step / post-step order shown in the diagram.

**`0x00755c80` — the step** (64 instructions; the only game function using the `havok.cpp` source-path string):
1. starts Havok's multithreaded step;
2. fans out worker jobs on the **game's own scheduler** (vlib, `scheduler thread %d`), so Havok's own thread pool is not what runs the step;
3. drains the job queue;
4. finishes the step.

A replacement engine has to fit this two-phase step-plus-job-queue shape, or the call site has to change.

### 1.3 Bodies, shapes and collision setup (HIGH)

**Bodies.** Every game physics body is a **`havok_rigid_body`**, an RTTI subclass of `hkpRigidBody` whose only game override is the destructor (`0x00756fe0`).
- It is registered by name with Havok's class registry in a static initialiser at `0x00ff803e`, which calls `0x00bebf30` with game helpers `0x00755ad0`, `0x00755a70` and `0x0079a660`.
- There is one Havok world, `game_havok_world`, a subclass of `hkpWorld`.

**Shapes come from three sources:**
- **level collision** — `.clmesh_pc`, handed to a Havok MOPP tree (already specified to HIGH, `spec-physics-format.md` §4.4.6(g); its constructor `0x00d56010` sits inside Havok range B);
- **vehicle part primitives** — `spec-vehicle-geometry.md`'s probe of `0x00ab0a10` calls `0x00d4d7f0`, which installs the vtable `0x01243174`. That vtable is **`hkpBoxShape`**'s (RTTI), so the probe builds a Havok box shape per part;
- **the character's shape phantom** (`hk_character_shape_phantom`).

**Filtering.** All filtering goes through **`havok_game_filter`**, which implements all five Havok filter interfaces (`hkpCollisionFilter`, `hkpCollidableCollidableFilter`, `hkpShapeCollectionFilter`, `hkpRayShapeCollectionFilter`, `hkpRayCollidableFilter`). Its ~10 game methods are at `0x00778950`–`0x00779260`. The collision-layer rules are game logic.

### 1.4 Collision detection and response (Havok) → feedback into gameplay (game)

Havok does all broadphase work (`hkp3AxisSweep`), narrowphase (GSK and the box/sphere/capsule/triangle agents, all in range B), solving and TOI (continuous simulation). The game hears about results in four ways:
- **Contacts:** `custom_collision_listener` (`hkpContactListener`), three methods at `0x00769040`, `0x00769500` and `0x00769750`, in the range that carries the `Collision` material strings. Closure ~43 functions / ~3.9k instructions, which then leaves for 32 gameplay functions (damage, effects, sound).
- **Breakable constraints:** `constraint_instance_breakable_listener` (`hkpConstraintListener`).
- **Queries:** six game collector classes over Havok's ray, linear-cast and body-pair collectors (`havok_closest_ray_hit_collector`, `havok_all_linear_cast_collector`, `object_collector`, `contact_collector`, …), plus `havok_wheel_closest_linear_cast_collector` and `man_cannon_vacuum_collector`. Closure ~23 functions / ~0.7k instructions. These are thin.
- **State read-back:** gameplay reads positions and orientations through the world handle `0x0161d378`, which is read in **98 functions at 144 sites**, spread far beyond the integration cluster.
  - **HYPOTHESIS:** Havok's transform accessors are inline in its headers, so many reads are raw field loads at Havok object offsets with no call. The call census in §3 cannot see those.

### 1.5 Vehicle handling (CONFIRMED call chain, HIGH roles)

The game subclasses Havok's vehicle framework. Each class below is an RTTI name and its Havok base; game override addresses are in §3.2.
- `hk_custom_vehicle_instance` (base `hkpVehicleInstance`, which is an `hkpUnaryAction`);
- `hk_custom_motorcycle_instance`, `hk_custom_aircraft_instance` and `hk_custom_watercraft_instance` (all derived from it);
- the custom components `hk_custom_vehicle_engine`, `_steering`, `_transmission`, `_brakes`, `_suspension`, `_driver_input` and `_raycast_wheel_collide`, each over the matching `hkpVehicleDefault*` class;
- `hk_custom_vehicle_extra_downforce` (an `hkpUnaryAction`).

**Per step:**
1. **Vehicle pre-step `0x00ac3f40`** (98 instructions) drives Havok's vehicle manager `0x00c561d0`.
2. Inside Havok's step, each vehicle's action override **`0x00ae9b40`** → `0x00ae9a60` → **`0x00ae98f0`** runs the custom vehicle step:
   - **`0x00ae9570`** (227 instructions) calls six Havok vehicle internals (`0x00c55f80`, `0x00c56040`, `0x00c560a0`, `0x00c562d0`, `0x00c58360`, `0x00c58770`) and two world-lock-style helpers;
   - **`0x00ae7470`** (343 instructions) calls seven more (`0x00c565b0`, `0x00c566e0`, `0x00c56800`, `0x00c57690`, `0x00c576d0`, `0x00c57a50`, `0x00c57ca0`) with game helpers between them (`0x00ae6000`, `0x00ae6470`, `0x00ae5910`, `0x00ae48e0`, `0x00ae71c0`, `0x00ae6bd0`, `0x00ae6760`, `0x00ae68f0`).
   - `0x00ae9a60` also reaches vehicle-class code at `0x00ad2f80`, `0x00ad4040` and `0x00ad9420` (aircraft/boat side; partly cited by `spec-lua-api-behaviour.md`).
3. Most component overrides call the Havok default implementation and then adjust the result:
   - suspension `0x00aebe30` (992 instructions) calls the base `0x00c59630`;
   - brakes `0x00ae2c80` (293) call `0x00c59e40`;
   - the wheel-collide override `0x00aea040` calls `0x00c5a1e0`.

   **HIGH:** these are "call base, then modify" overrides.

**Havok's own vehicle module** (`0x00c55000`–`0x00c5d000`) is only **145 functions / ~8.6k instructions**. The game calls 35 of them from 21 functions. Most of the handling behaviour is therefore in the game's overrides plus a small Havok core, which makes it tractable.

**Where the data comes from:** `_veh.xtbl` → runtime vehicle-info table (`spec-vehicle-data.md` §7; parser strings at `0x00ac5000`–`0x00ad0000`: `Engine`/`Torque`/`Min_RPM`…, `Steering`/`Max_Steering_Angle`…, `Drifting_*`, `Wheelie`, `Suspension_Raise_Max`, `Center_Of_Mass_Y_Offset`, `Static_Load_Friction`). **HIGH:** the custom components read those values. The mapping from table fields into component parameters is not traced.

---

## 2. Main functions and data structures, with rough sizes

| Stage | Key addresses | Instructions (approx.) | What it does |
|---|---|---|---|
| Lifecycle | init `0x0075f370`, world ctor wrapper `0x007572f0`, shutdown `0x007590d0`; closure from these + step | 311 + 66 (roots); closure ~40 functions / ~25k | Havok memory, world construction info, world, job queue, listeners, sub-inits |
| Frame + step | `0x0075ac20`, `0x00755c80`, worker `0x00754dd0` | ~111 + 64; closure from `0x0075ac20` ~134 functions / ~17k | fixed-step frame, multithreaded Havok step on the game scheduler |
| Bodies / world users | world handle `0x0161d378` (98 functions); `havok_rigid_body` | — | creation, add/remove, transform read-back across gameplay |
| Collision filter | `havok_game_filter` `0x00778950`…`0x00779260` | ~10 methods (in the contact closure) | layer/group rules |
| Contacts + constraints | `custom_collision_listener` ×3, breakable-constraint listener ×2 | closure ~43 functions / ~3.9k (then into gameplay) | contact → damage/effects/sound |
| Queries | 8 collector classes, `0x00755730`…`0x0075bfb0`, `0x00aea480`/`0x00aeab50`, `0x00ab1d60` | closure ~23 functions / ~0.7k | ray / linear cast / overlap wrappers |
| Character controller | `hk_custom_character_proxy`, `hk_character_shape_phantom`, `MyCharacterListener`, `character_controller`; `0x0079a670`…`0x007a45f0` | closure ~29 functions / ~2.6k; range ~11k | player/NPC movement on a Havok proxy |
| Ragdoll | `0x00785cb0` (pre-step), range `0x0077a000`–`0x00792000` (`Ragdoll_Collision`, bone names) | closure from the pre-step ~0.7k; range ~25k shared with memory | Havok ragdoll setup and blending |
| Water / buoyancy | in-water listeners `0x00796940`/`0x00796990`, `0x0079a380`; range `0x00792000`–`0x0079b000` | closure ~35 functions / ~7.7k | buoyancy and wave forces |
| **Vehicle physics** | 45 roots (§3.2) + pre-step `0x00ac3f40` + custom step `0x00ae98f0`/`0x00ae9570`/`0x00ae7470` | **closure ~196 functions / ~21k** | the handling model |
| Vehicle object / damage / customisation | `vehicle` vtable `0x01180c44` (methods `0x00a77000`–`0x00a90000`); damage `0x00a90000`+; `vehicle_deformable_mesh_instance` (`0x00b13000`–`0x00b19000`, ~6k, no Havok calls) | ~73k + ~6k (ranges) | hit points, damage types, visual deformation — gameplay, not simulation |
| `_veh.xtbl` handling-table parse | `0x00ac1840`/`0x00ac2ac0`, `0x00ac5000`–`0x00ad0000` | ~12.6k (range) | **specified** (`spec-vehicle-data.md` §7) |
| Havok vehicle module | `0x00c55000`–`0x00c5d000` | 145 functions / ~8.6k | middleware |
| Havok (all) | three ranges, §3.1 | ~4,050 functions / ~377k | middleware |

**Data structures** (scoping identifications only; no layouts claimed):
- the world handle `0x0161d378` and the job queue `0x0161d388`;
- the world construction info built in `0x0075f370`;
- `havok_rigid_body` (an `hkpRigidBody` subclass; game objects own one);
- the collision-filter object;
- the vehicle instance and its seven component objects (one each for engine, steering, transmission, brakes, suspension, driver input and wheel collide; `hkpVehicleData`, `hkpVehicleDriverInputAnalogStatus`);
- the runtime vehicle-info table (specified);
- the pool of `vehicle_damage_sim_event` (allocator vtable `0x01181914`).

**Totals.** "Closure" means the game functions reachable from the listed roots, restricted to the cluster ranges. That is a lower bound, because each closure also leaves the cluster for gameplay code.

| Scope | Functions | Instructions |
|---|---|---|
| Game general physics integration (closure; overlaps the frame closure) | ~277 | ~51k |
| Game vehicle physics (closure) | ~196 | ~21k |
| **Game physics-facing code needing RE (sum of the two)** | **~470** | **~70k** |
| Game clusters in full (`0x00754000`–`0x007a5000` + `0x00a77000`–`0x00af0000`) | ~2,400 | ~178k (includes gameplay vehicle code; the parse part is specified) |
| Havok | ~4,050 | ~377k |

---

## 3. Where Havok sits — the boundary (the extra question)

### 3.1 Presence and extent (CONFIRMED)

**Present, version 2010.2.0-r1, statically linked.** This re-confirms `spec-physics-format.md` §1:
- no physics DLL among the imports;
- Havok SDK assert paths under the game's own `...\sr3\src\game\havok\hk2010_2_0_r1\Source\...` tree;
- version string `hk_2010.2.0-r1`;
- a Havok product keycode string naming the licensee and the Physics product, dated 2012-06-02, at `0x01152dc0` and read by Havok `0x00c81690`. The key itself is deliberately not reproduced here.

**RTTI census:**
- 1,720 type descriptors and 1,701 vtables;
- **443 vtables of `hk`-named classes** (`hkp` 321, `hk` 116, `hkgp` 6);
- **1,618 of the 1,752 distinct Havok-class vtable slot targets** fall in the ranges below.

**Code extent.** String anchors show that the obvious "Havok block" contains OpenSSL:

| Range | Contents | Functions | Instructions |
|---|---|---|---|
| A `0x00be5ce0`–`0x00c98e00` | Havok Base (memory, containers, stack tracer, monitor/report), Physics dynamics (world, simulation incl. multithreaded and continuous, constraints, actions), **Vehicle** (`0x00c55000`–`0x00c5d000`), character proxy, utilities, packfile serialization, `hkStructureLayout` | 2,496 | ~187k |
| *(not Havok)* `0x00c98e00`–`0x00d1d800` | **OpenSSL 1.0.0d** (used by libcurl at `0x0041c700`/`0x0041d200`) | 1,809 | ~158k |
| *(not Havok)* `0x00d1d800`–`0x00d4c600` | unattributed; Steam DRM IPC and NTDLL helpers at its end | 915 | ~58k |
| B `0x00d4c600`–`0x00d9e140` | Havok collision: shapes, MOPP, collection and BV-tree agents, GSK and primitive agents, `hkp3AxisSweep` broadphase | 1,007 | ~92k |
| C `0x00ecb000`–`0x00f20000` | Havok geometry processing, KD-tree, GJK/penetration (`hkgpConvexHull.cpp`, `hkKdTreeBuilder.cpp`, `hkgpMesh.cpp`) | 549 | ~98k |
| **Havok total** | | **~4,050** | **~377k** |

### 3.2 The game side of the boundary — game classes that implement Havok interfaces (CONFIRMED from RTTI)

Havok calls game code **only through these vtables**. Each row is a callback contract.

| Game class (RTTI) | Havok base(s) | Game override addresses |
|---|---|---|
| `game_havok_world` | `hkpWorld` | `0x00757370` |
| `havok_rigid_body` | `hkpRigidBody` | `0x00756fe0` |
| `custom_collision_listener` | `hkpContactListener` | `0x00769040`, `0x00769500`, `0x00769750` |
| `havok_game_filter` | `hkpCollisionFilter` + 4 filter interfaces | `0x00778950`, `0x007789a0`, `0x00778a00`, `0x00778a30`, `0x00778c50`, `0x00778d50`, `0x00778ef0`, `0x00778f70`, `0x00779160`, `0x00779260` |
| `havok_const_accl_action`, `multi_game_havok_corrector` | `hkpUnaryAction` | `0x007588e0`, `0x00758970` |
| `object_collector`, `contact_collector`, `havok_closest/all_ray_hit_collector`, `havok_closest/all_linear_cast_collector` | `hkpCdBodyPairCollector`, `hkpCdPointCollector`, `hkpRayHitCollector` | `0x00755730`…`0x0075bfb0` |
| `constraint_instance_breakable_listener` | `hkpConstraintListener` | `0x00776740`, `0x00777fe0` |
| `CriticalAllocator`, `havok_memory_watchdog` | `hkMemoryAllocator`, `hkpDefaultWorldMemoryWatchDog` | `0x0077a0f0`, `0x0077a110`, `0x00779d70` |
| `hk_suspend_inactive_agents_util`, `hk_broadphase_border` | Havok utility bases | `0x007557a0`, `0x0079e540`, `0x0075aad0`, `0x00755ba0` |
| `hk_rigid_body_in_water_listener`, `hk_phantom_in_water_listener` | `hkpEntityListener`, `hkpPhantomListener` | `0x00796940`, `0x00796990` |
| `hk_character_shape_phantom`, `hk_custom_character_proxy`, `MyCharacterListener`, `havok_custom_character_collector`, `character_controller` | `hkpCachingShapePhantom`, `hkpCharacterProxy`, `hkpCharacterProxyListener`, `hkpAllCdPointCollector` | `0x0079a670`…`0x007a45f0` (17 methods) |
| `hk_custom_vehicle_instance` | `hkpVehicleInstance` | `0x00ae8470`, `0x00ae9b40` (20), `0x00ae8ca0` (168), `0x00ae84b0` (517), `0x00ae5c80`, `0x00ae4ab0`, `0x00ab8a60` |
| `hk_custom_motorcycle_instance` / `_aircraft_instance` / `_watercraft_instance` | via the above | `0x00aba2d0`, `0x00ae2800` (308), `0x00ae1170` / `0x00ade080`, `0x00ae0b10` (103), `0x00adabf0` (81), `0x00adab40` / `0x00aeeb30` |
| `hk_custom_vehicle_engine` | `hkpVehicleDefaultEngine` | `0x00ae31a0`, `0x00ae31f0`, `0x00ae3330` (293) |
| `hk_custom_vehicle_steering` | `hkpVehicleDefaultSteering` | `0x00aeb0c0`, `0x00aeb230` (142), `0x00aeb550` |
| `hk_custom_vehicle_transmission` | `hkpVehicleDefaultTransmission` | `0x00aecd60`, `0x00aece20`, `0x00aecef0` (199) |
| `hk_custom_vehicle_brakes` | `hkpVehicleDefaultBrake` | `0x00ae2c80` (293) |
| `hk_custom_vehicle_suspension` | `hkpVehicleDefaultSuspension` | **`0x00aebe30` (992)** |
| `hk_custom_vehicle_driver_input` | `hkpVehicleDefaultAnalogDriverInput` | `0x00ae30c0` |
| `hk_custom_vehicle_raycast_wheel_collide`, `havok_wheel_closest_linear_cast_collector` | `hkpVehicleRayCastWheelCollide`, `hkpClosestCdPointCollector` | `0x00ae9f10`, `0x00aea040`, `0x00aea160`, `0x00aeadb0` (192); `0x00aea480`, `0x00aeab50` |
| `hk_custom_vehicle_extra_downforce` | `hkpUnaryAction` | `0x00ae3770`, `0x00ae3850` (291), `0x00ae3cb0` |
| `man_cannon_vacuum_collector` | `hkpCdBodyPairCollector` | `0x00ab1d60` |

### 3.3 The call-in side — game code calling Havok's API (CONFIRMED census)

Across ranges A+B+C, excluding trivial ≤3-instruction functions (identical-code folding shares those program-wide):
- **326 distinct Havok functions** are called from outside Havok;
- they are called by **1,043 outside functions** at **1,940 call sites**;
- about 440 of those callers are one-call static class-registration initialisers at `0x01000000`–`0x0101ffff` (boilerplate, all into `0x00be*`/`0x00c2*` registration helpers);
- the rest are mostly in the two clusters, plus a thin scatter across gameplay.

**Where the called functions sit:**
- 267 in range A: Base refcount/allocation `0x00be*`, world/entity `0x00c1*`, actions/constraints `0x00c2*`–`0x00c3*`, vehicle `0x00c5*`, utilities `0x00c8*`;
- 53 in range B: shape constructors and queries;
- 6 in range C. **HIGH:** geometry processing is used almost only internally.

**Most-called entry points** (distinct callers, sites):

| Entry point | Size | Callers | Sites | Reading |
|---|---|---|---|---|
| `0x00be6620` | 7 insns | 164 | 250 | `hkContainerHeapAllocator` slot 4 (CONFIRMED by vtable); `hkArray` growth inlined into game code |
| `0x00be6100` | 73 | 75 | 151 | HIGH: reference release |
| `0x00c150c0` / `0x00c14790` | 35 / 28 | 84 / 48 | 129 / 64 | HIGH: world lock/unlock pair, also called around the vehicle step |
| `0x00be7010` / `0x00be6f90` | 59 / 54 | 30 / 20 | 47 / 43 | HIGH: Havok memory |
| `0x00c21b20` | 38 | 19 | 29 | — |
| `0x00be9c80` | 26 | 18 | 26 | — |
| `0x00c177b0` | 78 | 17 | 21 | HIGH: world remove-entity style; called twice in shutdown |
| `0x00d4d7f0` | 40 | 10 | 10 | `hkpBoxShape` constructor (installs its vtable) |

### 3.4 What data crosses the boundary (HIGH unless noted)

| Direction | What | Where seen |
|---|---|---|
| game → Havok | world construction info (gravity, broadphase extents, solver, collision tolerance, multithreaded simulation type — the fields are HYPOTHESIS) | `0x0075f370` → `0x00c1c4f0` |
| game → Havok | Δt per step, the job queue | `0x00755c80` → `0x00c17920` / `0x00c16fc0` |
| game → Havok | rigid-body construction info (shape, mass, material, motion type, collision-filter info) | world users via `0x0161d378`; shape constructors in range B (`0x00d4d7f0` box; MOPP via `0x00d56010` for `.clmesh_pc`) |
| game → Havok | forces, impulses, velocities, keyframes; vehicle driver input (analog status) | actions (`havok_const_accl_action`, extra downforce), `hk_custom_vehicle_driver_input` |
| Havok → game | step info, entity pointers, through action `applyAction` | vehicle/action overrides |
| Havok → game | collidable pairs and shape keys for filtering; return enable/disable | `havok_game_filter` |
| Havok → game | contact point events (position, normal, separating velocity, bodies) | `custom_collision_listener` |
| Havok → game | ray/cast hit records (fraction, normal, collidable) | the collector classes |
| Havok → game | per-wheel contact and suspension state, RPM, torque, friction status | vehicle component overrides and the custom step |
| Havok → game | body transforms and velocities | 98 world-user functions. HYPOTHESIS: largely inline field reads, invisible to a call census |

### 3.5 Game code versus middleware — the split the owner asked for

| Layer | Size | RE needed | Notes for a replacement |
|---|---|---|---|
| Havok internals (A+B+C) | ~377k insns | **None** (out of clean-room scope) | Replaced wholesale by whatever engine is chosen |
| Call-site contract: lifecycle, step, shape construction, world users, queries | the 326 entry points used; ~1,500 game call sites | **Contract only:** arguments in, results out, call order | Fits any rigid-body engine. The step's two-phase multithreaded shape and Havok's inline field reads are the awkward parts. |
| Callback contracts: filter, contact/constraint/entity/phantom listeners, collectors, actions | ~40 game methods (§3.2) | **Yes, small:** the game side is real logic (layer rules, contact → damage) | Any engine with contact callbacks and filter hooks |
| Character controller over `hkpCharacterProxy` | ~11k range, ~2.6k closure | **Yes** | A different engine's character controller will behave differently. The proxy's behaviour is Havok's, so its contract must be specified. |
| **Vehicle physics** | **~21k closure, about 45 overrides** | **Yes. This is the real work.** | The custom step calls ~13 of Havok's *internal* vehicle sub-steps (`0x00c55f80`…`0x00c58770`, ~0.8k instructions together), and the overrides call base implementations (~8.6k instructions of Havok vehicle module). **A non-Havok replacement must re-create the behaviour of those Havok sub-steps (wheel ray-cast/suspension integration, friction solve, the default engine/transmission curves) as well as the game's overrides.** The Havok part can be documented only by contract, not by reading its internals. So reproducing handling feel means either a licensed modern Havok (HYPOTHESIS, not checked: current Havok releases still carry a descendant of this vehicle kit) or tuning a new model against recordings of the original. |
| Ragdoll, buoyancy | ~25k range (shared with memory), ~7.7k water closure | Yes, medium | Ragdoll uses Havok constraints (`hkpRagdollConstraintData` etc.); buoyancy is game-side force logic over phantoms |

**Rule of thumb:** by size, about 84 % of "physics" is middleware (377k of ~450k physics-facing instructions). By RE effort, almost all of the work is in the ~70k game-side instructions, and **vehicle handling is where the game is coupled to Havok's internals rather than its public API.**

---

## 4. Specified versus missing

### 4.1 Already specified or identified

**`spec-physics-format.md`:**
- the Havok identification (§1–§2);
- the `.clmesh_pc` collision-mesh container walked 100,384/100,384 (§4);
- the Havok MOPP handoff to HIGH (§4.4.6(g)).
- The §3 boundary pair (`0x00bf81a0`, `0x00c1c4f0`) is NEEDS-EXE; see §6 for what this pass found.

**`spec-vehicle-data.md`:**
- the `_veh.xtbl` schema (§4–§5);
- the runtime vehicle-info table, entry layout, `Vehicle_Type` union and flag words (§7);
- validated on 123 base-game vehicles (§7.11).
- This is the **input data** of the vehicle model, not the model.

**`spec-vehicle-geometry.md`:** vehicle mesh and part data. It already probes `0x00ab0a10` (part → box shape). It did not recognise `0x00d4d7f0`, `0x00c8a890`, `0x00c1f540`, `0x00c960c0` or `0x00c81a90` as Havok; see §6.

**`spec-lua-api-behaviour.md`:** the Lua control surface —
- the vehicle-instance resolver `0x00a281e0`;
- vehicle force-flag bits (e.g. at vehicle `+0x1d7a`);
- `airplane_takeoff_do` / `airplane_land_do`;
- many `0x00a7xxxx`–`0x00abxxxx` vehicle bindings;
- the helicopter AI hook.

None of these covers simulation.

### 4.2 Missing, and whether it needs RE

| Gap | Needs RE? | Notes |
|---|---|---|
| Physics lifecycle: world construction values, memory setup, listener and filter installation | **Yes, small** | `0x0075f370` (~0.3k own) |
| Step contract: fixed step, sub-steps, critical section, pre/post order, single-step debug | **Yes, small** | `0x0075ac20`, `0x00755c80` |
| Body creation per game-object type (shape choice, mass, material, filter info) | **Yes, medium, diffuse** | 98 world users; `havok_rigid_body` |
| Collision layer/filter rules | **Yes, small** | `havok_game_filter` (~10 methods) |
| Contact → gameplay (damage, effects, sounds) | **Yes, medium** | `custom_collision_listener` → 32 gameplay functions |
| Query wrappers | **Yes, trivial** | collectors (~0.7k) |
| **Vehicle model:** custom step, component overrides, downforce, per-class instances, the `_veh.xtbl` → component parameter mapping | **Yes, large (~21k)** | §1.5, §3.2 |
| Contract for the ~35 Havok vehicle-module functions the game calls (`0x00c55000`–`0x00c5d000`) | **Contract only** | inputs and outputs per call. Internals are out of scope. |
| Character controller | **Yes, medium** | `0x0079a670`…`0x007a45f0` |
| Ragdoll | **Yes, medium** | `0x0077a000`–`0x00792000` |
| Water / buoyancy | **Yes, medium** | `0x00792000`–`0x0079b000` |
| Vehicle damage and deformation (gameplay plus visual) | **Partly** | `0x00a90000` range, `vehicle_deformable_mesh_instance`; not simulation |
| Cloth (`cloth_simulation.cpp`, `.sim_pc`) | **Out of this scope** | `0x00740000` range; game-owned, not Havok-facing (1 call into Havok). Its own topic. |
| Havok internals | **No** | out of clean-room scope by standing rule |

---

## 5. Proposed order of specification

Dependency first, in the spirit of `scoping-ui-render.md` §4.

1. **P0 — Boundary register** (contract only, small).
   - Contents: the three Havok ranges; the RTTI table of §3.2 (every game class over a Havok interface, with slots); the list of the 326 called entry points with caller counts; a short Havok-call-site data table.
   - *Why first:* every later unit cites it, and it fixes the clean-room line in writing, so no later pass wanders into Havok internals.
   - It also settles `spec-physics-format.md` §3's NEEDS-EXE.
2. **P1 — Lifecycle and step** (`0x0075f370`, `0x007590d0`, `0x0075ac20`, `0x00755c80`; small).
   - *Why here:* it defines the frame contract any engine must fit, including the 1/30 s fixed step and the game-scheduler job fan-out.
3. **P2 — Bodies, shapes and filter** (body creation per object type; shape sources: `.clmesh_pc`, already done, vehicle-part boxes, character phantom; `havok_game_filter` layer rules; medium).
   - *Why here:* nothing collides correctly without it. It reuses the `.clmesh_pc` work directly.
4. **P3 — Contacts, constraints and queries** (`custom_collision_listener`, breakable constraints, collectors; small to medium).
   - *Why here:* it is the feedback path into gameplay (damage, effects). Validation: contact-driven damage events.
5. **P4 — Vehicle model** (the largest unit, ~21k; probably its own spec). Sub-order:
   - (a) map `_veh.xtbl` fields into component parameters (bridges the already-specified table);
   - (b) the per-component overrides: engine, transmission, steering, brakes, then suspension at 992 instructions;
   - (c) wheel collide and the wheel collector;
   - (d) the custom step `0x00ae98f0` and its call-by-call contract with the ~13 Havok vehicle sub-steps;
   - (e) the extra downforce action;
   - (f) motorcycle, aircraft and watercraft instances.
   - *Why after P1–P3:* vehicles are bodies in the world, stepped by the frame, colliding through the filter.
   - **Decision point for the owner before (d):** whether the target engine is a licensed modern Havok (the contract maps closely) or a different engine. For a different engine, (d) also needs a behavioural description of the Havok sub-steps, from their observable inputs and outputs, to stay clean-room.
6. **P5 — Character controller** (medium). Players and NPCs walking.
7. **P6 — Ragdoll and buoyancy** (medium each). Not needed for a first drivable build.

**Recommended to park:** cloth simulation (a separate game-owned system); vehicle visual deformation (render-side); the unattributed `0x00d1d800`–`0x00d4c600` range.

**Rough weight against other candidates.** The game-side work (~70k) is about **7 times the UI render core (~10k)**, but it splits into well-defined units at a clean vtable seam. P0–P3 together are probably no larger than the UI core.

---

## 6. Notes for whoever takes it up

- **`spec-physics-format.md` §3 — two adjustments to raise for review.** This scoping note does not edit the spec.
  - (a) `0x00bf81a0` (the "Havok version / Total Times / Per Frame Time" report) sits **inside Havok range A**. Its strings sit among Havok's own `.rdata` assert paths (`0x01193aa8`–`0x01194b9c`). The other users of the version string (`0x00c96ad0`, `0x00c98240`) are also Havok. So it is **HIGH: Havok's own monitor/report code**, not Volition instrumentation as §3 states.
  - (b) `0x00c1c4f0` is confirmed as the only user of the `hkpWorld.cpp` string (two sites), and is called from the game's world-creation wrapper `0x007572f0`. **HIGH: the world constructor.**
- **`spec-vehicle-geometry.md` — one OPEN item is answered by RTTI, to raise for review.** The vtable `0x01243174` installed by `0x00d4d7f0` belongs to **`hkpBoxShape`**, so steps 3–4 of the `0x00ab0a10` probe construct a Havok box collision shape for the part. `0x00c8a890`, `0x00c1f540`, `0x00c960c0` and `0x00c81a90` (also cited there) all sit inside Havok range A.
- **OpenSSL inside the apparent Havok block** (`0x00c98e00`–`0x00d1d800`, ~158k instructions). Any future "Havok size" figure must exclude it.
- **Tooling.**
  - RTTI walking works across the whole image: 1,701 vtables, with base-class lists from the class-hierarchy descriptors. It is the fastest way to find the game classes that implement Havok interfaces.
  - Batch-file argument splitting breaks on `,`, `=` and `%`. Use `+` as a list separator, and avoid `%` in `str` searches.
  - Identical-code folding puts tiny shared stubs (e.g. `0x00c0dea0`) inside Havok's range, called from all over the game. Filter functions of ≤3 instructions out of any call census.
- **Out of bounds:** nothing here touches `.czn_pc` zone data or named-object resolution.
- Every address and label above is from this scoping pass only. A spec pass must re-derive each claim from the executable before recording it.

## 7. Clean-room check

- Addresses are plain hex throughout. There are no Ghidra auto-names for functions, globals or labels, no decompiler variable names, and no pasted pseudocode.
- Class names are RTTI type-descriptor strings read from the binary (`havok_game_filter`, `hk_custom_vehicle_suspension`, …) or Havok's public class names (`hkpWorld`, `hkpVehicleInstance`, …), quoted as data.
- Volition source-file names are quoted from shipped assert/diagnostic strings, with the build-machine prefix dropped where a path is long.
- The Havok keycode string is identified but not reproduced.
- Self-check on the finished file with the project's standard pattern (`iVar…|…|FUN_…|DAT_…`): **0 hits**. It was run twice, with ripgrep and with Python `re`; a known-positive control string matched, so the zero is real.
- No spec file was edited. The private Ghidra copy `tools\gp_physscope` was deleted after the dumps.

---

## 8. Scoping the now-authorized Havok-internals RE work (added 2026-10-07)

**Status: scoping only, same as the rest of this file. Nothing below is cleared for implementation.**

**Why this section exists.** The user directly authorized full RE of Havok's own internals this
session — previously the project's standing rule was call-boundary-only (§0–§7 above, and
`spec-physics-runtime.md`'s P0/P1 units, are all boundary/lifecycle work under that older rule).
This section proposes how to split the newly-authorized work into U0–U4-sized sub-units
(roughly 0.6k–5k instructions each, this project's usual scale), in the user's own stated order:
rigid-body step + character proxy, then `hkpVehicle` components, then collision queries. It
builds on `D:\Project Crreish\TEAM A\spec-physics-runtime.md` §1–§2 (the three Havok ranges, refined
to the byte; the 39-class / 72-slot RTTI callback register) and on
`D:\Project Crreish\TEAM A\scratchpad\middleware_census_2026-10-07.md` (the whole-binary middleware
census that authorized and ordered this workstream). **Same FORBIDDEN-sources list as the rest of
this project applies without exception: no leaked Saints Row IV/Re-Elected source, no leaked Havok
source (`sigmaco/havok-v6.6.0r1-b2009.07.04` or its DeepWiki pages), no other leaked
Volition/Havok/Audiokinetic material. Everything below is read from the shipped executable only.**

**Method (this pass).** Read-only Ghidra headless `func` dumps (`CrreishDump.java`, `-readOnly
-noanalysis`, `nodecomp:1` — no decompiler output was generated or read) against a private
throwaway copy of the shared project, `tools\gp_havokscope` (copied from `tools\ghidra_projects`,
**deleted after this pass**). Two batches, depth 1 and depth 2, over hand-picked root addresses
(the world-step callees already named in `spec-physics-runtime.md` §4.4, the character-proxy Havok
calls already named in `spec-player-runtime.md` §5.6.4–§5.6.5, the 13 vehicle sub-step addresses
already named in this file's own §1.5, and the collector/query wrapper addresses already named in
§1.4/§2 and `spec-physics-runtime.md` §2.1). Every call edge below is **CONFIRMED** (read directly
from Ghidra's reference manager over the existing, already-analyzed project — no new disassembly
pass was run). **Instruction counts for functions not already sized in an existing spec are
estimated from the function's byte range**, calibrated against four pairs already CONFIRMED by
instruction count elsewhere in this project (`0x00aebe30` 992 insn / 3886 B; `0x00ae7470` 343 / 1214;
`0x00ae9570` 227 / 786; `0x00ac3f40` 98 / 361 — average **3.6 bytes/instruction**, range 3.46–3.92).
These estimates are labelled **HIGH** below, not CONFIRMED, and are good to roughly ±15%, consistent
with this project's usual ±10% disclaimer for size figures. Addresses are plain hex; no Ghidra
auto-names or decompiler variable names appear below (§8.6 repeats the self-check).

### 8.1 Walking slice — rigid-body step + character proxy

**Already-named roots used** (per the task brief, all CONFIRMED addresses from
`spec-physics-runtime.md` §4.4 and this file's §1): the world step's three Havok-side callees
`0x00c17920` (`hkpWorld::initMtStep`, HIGH), `0x00be88c0` (`hkJobQueue::processAllJobs`, HIGH) and
`0x00c16fc0` (`hkpWorld::finishMtStep`, HIGH); plus the character-proxy/character-controller
call-in points from the 72-slot register (`spec-physics-runtime.md` §2.1 rows 21–25) and the Havok
calls `spec-player-runtime.md` §5.6.4–§5.6.5 already named inside the custom proxy's own
cast-and-solve machinery (`0x0079b000`–`0x0079e000`): `0x00c30150` (set phantom AABB), `0x00c835b0`/
`0x00c83570`/`0x00c835c0` (proxy velocity/position get/set), `0x00c838d0` (broadphase commit).

**Finding 1 (CONFIRMED by listing): the three world-step entry points are thin dispatchers, not the
dynamics code itself.**

| Root | Body range | Size (HIGH est.) | Immediate Havok-internal callees |
|---|---|---|---|
| `0x00c17920` initMtStep | `00c17920`–`00c17958` | ~16 insn | `0x00c164e0`→`0x00be5fe0`; `0x00c3fda0`→`0x00c18450`/`0x00bec640`/`0x00bfe5c0`; `0x00c164f0`→`0x00be6030` |
| `0x00be88c0` processAllJobs | `00be88c0`–`00be890a` | ~21 insn | `0x00be88a0`→`0x00be8650` |
| `0x00c16fc0` finishMtStep | `00c16fc0`–`00c17023` | ~28 insn | `0x00c165d0`, `0x00c165b0` (both leaves) |

Two hops down, every one of `0x00c3fda0`'s own callees (`0x00c18450`, `0x00bec640`, `0x00bfe5c0`)
is itself a 4–9-byte leaf (CONFIRMED by listing) — essentially flag tests, not physics. **The one
real workhorse found this pass is `0x00be8650`** (called from `processAllJobs` via `0x00be88a0`):
317 bytes/**~88 insn** own body, with its own further callees (`0x00be7bd0`, `0x00be7d80`,
`0x00be8bf0`, and a substantial `0x00be8d70` at 589 B/**~164 insn** with six more callees of its
own) — shape and naming (queue entries, `memcpy`, paired tiny accessors) are consistent with
**Havok's job-queue locking/pop machinery**, not the solver or integrator. Summed over the two hops
actually walked, this cluster is on the order of **300–400 instructions (HIGH)**.

**Correction/addition to this pass's own expectation, not to an existing spec figure: the actual
per-job dynamics work (broadphase update, narrowphase, solve, integrate) is NOT reachable by static
call tracing from these three roots at all.** Havok's multithreaded job queue hands out opaque job
descriptors that get dispatched to job-type-specific code, almost certainly through a function-
pointer table set up at bring-up (the registration helpers `spec-physics-runtime.md` §4.1 already
lists at steps 1/4/10, none of them opened yet) rather than through a fixed call edge Ghidra's
reference manager can see. **So the first real task of this slice is not a deeper closure dump from
`0x00c17920`/`0x00be88c0`/`0x00c16fc0` — it is finding that job-type dispatch table.** This is an
honest scope gap, flagged here rather than papered over with a guessed size.

**Finding 2 (CONFIRMED by listing): the character-proxy Havok primitives are real and small, and
two of them are shared with the collision-query slice (§8.3).**

| Root | Body range | Size (HIGH est.) | Role | Shared with |
|---|---|---|---|---|
| `0x00c835b0` | `00c835b0`–`00c835b3` | ~1 insn | get proxy velocity | — |
| `0x00c83570` | `00c83570`–`00c83579` | ~2 insn | get proxy position | — |
| `0x00c835c0` | `00c835c0`–`00c835d5` | ~6 insn | set proxy linear velocity | — |
| `0x00c1e730` | `00c1e730`–`00c1e7ad` | ~35 insn | update a phantom's AABB (calls `0x00c30150`); **10 distinct callers** incl. the overlap-query wrappers `0x0075a480`/`0x007581b0`/`0x00757ec0` | §8.3 |
| `0x00c30150` | `00c30150`–`00c30485` | ~228 insn | the AABB setter itself; calls into range B (`0x00d58180` ~234 insn, `0x00d58140` ~16, plus 9 smaller) | §8.3 |
| `0x00c838d0` | `00c838d0`–`00c83d5a` | ~315 insn | broadphase commit after a cast-and-solve pass; calls `0x00c83720`, `0x00c83760`, `0x00c150c0` (a per-body method, HIGH), and `0x00be9ed0` | — |

`0x00c30150`'s own two-hop closure (root + the 13 callees named in the dump) comes to roughly
**600 instructions (HIGH)** on its own — already most of a U0 unit by itself, before the proxy's
own cast/solve internals are even opened.

**Proposed sub-units:**
- **W-U0 — world-step dispatch + job-queue mechanics.** Roots `0x00c17920`, `0x00be88c0`,
  `0x00c16fc0`, plus `0x00be8650`'s cluster. ~0.3–0.5k instructions as traced; **the real deliverable
  of this unit is locating the job-type dispatch table**, not just sizing what's already visible —
  expect this unit to grow once that table is found.
  **UPDATE 2026-10-07: found.** The job-type dispatch table lives at job-queue-object offset
  `+0x128` (a 16-byte row per job type, holding a subtype function-pointer-table pointer + length +
  two more directly-stored function pointers), populated once at bring-up by a
  `registerJobType`-shaped function (`0x00be7c90`) called exactly 4 times from one setup routine
  (`0x00c2b220`, itself called once from the already-known bring-up function `0x0075f370`). Four job
  types are registered (0-3); types 2 and 3 have real, distinct per-subtype collision-detection
  handlers reaching into Havok range B/C, while types 0/1 share one generic admin dispatcher. Full
  write-up: `scratchpad/havok_wu0_job_dispatch_2026-10-07.md`; open items (incl. what the other two
  stored function pointers per row are for, and who enqueues type-2/3 jobs):
  `scratchpad/havok_wu0_open_items_2026-10-07.md`. Not yet integrated into `spec-physics-runtime.md`
  — scratch-only per this pass's brief.
- **W-U1 — character-proxy Havok primitives (AABB/broadphase integration).** Roots `0x00c835b0`,
  `0x00c83570`, `0x00c835c0`, `0x00c1e730`, `0x00c30150`, `0x00c838d0`. **~1.0k instructions (HIGH)**,
  a clean single U0-sized unit. Note the overlap with W-Q0 (§8.3) at `0x00c1e730`/`0x00c30150` —
  document these two functions once, cross-referenced from both units, not twice.
- **W-U2 — the proxy's own cast/solve Havok calls (OPEN, unsized).** `spec-player-runtime.md` §5.6.4
  (OPEN 36) already flagged that the custom proxy's pass functions (`0x0079b000`–`0x0079e000`) call
  into Havok range A "around `0x00c83xxx`" beyond the four primitives in Finding 2 — almost
  certainly `hkpCharacterProxy::integrate`/`checkSupport`/the cast itself. This pass did not locate
  those specific addresses (they were not among the four already-named call sites, and a fresh
  closure search starting from `0x0079dd50` was out of this pass's budget). **HYPOTHESIS: comparable
  in scale to the vehicle module's own (§8.2), i.e. low thousands of instructions** — recommend this
  as the next concrete discovery task for this slice, not a guessed size.
- One loose end for whoever opens this unit: `0x0079dd10`'s own callee `0x0079c4d0` (the shared
  entity/phantom-listener teardown reached from rows 21/22's destructors) calls a not-yet-sized
  Havok function `0x00c84960`, and `0x007a1870`'s chain reaches `0x00c835e0` (unsized, in the same
  cluster as the three primitives above) — both worth folding into W-U1 once sized.

### 8.2 Driving slice — `hkpVehicle` components

Builds directly on the four already-named roots (`0x00ac3f40`, `0x00ae9570`, `0x00ae7470`,
`0x00aebe30`) and the 13 Havok vehicle sub-step addresses this file's own §1.5 already lists
(`0x00c55f80`…`0x00c58770` from `0x00ae9570`; `0x00c565b0`…`0x00c57ca0` from `0x00ae7470`).

**Confirmation, not a correction: the 13 sub-steps' own bodies sum to ~755 instructions (HIGH),
matching this file's existing "~0.8k instructions together" figure (§1.5, §3.5) within this pass's
own estimation tolerance.** Per-function sizes (own body only):

| From `0x00ae9570` | Size (HIGH est.) | From `0x00ae7470` | Size (HIGH est.) |
|---|---|---|---|
| `0x00c55f80` | ~16 insn | `0x00c565b0` | ~55 insn |
| `0x00c56040` | ~24 insn | `0x00c566e0` | ~79 insn |
| `0x00c560a0` | ~8 insn | `0x00c56800` | ~51 insn |
| `0x00c562d0` | ~10 insn | `0x00c57690` | ~15 insn |
| `0x00c58360` | ~69 insn | `0x00c576d0` | ~116 insn |
| `0x00c58770` | ~76 insn | `0x00c57a50` | ~164 insn |
| — | — | `0x00c57ca0` | ~71 insn |
| **subtotal** | **~204 insn** | **subtotal** | **~551 insn** |

**New finding this pass: two of the thirteen are not leaves.** `0x00c565b0` calls `0x00c32520`
(857 B/**~238 insn**, a shared function also called directly from the game's own wheel/suspension
helper `0x00ae6000` and from another Havok vehicle function `0x00c571f0` — HIGH: a contact/friction
description builder, shared rather than vehicle-only). `0x00c57690` calls `0x00c63c70`
(3528 B/**~980 insn**, by far the single biggest Havok-internal function reached from this slice —
also called twice directly from the game's own `0x00ae6bd0`), which itself calls `0x00c63570`
(~39 insn), `0x00c63930` (~227 insn, itself calling CRT floating-point exception plumbing at
`0x00ea3f60` — boilerplate, not vehicle logic) and `0x00c81690` (~151 insn — **this one is general
Havok infrastructure, not vehicle-specific**: it has 11 callers spread across all three Havok
ranges including the product-keycode check `0x00f21bd0`, so it is a shared utility, likely
monitor/report or a hash, not part of the driving slice proper. Recommend capping the driving
slice's closure at `0x00c81690` itself and not following it further).

**Confirmation: "call base, then modify" overrides have thin Havok bases.** The suspension
override's own base call, `0x00aebe30`→`0x00c59630`, is only 200 B/**~56 insn** (calling the shared
utility `0x00c14790`). The brakes (`0x00ae2c80`→`0x00c59e40`) and wheel-collide
(`0x00aea040`→`0x00c5a1e0`) bases were not dumped this pass but almost certainly follow the same
pattern (HIGH, not CONFIRMED).

**Proposed sub-units:**
- **D-U0 — the 13 named sub-steps' own bodies.** All 13 addresses above. **~755 instructions
  (HIGH)**, matching the existing estimate — a single clean U0 unit, and the natural first unit of
  this slice since every root is already named and CONFIRMED.
- **D-U1 — the friction/material extension cluster.** `0x00c32520`, `0x00c63c70`, `0x00c63570`,
  `0x00c63930`, and `0x00c81690` as the outer boundary (do not follow `0x00c81690`'s own callees —
  they are generic Havok infrastructure, see above). **~1.6k instructions (HIGH)** — the single
  meatiest well-defined chunk in this slice, comfortably one U1 unit.
- **D-U2 — the component-override Havok bases.** `0x00c59630` (CONFIRMED ~56 insn) plus
  `0x00c59e40`/`0x00c5a1e0` (HIGH, unsized — size these first). Small, **~0.1–0.3k instructions**;
  could be folded into D-U0 or D-U1 rather than kept as its own unit, depending on what the actual RE
  pass finds once the other two bases are sized.
- Total for this slice as currently bounded: **D-U0 + D-U1 + D-U2 ≈ 2.4–2.7k instructions**, i.e.
  two to three U-sized units rather than one, once the previously-forbidden internals are opened —
  roughly 3× the ~0.8k that was visible under the old boundary-only rule.

### 8.3 Collision queries / raycasts

**Root functions, surveyed from the existing call-in registry.** The census's own framing
("~23 fn/~0.7k insn… not yet pinned to exact addresses") was about the **game-side** collector
closure (`spec-physics-runtime.md` §2.1 rows 12–18/37, already addressed there). This pass instead
traced the **Havok-internal** side reached from that same closure — concretely, from the two
already-named families of game-side query wrappers:
- the two 1-metre query-phantom overlap wrappers `0x00757ec0`/`0x007581b0`/`0x0075a480`
  (`spec-physics-runtime.md` §4.1 step 15; HIGH, three near-identical variants, one per
  phantom/query shape);
- the ray/linear-cast collector overrides `0x0075b800` (closest ray hit), `0x0075b9c0` (all ray
  hits), `0x0075bb50` (closest linear cast), `0x0075bfb0` (all linear casts).

**Overlap-query Havok roots (CONFIRMED edges):** all three wrappers call `0x00c1e730`→`0x00c30150`
(the same phantom-AABB pair as §8.1's W-U1 — **shared root, do not double-count**), plus `0x00c19160`
(HYPOTHESIS "world add-entity" per `spec-physics-runtime.md` §3.1; 462 B/**~128 insn** own body,
calling 7 further small Havok reference-count/utility functions already named in that spec's §3.1
most-used-entry-points table — `0x00be6620`, `0x00be6b90`, etc., cross-validating this pass against
that one) and `0x00d4d3c0` (range B, 86 B/**~24 insn**).

**Ray/linear-cast Havok roots (CONFIRMED edges, and a direct cross-check of an earlier interp
note):** `0x0075b800` calls `0x00d4c800` — **the very first function of Havok range B**
(`spec-physics-runtime.md` §1), 114 B/**~32 insn**, which itself calls `0x00d6bff0` (83 B/**~23
insn**). `0x0075b9c0`/`0x0075bb50`/`0x0075bfb0` reach the same `0x00d6bff0` through `0x00d527d0`
(190 B/**~53 insn**, also calling the already-documented shared Havok-memory utility `0x00be7010`)
or terminate at the leaf `0x00d4d280` (262 B/**~73 insn**, no further calls). `0x00d6bff0`'s two
callers and its own small size are consistent with a **shape-key/sub-shape resolver** — exactly what
a ray or linear-cast hit callback needs to decode which part of a composite shape (e.g. a `.clmesh_pc`
MOPP tree) was actually struck.

**Negative finding: `0x00ab1d60` (`man_cannon_vacuum_collector`) makes zero Havok calls.** Its three
callees (`0x00458230`, `0x00853b10` the liveness check, `0x00ab1a20`) are all game-side. This
confirms it contributes nothing to the Havok-internal count — it is a pure-gameplay collector that
happens to share a Havok base class with `object_collector`, not a real physics query.

**Confirming, not correcting, the census's size estimate.** This pass's own byte-derived sizes for
the four collector overrides it dumped (`0x0075b800` ~45 insn, `0x0075b9c0` ~61 insn, `0x0075bb50`
~41 insn, `0x0075bfb0` ~9 insn) land within this pass's own ±15% estimation band of the precise
per-function sizes `spec-physics-runtime.md` §2.1 already gives for the same addresses (`[54]`,
`[77]`/`[19]`, `[39]`, `[19]`) — the gap is the estimation method, not a real discrepancy. **No
correction to the ~23 fn/~0.7k game-side figure is proposed.** What this pass adds is the
previously-missing Havok-internal root list, which the census explicitly said had not been pinned
down yet.

**Proposed sub-unit:**
- **Q-U0 — collision-query Havok-internal roots.** `0x00c1e730`/`0x00c30150` (shared with W-U1, cite
  rather than re-document), `0x00c19160`, `0x00d4d3c0`, `0x00d4c800`, `0x00d6bff0`, `0x00d527d0`,
  `0x00d4d280`. Excluding the shared pair already counted under W-U1, the **net new Havok-internal
  size is small, on the order of 0.2–0.4k instructions (HIGH)** — a single, comfortably-sized unit,
  confirming the census's own framing of this as the thinnest, cheapest-to-close of the three
  slices. `0x00ab1d60` is explicitly excluded (see negative finding above).

### 8.4 Totals and ordering

| Slice | Proposed units | Combined size (HIGH est.) |
|---|---|---|
| Walking (rigid-body step + character proxy) | W-U0 (job dispatch, size TBD pending the dispatch-table find), W-U1 (~1.0k), W-U2 (OPEN, unsized) | **≥1.3k, likely several k once W-U0/W-U2 are opened** |
| Driving (`hkpVehicle` components) | D-U0 (~0.75k), D-U1 (~1.6k), D-U2 (~0.1–0.3k) | **~2.4–2.7k** |
| Collision queries | Q-U0 (~0.2–0.4k net new) | **~0.2–0.4k** |

**D-U2 RESOLVED 2026-10-08:** suspension/wheel-collide bases re-confirmed (71/51 insn exact, no behavioral change); brakes' base (`0x00c59e40`, 159 insn, 0 callees — refutes this file's own "almost certainly calls `0x00c14790`" guess) fully characterized. See `spec-physics-runtime.md` §10.4-§10.7/§11 and `scratchpad/havok_du2_wwise_uw4b_2026-10-08.md`.

Recommended order within the authorized workstream matches the user's own ordering and this
project's usual dependency-first rule: **W-U1 (character-proxy primitives) before W-U0/W-U2** (W-U1
is fully rooted and CONFIRMED-by-edge today; W-U0 and W-U2 both need a discovery step before they
can be sized properly), then **D-U0 before D-U1/D-U2** (same reasoning — D-U0's roots are already
exact, D-U1/D-U2 need one more hop of real tracing but are already well-bounded), then **Q-U0**
last, since it is the smallest and most self-contained, and partly reuses W-U1's own roots.

### 8.5 Corrections to this file's own existing figures

**None required.** This pass's independently-derived sizes for the 13 named vehicle sub-steps
(~755 insn total) and the four named collector overrides confirm §1.5/§3.1/§3.5's existing figures
within this pass's own estimation tolerance; nothing here contradicts a number already in §0–§7.
Two small, non-contradicting additions for whoever takes this up next:
1. `spec-physics-runtime.md` §3.1 already notes "474 direct calls from Havok to 63 functions
   outside the ranges… None of the other 41 is Volition gameplay logic (HIGH)" without naming them
   individually. This pass can now name two concrete instances: `0x00ae4870` (called from the
   vehicle sub-step `0x00c565b0`) and `0x005247f0` (called from the character-proxy broadphase
   commit `0x00c838d0`) — both small (≤30 insn, HIGH est.) and both plausible further examples of
   the same identical-code-folding/header-inline pattern `spec-physics-runtime.md` §1 already
   documents for `hkSplineParametricCurve` (compiled Havok code physically sitting inside a game
   address cluster rather than one of the three canonical ranges).
2. `D:\Project Crreish\TEAM A\scratchpad\middleware_census_2026-10-07.md` §15 item 2 cites the
   character-controller "~11k range/~2.6k closure" figure to `spec-physics-runtime.md` §2 — that
   figure is actually this file's own §2 table, not `spec-physics-runtime.md`'s. Harmless, but worth
   fixing at the source next time that census is touched.

### 8.6 Clean-room check (this addendum)

- Addresses are plain hex throughout; no Ghidra auto-names, decompiler variable names or pasted
  pseudocode anywhere above. `FUN_`/`DAT_` is not used in this addendum (stricter than this file's
  own §0–§7 convention, which already allows it; this section didn't need it).
- Every call edge is read directly from the existing project's reference manager (CONFIRMED); every
  instruction count not already published elsewhere in this project is a HIGH-confidence estimate
  from the function's byte range, calibrated against four already-CONFIRMED pairs (see Method
  above) and labelled HIGH throughout, never presented as CONFIRMED.
- No Havok source — leaked or otherwise — was read. No DeepWiki page was read. The only inputs were
  this project's own prior specs/scoping notes and this pass's own read-only Ghidra dumps against
  the private throwaway copy `tools\gp_havokscope`, which has been deleted.
- Self-check with the project's standard pattern (`iVar…|…|FUN_…|DAT_…`) over this addendum alone:
  0 hits.
- No other spec file was edited by this pass. `spec-physics-runtime.md` and
  `D:\Project Crreish\TEAM A\spec-player-runtime.md` were read and cited, not modified.

### 8.7 Q-U0 taken up — direct disassembly corrections, and the AI-action connection (added 2026-10-07, later pass)

**Method.** Ghidra-free this time: a plain `capstone` (x86, 32-bit) linear-sweep disassembler against
the shipped exe's raw bytes (`tools/harnesses/exe_va.py`'s PE/section-mapping logic, no project, no
decompiler). `0x00c1e730`/`0x00c30150` were given only a light confirmation pass (shared with the
parallel W-U1 unit; not re-documented in depth here). Every instruction count below is an exact count
from real disassembly, not a byte-range estimate — it **supersedes** this file's own §8.3 HIGH figures
for the same six addresses, all of which turn out to have been directional-correct but numerically a
bit low:

| Root | §8.3's HIGH estimate | Actual (CONFIRMED by disassembly) | Calls |
|---|---|---|---|
| `0x00c19160` | 462 B / ~128 insn | **463 B / 143 insn** (one function; real extent `0x00c19160`-`0x00c1932e`, `int3` padding at `0x00c1932f` then a distinct twin function starts at `0x00c19330` — not the same unit) | `0x00c164c0`, `0x00be6960`, `0x00c161c0`, `0x00c16420`, `0x00c16440`, `0x00be6b90`, plus 2 vtable calls and 2 calls through the CRT TLS thunk `[0x00101c0c8]` |
| `0x00d4d3c0` | ~24 insn | **32 insn**, 0 calls (pure FPU leaf) | — |
| `0x00d4c800` | ~32 insn | **49 insn**, 1 call | `0x00d6bff0` |
| `0x00d6bff0` | ~23 insn | **36 insn**, 0 calls | — |
| `0x00d527d0` | ~53 insn | **66 insn**, 2 calls | `0x00be7010`, `0x00d6bff0` |
| `0x00d4d280` | ~73 insn | **100 insn**, 0 calls | — |

**Behavioural findings, all CONFIRMED by listing (no decompiler used):**

- **`0x00d6bff0` is a shape-key/sub-shape chain resolver, confirmed, not just HYPOTHESISED by shape.**
  cdecl, 3 dword args (by call-site push order: an output array pointer, a max depth — always `8` at
  both call sites below — and a chain-head pointer). It walks the chain head's own `+0xc`-linked
  "parent" pointers up to the max depth, collects each visited node's `+4` field into the output array
  **innermost-first**, appends a `0xffffffff` sentinel, and returns the depth actually walked. This is
  exactly the shape this file's §8.3 already predicted ("what a ray or linear-cast hit callback needs
  to decode which part of a composite shape was actually struck") — raised here from HYPOTHESIS-by-size
  to CONFIRMED-by-behaviour. Havok method name: still not pinned down (HYPOTHESIS only); no RTTI string
  or vtable slot was found for it, consistent with it being a free internal helper rather than a virtual
  method.
- **`0x00d4c800`, the first function of Havok range B, is a "replace the cached hit if this candidate is
  closer" updater — and it is called directly by the already-RTTI-identified collector override
  `0x0075b800` (`havok_closest_ray_hit_collector` / `hkpClosestRayHitCollector`, `spec-physics-runtime.md`
  §2.1 row 14).** Disassembly of `0x0075b800` itself (41 insn, not previously read instruction-by-
  instruction in this file) shows the shape plainly: it calls a game-side filter `0x0075b780` first,
  and only on success calls `0x00d4c800(this, candidate_body_ptr, candidate_hit_record_ptr)`, which
  compares the candidate record's fraction (`+0x10`) against `this`'s cached best (`+0x20`); if closer,
  it copies the candidate's normal/position/fraction/extra fields into `this`'s own hit-record slots,
  resolves `candidate_body_ptr`'s shape-key chain via `0x00d6bff0` (depth 8) into `this+0x30`, and caches
  a terminal body pointer (found by its own short `+0xc`-chain walk) into `this+0x60`. **This is the
  missing link between the game's own `hkpClosestRayHitCollector` override and Havok range B** that
  §8.3 flagged only as a call edge without reading either side.
- **`0x00d527d0` is the shared "append a hit" implementation for the all-ray / all-linear-cast / closest-
  linear-cast collector family** (it is reached from `0x0075b9c0`, `0x0075bb50` and `0x0075bfb0` alike,
  per §8.3's own edge list). Disassembly shows a textbook `hkArray`-shaped growable buffer at `this+0x10`
  (data ptr) / `this+0x14` (size) / `this+0x18` (capacity, top 2 bits reserved for flags) with a 0x60-byte
  element stride; on overflow it calls the already-documented shared growth utility `0x00be7010` (passing
  the global allocator-singleton pointer `0x01312854`), then appends a new element, copies the same
  4-field hit record shape `0x00d4c800` uses, and resolves the shape-key chain via `0x00d6bff0` (depth 8
  again) into the new element. **Open question, flagged rather than forced:** this function is reached
  from `0x0075bb50` (`havok_closest_linear_cast_collector`, a "closest only" class by its own RTTI name)
  as well as from the two "all hits" wrappers, yet its own shape is pure append-to-growing-array with no
  visible "closest" filtering. Either the closest/all distinction for linear casts is enforced one layer
  up (inside the `0x0075bXXX` wrappers themselves, not traced this pass) or at the Havok-internal level
  "closest" linear-cast collectors are implemented as an always-size-1 array and the override does the
  trimming — not resolved here, left HIGH not CONFIRMED.
- **`0x00d4d280` resolves a *pair* of shape-key chains in-line** (manually walking both a `+0x30`-rooted
  and a `+0x34`-rooted chain from its one stack argument, each into its own length-prefixed, `-1`-
  terminated output array inside `this`), rather than delegating to `0x00d6bff0`. A guard skips the two
  chain-walks (but not a shared tail copy of fraction + a position vector) when `this+0x30` is already
  populated and the new candidate's fraction is not better. HIGH: this is the dual-shape-key sibling of
  `0x00d6bff0`, used where a cast result needs both the cast shape's own sub-key and the struck shape's
  sub-key, rather than just one.
- **`0x00d4d3c0` is a pure FPU leaf with no calls**: it copies a caller-supplied vec4 into `this+0x20`,
  then overwrites `this+0x2c` with the component-wise minimum of the copied vector's x/y/z. HIGH: a
  conservative-extent helper (e.g. deriving a scalar bounding radius from a half-extents vector); no
  caller edge to it was independently re-confirmed this pass (grouped here per the task brief and §8.3's
  own framing, not re-derived).
- **`0x00c19160` is better described as a guarded deferred-command dispatcher than simply "add entity."**
  Full disassembly (both branches, 143 insn total, not just the first-reached `ret`) shows: a lock-style
  counter at `this+0x94`; while it reads non-zero, the function takes a 2-stack-arg short path that packs
  those two args plus a literal byte tag (`0x15`) into a small on-stack record and hands it to
  `0x00c164c0(this, &record)` — a deferred-command queue, by shape. While `this+0x94` is zero, it instead
  does the "real" work: bumps `this+0x94` to 1, builds a bigger descriptor, makes a per-thread allocator
  lookup (`[0x0101c0c8]`, an IAT thunk) to size-class an allocation (`(count*8+0x7f) & ~0x7f`, an 8-byte-
  per-entry request), calls two vtable methods and the already-documented utilities `0x00be6960`/
  `0x00c161c0`/`0x00c16420`/`0x00c16440`, decrements `this+0x94` back toward 0, and finally frees/releases
  through another vtable call reached via the same global allocator-singleton pointer `0x01312854` seen
  in `0x00d527d0` above (cross-confirms `0x01312854` as one shared Havok heap-singleton pointer, used by
  both the W-area command-queue machinery and the B-area collector-array growth). An exact twin of the
  short-path shape sits immediately after (`0x00c19330`, tag `0x13` instead of `0x15`) — a different
  command kind sharing the same dispatcher-and-queue pattern, not part of this unit. **This refines, not
  contradicts, §8.3's own HYPOTHESIS** ("world add-entity"): the call shape is consistent with a
  locked/stepping world deferring additions via a tagged command queue rather than applying them
  immediately, which is one plausible reason a "world add" call would need exactly this shape — but the
  literal Havok method name is still not confirmed by any RTTI string, so it stays HYPOTHESIS.

**The AI-action connection (asked for explicitly by this pass's brief) — found, but narrower than a
first guess would suggest.** `spec-ai-runtime.md` §11's own OPEN item 18 flags three untraced "FOV
cone/frustum geometry" primitives, one of which, `0x0075d7e0`, is also the ray helper the perception
system's list-2 "line of fire" probe uses (§10.3/§10.6 in that spec). Disassembling it this pass shows
it is **not** a pure game-side geometry routine — it directly builds and fires a real Havok closest-ray
cast:
1. `0x0075d7e0` calls `0x0075b8b0`, a constructor that writes vtable pointer `0x01153ad8` into a stack
   object and zero/−1-fills a block at `+0x320..+0x18c` — shape-consistent with constructing a
   `havok_closest_ray_hit_collector`-family instance in place (row 14 of `spec-physics-runtime.md` §2.1).
2. It then loads the cached `hkpWorld`-shaped pointer from the fixed global `[0x00161d378]` and calls
   `0x00c163a0` — **inside Havok range A** — passing the freshly-built collector and a ray-input block as
   the two arguments. Shape (`this`-style world pointer + `(input, collector)`) matches public Havok
   2010.2 `hkpWorld::castRay(const hkpWorldRayCastInput&, hkpRayHitCollector&)` well enough to call it
   **HIGH**, not CONFIRMED (no string/RTTI evidence, just argument/role shape). `0x00c163a0` itself is a
   28-instruction thin dispatcher with two indirect (vtable) calls — consistent with "walk the broadphase,
   then ask each candidate's own shape to cast" and consistent with why the scoping pass's static-edge
   census never saw this hop (both calls are indirect, not literal `call 0xADDR`).
3. The collector's own override is `0x0075b800`, already named above as the function that calls
   `0x00d4c800` → `0x00d6bff0`.

**So the chain is: AI perception's line-of-fire probe (`0x0075d7e0`) → `hkpWorld::castRay`-shaped call
(`0x00c163a0`, Havok range A) → the closest-ray-hit collector override (`0x0075b800`) → this unit's own
`0x00d4c800` → `0x00d6bff0`.** This is a real, CONFIRMED-by-edge dependency from the AI runtime onto two
of this unit's roots — but it is the **perception layer** (which feeds the "visible"/"engageable"/
"clear line of fire" flags essentially every action routine reads), not a per-action call. A second,
separate AI-side query, `0x0075d4b0` (the spatial query behind action 51 `brute throw prop`'s prop
search, `spec-ai-runtime.md` §11.3F), calls `0x0075ae80` directly — the `object_collector` override
(`spec-physics-runtime.md` §2.1 row 12) — but that override turned out, on disassembly, to be an
11-instruction boolean latch ("did I hit anything at all") with **no** further calls, so it does **not**
reach this unit's shape-key machinery.

**Negative finding, worth recording since it corrects an assumption in this pass's own brief:** the
brief named actions 7 (`melee vehicle`), 15, 23, 37, 38, 39 and 52 (`brute car flip`) as routines
"waiting on" collision/geometry query results. Direct disassembly of two of them this pass —
**7 (`0x005525c0`) and 52 (`0x00526240`)** — finds **no call into Havok at all**. 52's own "clearance
probe" is pure vector math: a local-frame point transform (`0x00525d30`, itself just calling the
already-named `0x004f4770`) plus one indirect vtable call on the *vehicle game object* (`[[edi]+0x60]`,
not one of this project's 39 Havok-RTTI rows) plus the already-documented clamp helper `0x004bc680`. 7's
melee-range test is the same family (`0x004aac40`/`0x004f4770`/`0x004f2970`, per `spec-ai-runtime.md`
§11.6, confirmed here to have no Havok call hiding behind an early-exit branch either). **These seven
actions' own geometry needs are met entirely by cached vehicle-local-frame math, not by a live Havok
query** — the real Havok dependency for "is T engageable/visible" that gates nearly all 70 action
routines runs through the perception layer's line-of-fire probe described just above, not through the
action routines' own bodies. A port can supply 7/15/23/37/38/39/52's own geometry from the vehicle's
cached local bounding box without touching this unit's roots at all; it does need this unit's roots
(via the perception layer) to answer "can X see/hit Y" for everything else.

**Self-check:** addresses plain hex throughout; no Ghidra, no decompiler, no auto-names. Cleanroom
pattern self-scan (`iVar…|…|FUN_…|DAT_…`) over this subsection alone: 0 hits.
