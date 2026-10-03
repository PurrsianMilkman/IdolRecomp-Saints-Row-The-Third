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
