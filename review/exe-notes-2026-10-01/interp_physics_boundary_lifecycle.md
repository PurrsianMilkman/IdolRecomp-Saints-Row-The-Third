# Interp note — physics P0 (Havok boundary register) and P1 (lifecycle and step)

**Date:** 2026-10-03 · **Units:** items 1 (P0, boundary register) and 2 (P1, lifecycle and step) of `review/scoping-physics-vehicle.md` §5. Investigation only: no spec file was edited. For whoever folds it into a physics spec (and into `spec-physics-format.md` §3, see §4 below).

**Shared conventions:** the same as `review/scoping-physics-vehicle.md` and the other `exe-notes-2026-10-01` interp notes — plain hex addresses, my own words, labels **CONFIRMED** (read directly in this pass) / **HIGH** (inferred from structure) / **HYPOTHESIS** (plausible, not checked); no Ghidra auto-names and no decompiler output. Class names are the binary's own RTTI type-descriptor strings; Havok class and method names are Havok's public 2010.2 API names, quoted as data exactly as the scoping note does. Background (Havok identity, the version string, the pipeline diagram, the vehicle seam) is cited from the scoping note and not repeated.

**Scope kept:** the call contract and the game's own step-driving code only. Nothing here reads Havok's internals beyond confirming where a call crosses into them; the vehicle custom sub-steps and `.czn_pc` were not opened.

**Method.**
- Two private, disposable copies of the Ghidra project (`tools\gp_physA0`, `tools\gp_physA0b`), read-only, `-noanalysis`; both deleted at the end.
- `CrreishDump.java` `func` (the five lifecycle/step functions and their direct callees, depth 1), `xref` (the timing globals).
- One read-only census script of my own (`PhysA0Census.java`, session scratchpad): every defined function's extent and instruction count, every direct call/jump edge, every string reference — over the whole program (41.8k functions).
- Raw-file Python over `SaintsRowTheThird.exe` (`tools/harnesses/exe_va.py` + capstone + pefile): an independent RTTI census (type descriptor → complete-object locator → vtable, with class-hierarchy base lists), a raw `E8`/`E9` byte scan for call sites Ghidra never put inside a function, import-thunk resolution, and static values of globals.
- Range boundaries were placed **to the byte** this time: each one is the first/last instruction of a function whose call-graph neighbours (and, where present, string anchors) change side, checked by disassembly.

---

## Part 1 — the boundary register

### 1.1 Address ranges — CONFIRMED (to the byte)

Refines scoping note §3.1. Function and instruction counts are over Ghidra-defined functions (same ±10 % caveat as the scoping note: undefined starts are missed).

| Region | Start (incl.) | End (excl.) | Functions | Instructions | What it is | How the edges were fixed |
|---|---|---|---|---|---|---|
| (game) | … | `0x00be5ce3` | | | `spawn_info_*.xtbl` loaders end at `0x00be5cd0` (called from game init `0x005d25f0`) | |
| (thunks, not Havok) | `0x00be5ce4` | `0x00be5d1a` | 9 | 9 | nine `jmp [IAT]` import thunks to **WS2_32** (`htons`, `WSASetLastError`, `ntohl`, `gethostname`, `inet_addr`, `inet_ntoa`, `htonl`, `gethostbyname`, `ntohs`), padded with `int3` to `0x00be5d1f` | thunks; called from networking code and twice from inside Havok (`0x00c05410`, Havok's socket class) |
| **Havok range A** | **`0x00be5d20`** | **`0x00c98da0`** | **2,486** | **186,544** | Havok Base, Physics dynamics/world/simulation/constraints/actions/vehicle/utilities, serialization, `hkStructureLayout` (last string anchor `.\Util\hkStructureLayout.cpp` at `0x00c98410`; last function `0x00c98d20`–`0x00c98d9d`) | first function whose callers are all Havok; last Havok function ends `0x00c98d9d` |
| **OpenSSL 1.0.0d (not Havok)** | **`0x00c98da0`** | **`0x00d1d570`** | **1,792** | **157,562** | first function `0x00c98da0` (`.\ssl\ssl_lib.c`); last OpenSSL functions are the BIO base64 / ASN.1 BIO group (`.\crypto\evp\bio_b64.c`, `.\crypto\asn1\bio_asn1.c`), ending with one undefined function `0x00d1d300`–`0x00d1d569` (calls `0x00d1cef0`, the base64 write); then one import thunk `0x00d1d56a` → **IPHLPAPI** `GetAdaptersAddresses` | string anchors; **zero call edges to or from any Havok range** (incoming 290 edges, all from game/libcurl pages `0x0040xxxx`–`0x0041xxxx` plus a scatter; outgoing 1,287, all to CRT/game) |
| **unattributed (not Havok)** | **`0x00d1d570`** | **`0x00d4c800`** | **934** | **58,776** | starts with a small hash routine (`0x00d1d570`, shift-xor mixing) called from ~60 six-instruction stubs; no string anchors until `0x00d28000` (`kernel32.dll` / `Wow64DisableWow64FsRedirection`), `0x00d40000`+ (`STEAM_SPLIT_GUID`, `Software\Valve\Steam`, `InstallPath`), ending with NTDLL wrapper functions `0x00d4c516`–`0x00d4c7f7` (`NtQueryInformationFile`, `RtlNtStatusToDosError`, `NtCreateFile`, `NtQueryDirectoryFile`) | **zero call edges to or from Havok**; 364 incoming edges from all over gameplay code, 230 outgoing to game/CRT. Identity not established (left OPEN, out of scope) |
| **Havok range B** | **`0x00d4c800`** | **`0x00d9dff0`** | **1,000** | **91,547** | Havok collision: shapes (incl. `hkpBoxShape` ctor `0x00d4d7f0`), MOPP, agents, `hkp3AxisSweep` (vtable `0x01246b1c`, all 31 slots in `0x00d5c530`–`0x00d65cf0`) | first function `0x00d4c800` is called by the game's ray collector `0x0075b800` and calls Havok `0x00d6bff0`; last functions `0x00d9d7c0` (491 insns, vtable-only, calls 17 Havok functions) and `0x00d9dfb0`, padding to `0x00d9dfef` |
| (game: vlib, scheduler, file system, curl client, Lua, Vint, render lib, CRT) | `0x00d9dff0` | `0x00ecb400` | | | `0x00d9dff0` is the game timer helper `interp_delay_native.md` §1.3 already cites; `0x00d9e140`/`0x00d9e250` timer, `0x00d9e740` CRC-32, `0x00dc1xxx`–`0x00dc2xxx` the job scheduler (Part 2 §2.4), CRT from `0x00ea0000` to `0x00ecb3ff` (`1#SNAN` formatter `0x00eca31c`) | |
| (thunks, not Havok) | `0x00ecb400` | `0x00ecb412` | 3 | 3 | **XINPUT1_3** ordinals 2 and 3, **DINPUT8** `DirectInput8Create` | thunks |
| **Havok range C** | **`0x00ecb420`** | **`0x00f22240`** | **552** | **99,809** | geometry processing, KD-tree, GJK/penetration (`hkgpConvexHull`, `hkgpMesh`, `TtqueryAabb`, `BTtGJK Ray Cast`, `3TtPenetration`), and the product-keycode check `0x00f21bd0` (calls the keycode reader `0x00c81690` and a 594-instruction routine `0x00f21340`) | first function `0x00ecb420` is only Havok-called; **the scoping note's end `0x00f20000` was ~8.8 KB short** — Havok-internal callers continue to `0x00f21ed0`–`0x00f22236` |
| (game: Wwise and on) | `0x00f22240` | … | | | `0x00f22240` is a destructor installing the `AK::IAkRTPCSubscriber` vtable `0x0126d0f8` | RTTI |
| **Havok total** | | | **4,038** | **377,900** | | |

Corrections to the scoping note's table: range A ends at `0x00c98da0` (not `0x00c98e00`); the OpenSSL block is `0x00c98da0`–`0x00d1d570`; range B ends at `0x00d9dff0` (the `0x00d9dff0`–`0x00d9e13f` stretch is game timer code); range C starts at `0x00ecb420` (the `0x00ecb000`–`0x00ecb3ff` stretch is CRT, and it held the "6 range-C callees" of the scoping census — see §1.2) and ends at `0x00f22240`. The totals barely move (4,038 functions / 377.9k instructions versus ~4,050 / ~377k).

**No game code inside the three ranges (HIGH).** 42 functions inside the ranges have only non-Havok call neighbours; every one is an API leaf that the game calls and that calls nothing large (e.g. the `hkpBoxShape` constructor `0x00d4d7f0`, ten game callers). None carries a game string.

**Havok code outside the three ranges (CONFIRMED by RTTI — new).** Havok's header-inline virtual methods were compiled into Volition's own object files and the linker kept those copies, so 121 slot targets of Havok-class vtables lie outside A/B/C:
- **identical-code-folded stubs** shared program-wide: `0x00754410` (a bare `ret`, in 27 Havok vtables; already documented project-wide as a no-op stub), `0x008a5bf0` (27), `0x006b89a0` (22), `0x006948c0` (18), `0x00db60a0` (13), `0x00f52900` (23, every `hkpSymmetricAgentLinearCast<…>` vtable), `0x00f823e0` (21), and the CRT pure-call handler `0x00ea2778` (50);
- **Havok base-interface defaults living in the game physics cluster**, e.g. `hkpContactListener` slot `+0x0c` → `0x00755670`, `hkpCdPointCollector` → `0x00755730`, `hkpCdBodyPairCollector` → `0x00755760`/`0x00757e90`, `hkpWorldCinfo` destructor `0x00756c10`, `hkpConstraintListener` → `0x00776740`, the four filter-interface defaults `0x00778a60`/`0x00778a80`/`0x00778aa0`/`0x00778ac0`, `hkpMoppCode` → `0x0077a570`, `hkpSingleShapeContainer`/`hkpShapeContainer` → `0x0077b020`/`0x0077d420`/`0x0077d480`, `hkpConvexTranslateShape` → `0x0077d4c0`, `hkpEntityListener`/`hkpPhantomListener` defaults `0x0078bd10`/`0x0078a690`/`0x0078b990`, `hkpWorldObject`/`hkpPhantom` destructors `0x0079ad40`/`0x0079ad80`, `hkpCharacterProxyListener` → `0x0079f870`, `hkpVehicleSuspension` destructor `0x00abcfc0`;
- **one whole Havok class body in the vehicle cluster:** `hkSplineParametricCurve`'s methods are at `0x00ad8740`–`0x00ad9150` (seven slots, ~640 instructions).

So "the game physics clusters" are not 100 % Volition code: a future size figure or RE pass over `0x00754000`–`0x007a5000` and `0x00a77000`–`0x00af0000` must skip these Havok-inline copies (list them by the RTTI slot test above). A related oddity: the Havok-range-B function `0x00d9d7c0` calls the vehicle-cluster function `0x00ae4390` (55 instructions) three times — **HYPOTHESIS:** another identical-code-folded inline shared between a game and a Havok translation unit.

### 1.2 The game side of the boundary — game classes over Havok interfaces, slot by slot — CONFIRMED

Refines scoping note §3.2. Done again from raw bytes in this pass, not copied from the scoping table:
- **Census re-run.** The raw-file RTTI walk (type descriptor → complete-object locator → vtable, with the class-hierarchy base lists) was re-run and gave the same totals as the scoping note: **1,720 type descriptors, 1,701 locators and vtables, 1,579 classes with a vtable**. A second, independent pass re-read every slot of every vtable below straight from the file and re-resolved its locator to its type-descriptor string; every slot and every name matched.
- **Selection.** I took every class whose own name is not a Havok name but whose RTTI base list contains a Havok class. That gives 64. Of those, **25 are Havok's own**: the `hkpSymmetricAgent<…>` / `hkpSymmetricAgentLinearCast<…>` template instances, the `hkgp` mesh and job internals, `IConvexOverlapImpl`, and two anonymous-namespace filters (`Clear1nTracksFilter`, `NeverCollideFilter`). Every slot of those 25 lies in range A, B or C, except the identical-code-folded stubs listed in §1.1. That leaves **39 game classes**.
- **Slot classes.** Each slot outside A/B/C is put into one of three kinds by comparing it with the Havok base's own vtable for the same subobject:
  - **Havok-callable override**: the slot exists in the Havok base, and the game put its own code there. Havok can call these. This is the real callback contract.
  - **Havok default copy**: the slot holds the same address as the Havok base's own vtable. It is a header-inline Havok method compiled outside the ranges (§1.1), so it is not game logic.
  - **Game-introduced virtual**: the slot index lies past the end of the Havok base's vtable. The game added this virtual itself. **Havok cannot call it**: Havok code only knows the base layout. Only game code calls these.
- **Totals over the 39 classes:** **72 Havok-callable override slots** (destructors included), **38 Havok default copies**, **24 game-introduced virtuals**.
- "Pure in base" means the Havok base's slot is the CRT pure-call handler `0x00ea2778`. In other words the Havok class is an interface there, and the game must supply the method.
- Numbers in brackets are instruction counts; `~` marks a body Ghidra never defined, measured to its first return.
- Slot +0x00 on a `hkReferencedObject`-derived class is the destructor. Havok calls it when the last reference is dropped.

| # | Game class (RTTI) | Havok base (direct) | vtable (slots; Havok base slots if fewer) | Havok-callable game overrides | Havok default copies | Game-introduced virtuals (not callable by Havok) |
|---|---|---|---|---|---|---|
| 1 | `havok_rigid_body` | `hkpRigidBody` | `0x01153898` (8) | +0x00 `0x00756fe0` [20] | — | — |
| 2 | `game_havok_world` | `hkpWorld` | `0x011539e8` (3) | +0x00 `0x00757370` [18] | — | — |
| 3 | `custom_collision_listener` | `hkpContactListener` | `0x011539c8` (7) | +0x00 `0x00769040` [361], +0x04 `0x00769500` [186], +0x08 `0x00769750` [186] | +0x0c `0x00755670` | — |
| 4 | `havok_game_filter` | `hkpCollisionFilter` @+0; `hkpCollidableCollidableFilter` @+8; `hkpShapeCollectionFilter` @+12; `hkpRayShapeCollectionFilter` @+16; `hkpRayCollidableFilter` @+20 | `0x01153e3c` (9; base 4), `0x01153e30` (2), `0x01153e1c` (4), `0x01153e10` (2), `0x01153e04` (2) | @+8 +0x04 `0x00779260` [334]; @+12 +0x00 `0x00778ef0` [~48], +0x04 `0x00778d50` [150]; @+16 +0x00 `0x00778f70` [108]; @+20 +0x04 `0x00779160` [~22] — all five pure in base | — | @+0 +0x10 `0x00778950` [~32], +0x14 `0x007789a0` [~34], +0x18 `0x00778a00` [20], +0x1c `0x00778a30` [20], +0x20 `0x00778c50` [32] |
| 5 | `hk_broadphase_border` | `hkpBroadPhaseBorder` @+0; `hkpWorldDeletionListener` @+8; `hkpPhantomOverlapListener` @+12; `hkpWorldPostSimulationListener` @+16 | `0x011539b0` (5), `0x011539a0` (3), `0x01153990` (3), `0x01153980` (3) | @+0 +0x0c `0x0075aad0` [89]; @+8 +0x00 `0x00755ba0` [~39] | — | — |
| 6 | `hk_suspend_inactive_agents_util` | `hkpSuspendInactiveAgentsUtil` @+0; `hkpIslandActivationListener` @+8; `hkpWorldDeletionListener` @+12 | `0x01153944` (4; base 3), `0x01153934` (3), `0x01153924` (3) | @+8 +0x08 `0x0079e540` [81] (pure in base) | — | @+0 +0x0c `0x007557a0` [~5] |
| 7 | `havok_memory_watchdog` | `hkpDefaultWorldMemoryWatchDog` | `0x01153ecc` (6) | +0x10 `0x00779d70` [~21] | — | — |
| 8 | `CriticalAllocator` | `hkMemoryAllocator` | `0x01153e9c` (11) | +0x04 `0x0077a0f0` [~69], +0x08 `0x0077a110` [~52] (both pure in base) | +0x28 `0x00754410` | — |
| 9 | `constraint_instance_breakable_listener` | `hkpConstraintListener` | `0x01153d94` (7) | +0x14 `0x00777fe0` [~19] | +0x00 `0x00776740` | — |
| 10 | `havok_const_accl_action` | `hkpUnaryAction` | `0x01153a4c` (8) | +0x0c `0x007588e0` [48], +0x1c `0x00758970` [53] (both pure in base) | +0x04 `0x00756a80` | — |
| 11 | `multi_game_havok_corrector` | `hkpUnaryAction` | `0x01166714` (8) | **none**. +0x0c is the folded one-instruction stub `0x00c0dea0` (return, pop 4) and +0x1c is `0x00c211d0` (return 0, pop 8), both in range A | +0x04 `0x00756a80` | — |
| 12 | `object_collector` | `hkpCdBodyPairCollector` | `0x01153a2c` (3) | +0x04 `0x0075ae80` [80] (pure in base) | +0x00 `0x00757e90`, +0x08 `0x00755760` | — |
| 13 | `contact_collector` | `hkpCdPointCollector` | `0x01153a3c` (3) | +0x04 `0x007565c0` [38] (pure in base) | +0x08 `0x00755730` | — |
| 14 | `havok_closest_ray_hit_collector` | `hkpClosestRayHitCollector` | `0x01153a90` (2) | +0x00 `0x0075b800` [54] | +0x04 `0x00756e90` | — |
| 15 | `havok_all_ray_hit_collector` | `hkpAllRayHitCollector` | `0x01153ad8` (2) | +0x00 `0x0075b9c0` [77], +0x04 `0x0075bf70` [19] | — | — |
| 16 | `havok_closest_linear_cast_collector` | `hkpClosestCdPointCollector` | `0x01153a9c` (3) | +0x04 `0x0075bb50` [39], +0x08 `0x00756fa0` [~11] | +0x00 `0x00756e20` | — |
| 17 | `havok_all_linear_cast_collector` | `hkpAllCdPointCollector` | `0x01153ae4` (3) | +0x00 `0x0075bfb0` [19], +0x04 `0x0075bc60` [~62], +0x08 `0x0075bc40` [~6] | — | — |
| 18 | `man_cannon_vacuum_collector` | `hkpCdBodyPairCollector` | `0x01182210` (3) | +0x04 `0x00ab1d60` [~53] (pure in base) | +0x00 `0x00757e90`, +0x08 `0x00755760` | — |
| 19 | `hk_rigid_body_in_water_listener` | `hkpEntityListener` | `0x01154664` (6) | +0x14 `0x00796940` [~21] | +0x00 `0x0078bd10`, +0x0c `0x0078b990` | — |
| 20 | `hk_phantom_in_water_listener` | `hkpPhantomListener` | `0x01154680` (5) | +0x10 `0x00796990` [~13] | +0x00 `0x0078a690`, +0x0c `0x0078b990` | — |
| 21 | `hk_character_shape_phantom` | `hkpCachingShapePhantom` | `0x011547ec` (23; base 19) | +0x20 `0x0079abf0` [~12] | +0x00 `0x0079a990` | +0x4c `0x0079a9d0` [61], +0x50 `0x0079a6b0` [102], +0x54 `0x0079aaa0` [56], +0x58 `0x0079a670` [15] |
| 22 | `hk_custom_character_proxy` | `hkpCharacterProxy` @+0; `hkpEntityListener` @+8; `hkpPhantomListener` @+12 | `0x01154a18` (6; base 5), `0x011549fc` (6), `0x011549e4` (5) | @+0 +0x00 `0x0079dd10` [18]; @+8 +0x00 `0x0079c4b0` [~90]; @+12 +0x00 `0x0079c4c0` [~80] | @+8 and @+12 +0x0c `0x0078b990` | @+0 +0x14 `0x0079de40` [536] |
| 23 | `MyCharacterListener` | `hkpCharacterProxyListener` | `0x01154a98` (6) | +0x00 `0x0079f8d0` [~17], +0x04 `0x007a2960` [578], +0x10 `0x0079fb80` [~24], +0x14 `0x007a45f0` [288] | +0x08 and +0x0c `0x008a5bf0` | — |
| 24 | `havok_custom_character_collector` | `hkpAllCdPointCollector` | `0x011549d4` (3) | +0x00 `0x0079dcd0` [19], +0x04 `0x00778470` [396], +0x08 `0x0079c300` [~7] | — | — |
| 25 | `character_controller` | `hkReferencedObject` only (no Havok interface) | `0x01154ac4` (3) | +0x00 `0x007a1870` [18] | — | — |
| 26 | `hk_custom_vehicle_instance` | `hkpVehicleInstance` | `0x011853f4` (20; base 16) | +0x00 `0x00ae8470` [18], +0x0c `0x00ae9b40` [20], +0x2c `0x00ae8ca0` [168] | +0x04 `0x00756a80` | +0x40 `0x00ae84b0` [517], +0x44 `0x00ae5c80` [39], +0x48 `0x00ae4ab0` [~4], +0x4c `0x00ab8a60` [~69] |
| 27 | `hk_custom_motorcycle_instance` | `hk_custom_vehicle_instance` (#26) | `0x0118270c` (20) | +0x00 `0x00aba2d0` [19], +0x0c `0x00ae2800` [308] | +0x04; +0x2c taken over from #26 | +0x40 `0x00ae1170` [33] replaces #26's game-introduced +0x40; +0x44 to +0x4c taken over from #26 |
| 28 | `hk_custom_aircraft_instance` | `hk_custom_vehicle_instance` (#26) | `0x0118522c` (21) | +0x00 `0x00ade080` [18], +0x0c `0x00ae0b10` [103], +0x20 `0x00adabf0` [81] | +0x04; +0x2c taken over from #26 | +0x50 `0x00adab40` [47] (new at this level); +0x40 to +0x4c taken over from #26 |
| 29 | `hk_custom_watercraft_instance` | **`hkpUnaryAction`** (not the vehicle instance) | `0x011855ac` (8) | +0x0c `0x00aeeb30` [43] (pure in base) | +0x04 `0x00756a80` | — (+0x1c is range-A `0x00c211d0`) |
| 30 | `hk_custom_vehicle_engine` | `hkpVehicleDefaultEngine` | `0x01182784` (6; base 4) | +0x0c `0x00ae31a0` [~38] | — | +0x10 `0x00ae31f0` [~23], +0x14 `0x00ae3330` [293] |
| 31 | `hk_custom_vehicle_steering` | `hkpVehicleDefaultSteering` | `0x0118548c` (5) | +0x00 `0x00aeb550` [18], +0x0c `0x00aeb230` [142], +0x10 `0x00aeb0c0` [~13] | — | — |
| 32 | `hk_custom_vehicle_transmission` | `hkpVehicleDefaultTransmission` | `0x011827ec` (10; base 9) | +0x00 `0x00abd7b0` [18], +0x0c `0x00aecd60` [69], +0x1c `0x00aece20` [~65] | — | +0x24 `0x00aecef0` [~193] |
| 33 | `hk_custom_vehicle_brakes` | `hkpVehicleDefaultBrake` | `0x01182818` (4) | +0x00 `0x00abdac0` [18], +0x0c `0x00ae2c80` [293] | — | — |
| 34 | `hk_custom_vehicle_suspension` | `hkpVehicleDefaultSuspension` | `0x011827d8` (4) | +0x0c `0x00aebe30` [992] | +0x00 `0x00abd350` | — |
| 35 | `hk_custom_vehicle_driver_input` | `hkpVehicleDefaultAnalogDriverInput` | `0x01182760` (8) | +0x0c `0x00ae30c0` [53] | — | — |
| 36 | `hk_custom_vehicle_raycast_wheel_collide` | `hkpVehicleRayCastWheelCollide` | `0x0118282c` (19; base 17) | +0x00 `0x00abee20` [18], +0x34 `0x00aea040` [95], +0x3c `0x00aea160` [150] | +0x2c `0x00abd220` | +0x44 `0x00ae9f10` [100], +0x48 `0x00aeadb0` [192] |
| 37 | `havok_wheel_closest_linear_cast_collector` | `havok_closest_linear_cast_collector` (#16) | `0x0118545c` (3) | +0x04 `0x00aeab50` [29], +0x08 `0x00aea480` [~13] | +0x00 `0x00756e20` | — |
| 38 | `hk_custom_vehicle_extra_downforce` | `hkpUnaryAction` | `0x01185368` (11; base 8) | +0x0c `0x00ae3850` [291], +0x1c `0x00ae3cb0` [54] (both pure in base) | +0x04 `0x00756a80` | +0x20 `0x00ae3790` [~3], +0x24 `0x00ae37a0` [~3], +0x28 `0x00ae3770` [~11] |
| 39 | `hk_door_constraint_motor` | `hkpCallbackConstraintMotor` | `0x011855d0` (4) | **none in the vtable** (every slot is range A). Its behaviour is a plain function pointer, see below | — | — |

**Corrections to the scoping note's §3.2 table:**
1. **`multi_game_havok_corrector` has no game override at all.** Its per-step action slot (+0x0c) is a folded no-op stub inside range A. **HIGH:** it is an action that does nothing when stepped. The two addresses the scoping note gave it, `0x007588e0` and `0x00758970`, both belong to `havok_const_accl_action` (#10) alone.
2. **`hk_custom_watercraft_instance` is not a vehicle instance.** It derives straight from `hkpUnaryAction` (8 slots), with one override, `0x00aeeb30`. The scoping note grouped it with the motorcycle and aircraft instances. `0x00adab40` belongs to the aircraft instance: it is a slot the aircraft class adds (+0x50).
3. **Twenty-four of the "game override addresses" are not Havok callbacks.** They are virtuals the game class adds past the end of the Havok base's vtable. The ones the scoping note listed as callback contracts are:
   - `havok_game_filter`'s whole primary run `0x00778950`…`0x00778c50`. Its real Havok-facing filter methods are the five secondary-vtable slots in row 4, chiefly `0x00779260` [334].
   - The engine's `0x00ae3330` [293].
   - The transmission's `0x00aecef0` [~193].
   - The vehicle instance's `0x00ae84b0` [517], `0x00ae5c80` and `0x00ae4ab0`, plus `0x00ab8a60`.
   - The wheel-collide's `0x00ae9f10` and `0x00aeadb0`.
   - The character proxy's `0x0079de40` [536].
   - Four of the character phantom's slots (`0x0079a670`…`0x0079aaa0`).
   - `0x007557a0`.
   - The downforce action's +0x20 to +0x28.

   **HIGH:** these are game-side entry points that the game's own code calls, for example from the custom vehicle step. A replacement engine does not need to call them. They belong to P2–P5, not to the boundary.
4. **Eleven listed addresses are Havok default copies, not game logic:** `0x00755670`, `0x00776740` (the scoping note's first `constraint_instance_breakable_listener` override), `0x00757e90`, `0x00755760`, `0x00755730`, `0x00756e90`, `0x00756e20`, `0x0078bd10`, `0x0078a690`, `0x0079a990` and `0x00abd350`.
5. **New rows:** `hk_door_constraint_motor` (#39) and `character_controller` (#25). The second is not over a Havok interface, only over Havok's reference-counted base. The scoping table had neither.
6. The scoping note says **"Havok calls game code only through these vtables."** That needs one exception. The **door motor's game logic is a function pointer**: the game constructor `0x00aef0a0` passes `0x00aeee60` (code Ghidra never defined as a function) and the value 1 to the Havok base constructor `0x00c34ff0`. CONFIRMED, from the push at `0x00aef0a4`. **HIGH:** this is `hkpCallbackConstraintMotor`'s callback.
   - I checked all 2,019 outside-to-Havok call sites (§1.3) for immediates that are game `.text` addresses within the 8 instructions before the call. Only two turned up: this one, and the no-op stub `0x00754410`, which the game passes to Havok `0x00bec9a0` during memory bring-up.
   - **HIGH:** apart from the 72 vtable slots above, those are the only game code addresses handed to Havok as data.
   - Caveat: a pointer loaded further from its call site, or passed in a structure, would escape this scan.

**Slot meanings (HIGH, from shape; the names are Havok's public 2010.2 API):**
- **Action step.** +0x0c of every `hkpUnaryAction`-derived row is pure in the base, and the vehicle's slot leads into the custom step (scoping note §1.5). It is `applyAction`.
- **Filter methods.** The four secondary filter interfaces each declare one pure method beside their destructor (rows 4 and 6). These are the `isCollisionEnabled` family (the shape-collection interface has two).
- I have not mapped any other slot to a method name. That belongs to the P2/P3/P5 units, which will read the game side of each slot.

### 1.3 The call-in side — census with caller counts and a call-site data table — CONFIRMED

Refines scoping note §3.3–§3.4.

**Method.**
- An edge counts if it is a direct call or jump from code outside A/B/C to a function inside them that has more than 3 instructions.
- Edges come from the Ghidra-wide edge list (41.8k functions), plus a raw `E8`/`E9` byte scan of `.text` for call sites Ghidra never put in a function.
- Functions of 3 instructions or fewer are counted separately, at the end of this section, because identical-code folding puts tiny shared stubs inside the ranges.

**Counts.**

| Measure | This pass | Scoping note |
|---|---|---|
| Call sites, Ghidra-known (inside defined functions) | 1,458 in 567 functions | — |
| Call sites found only by the raw byte scan (outside any defined function) | 498 (458 in the static-initialiser pages `0x00fd0000`–`0x0101ffff`, 40 in undefined gameplay code at `0x0075`…`0x00b1`) | — |
| Raw-scan hits inside a defined function that Ghidra did not list (mid-instruction false positives) | **0** | — |
| **All call sites** | **1,956** | 1,940 |
| **Distinct Havok entry points called** | **317** (266 in A, 51 in B, **0 in C**) | 326 (267 / 53 / 6) |
| Called only from static initialisers | 71 entry points, 458 sites (373 of them into one 26-instruction registration helper, `0x00bebf30`) | ~440 callers |
| **Called at runtime** | **246 entry points** (215 A, 31 B), **1,498 sites**, **607 calling functions or undefined sites** | — |
| Runtime sites by caller location | general physics cluster `0x00754000`–`0x007a5000`: 796; vehicle cluster `0x00a77000`–`0x00af0000`: 403; elsewhere in gameplay: 298; static region: 1 | "mostly the two clusters" |

- **Range C has no outside caller** (CONFIRMED). The scoping note's "6 range-C callees" were CRT functions in `0x00ecb000`–`0x00ecb3ff`, which §1.1 moved out of range C. So geometry processing, the KD-tree and GJK are reached only through Havok itself.
- The small drop from 326 to 317 comes from the corrected range edges in §1.1. The game timer stretch `0x00d9dff0`–`0x00d9e13f`, which the old range B wrongly took in, is called heavily.

**Tiny entry points (3 instructions or fewer).** 23 such functions inside the ranges are called from outside, at 63 sites.
- Only 2 are folded stubs: `0x00c0dea0` (14 callers) and `0x00c211d0`.
- The other **21 are real one-line Havok entry points**, and are part of the called surface:
  - the memory-system instance getter `0x00bec640` (7 game callers) and its setter `0x00bec630`;
  - a vtable installer `0x00bec510` (6);
  - field getters and setters on Havok objects: `0x00c967e0`, `0x00c83560`, `0x00c835b0`, `0x00c835e0`, `0x00c2c0b0`, `0x00c2c0c0`, `0x00c2c070`, `0x00c1f290` (writes +0x94), `0x00c2da20` (+0x108), `0x00c2c060` (+0x1c);
  - five installers that put Havok's own function pointers into Havok globals: `0x00c5f2a0`, `0x00c88f10`, `0x00c28bf0`, `0x00c254f0`, `0x00c23080`;
  - three forwarders: `0x00c55f60`, `0x00beeeb0`, `0x00c1e920`.
- **The full directly called surface is therefore 246 + 21 = 267 runtime entry points, plus 71 that only static initialisers call.**

**What the census cannot see (HIGH).**
- **Virtual calls the game makes on Havok objects.** Examples are the memory-system calls through `0x00bec640`'s result in `0x00779b50` and `0x0077a280` (Part 2), and the error-object calls in bring-up (Part 2 §2.1). These cross into Havok without a direct edge.
- **Inline field reads and writes on Havok objects.** One example is the vehicle pre-step testing the body pointer at vehicle +0x81c before calling into Havok.

The scoping note's 98-function world-handle figure stands as the best available measure of that hidden surface.

**Call-site data table: the most-used entry points.**
- **Shape** is read from each callee's own return (`ret N`: callee pops N bytes, so N/4 stack arguments plus `this` in ECX), or from the caller's stack clean-up.
- **Receiver** is what the caller loads into ECX, sampled over up to 30 sites.
- Readings are labelled. None comes from reading the Havok body beyond the vtable it installs.

| Entry | Insns | Callers / sites | Shape | Receiver at call sites | Callers in P / V / elsewhere | Reading |
|---|---|---|---|---|---|---|
| `0x00be6620` | 7 | 163 / 249 | plain call, no stack args | the fixed object `0x01312854` at 30/30 sampled sites | 70 / 46 / 132 | CONFIRMED by the scoping note's vtable check: `hkContainerHeapAllocator` slot; container growth inlined into game code |
| `0x00c150c0` | 35 | 84 / 129 | `this`, no args | a body or entity in a register; never the world handle. In `0x00755bb0` the receiver is a queued object whose +0x08 (world pointer) is non-null; in `0x00758f00` it is called only when `0x00c14cc0` on the same body returned false | 43 / 61 / 25 | **Correction:** not a world lock. **HYPOTHESIS:** entity `activate` (the classic "if not active, activate" pairing with `0x00c14cc0`) |
| `0x00be6100` | 73 | 76 / 152 | `this`, no args | world, filters, border, watchdog, shapes, bodies | 109 / 21 / 22 | HIGH: reference release. Bring-up releases each object right after handing it to the world; shutdown releases the world itself |
| `0x00c14790` | 28 | 49 / 65 | `this`, no args | very often the address of a member at +0xe0 of a body object (`lea ecx,[reg+0xe0]`) | 21 / 28 / 16 | **Correction:** not a world unlock. HIGH: a method on an embedded sub-object of the body |
| `0x00be6f90` | 54 | 38 / 61 | no `this`; caller pops 16 | — | 30 / 30 / 1 | HIGH: Havok memory (scoping note) |
| `0x00be7010` | 59 | 29 / 46 | no `this`; caller pops 12 | — | 34 / 8 / 4 | HIGH: Havok memory |
| `0x00c21b20` | 38 | 20 / 30 | `this` + 3 args | a freshly constructed object, e.g. each bring-up phantom right after its constructor, with (3, 0, 0) (Part 2 §2.1 step 15) | 18 / 9 / 3 | — |
| `0x00be9c80` | 26 | 18 / 26 | `this` + 2 args | registers | 11 / 12 / 3 | — |
| `0x00c177b0` | 78 | 17 / 21 | `this` + 1 arg | **the world handle at 21/21 sites** | 9 / 6 / 6 | HIGH: world removal; shutdown uses it on the two bring-up phantoms |
| `0x00c18930` | 77 | 10 / 12 | `this` + 1 arg | **world, 12/12** | 6 / 2 / 4 | HIGH: world addition, the partner of `0x00c177b0`; bring-up uses it on the two phantoms |
| `0x00c19160` | 143 | 11 / 16 | `this` + 2 args | **world, 15/16** | 14 / 2 / 0 | HYPOTHESIS: world add-entity |
| `0x00c16840` / `0x00c16990` | 97 / 66 | 10 / 10, 9 / 11 | `this` + 2 args | **world, 10/10 and 11/11** | — | world methods; not identified |
| `0x00c19330` | 305 | 6 / 6 | `this` + 3 args | **world, 5/6** | 3 / 0 / 3 | called right after `0x00c150c0` on the same entity, with arguments (entity, 0, 1) |
| `0x00c1eca0` | 44 | 11 / 18 | `this` + 3 args | bodies, including the vehicle's body at +0x81c | 6 / 12 / 0 | HYPOTHESIS: a body state setter; `0x00758f00` calls it with (1, 1, 0) |
| `0x00c14cc0` | 12 | 11 / 11 | `this` + 1 arg, a pointer to a one-byte result | bodies, including vehicle +0x81c | 5 / 5 / 1 | HIGH: a boolean query on a body returned through a hidden pointer. HYPOTHESIS: `isActive` |
| `0x00d4d7f0` | 40 | 10 / 10 | `this` + 2 args | freshly allocated block | 5 / 3 / 2 | CONFIRMED: `hkpBoxShape` constructor (installs its vtable) |
| `0x00d50670` | 9 | 11 / 16 | `this` + 1 arg | freshly allocated block | 14 / 1 / 1 | CONFIRMED this pass: installs the `hkpSphereShape` vtable, so it is the sphere-shape constructor |
| `0x00c1e1d0` | 19 | 7 / 8 | `this` + 3 args | freshly allocated block | 4 / 3 / 1 | CONFIRMED this pass: installs the `hkpSimpleShapePhantom` vtable |
| `0x00d4f8d0` / `0x00d4fb30` | 6 / 50 | 10 / 41, 7 / 19 | `this` + 2 / + 3 args | a stack object (24 of 30 sites) | mostly P | — (range B; not identified) |

**World-handle receivers.** 18 entry points take the world handle `0x0161d378` as `this` at half or more of their sampled sites:
- `0x00c177b0`, `0x00c19160`, `0x00c16840`, `0x00c18930`, `0x00c16990`, `0x00c19330`, `0x00c1bc10`, `0x00c1bd70`, `0x00c19f60`, `0x00c1a3a0`, `0x00c1be40`, `0x00c163a0`, `0x00c19760`, `0x00c19cb0`;
- the four lifecycle-only ones: `0x00c16ac0`, `0x00c16fc0`, `0x00c17030`, `0x00c17920` (Part 2).

**This is the world API the game uses directly. All of it is in `0x00c16xxx`–`0x00c1bxxx`.**

**What crosses the boundary (refines scoping note §3.4).** The rows below were confirmed this pass. The remaining §3.4 rows stand as HIGH.

| Direction | What | Where | Label |
|---|---|---|---|
| game → Havok | the world construction info: a Havok structure built and filled on the game's stack, then passed by pointer, plus the SDK version number **20100200** (`0x0132b468`; Ghidra mislabels it as a pointer to an RTTI string) | `0x007572f0` → `0x00c1c4f0` | CONFIRMED (Part 2 §2.1) |
| game → Havok | the job queue and the step length Δt (a float) to begin; the job queue and a null second argument to finish | `0x00755c80` → `0x00c17920` / `0x00c16fc0` | CONFIRMED (Part 2 §2.3) |
| game → Havok | a function pointer: the door motor callback `0x00aeee60` | `0x00aef0a0` → `0x00c34ff0` | CONFIRMED |
| game → Havok | the game filter object, wrapped by Havok's `hkpConstraintCollisionFilter` | `0x0075f370` → `0x00c2b380` | CONFIRMED (Part 2 §2.1) |
| Havok → game | the job queue's per-thread draining, run on **game** worker threads | worker `0x00754dd0` → `0x00be88c0` | CONFIRMED (Part 2 §2.4) |
| game → Havok | per-vehicle pre-collision update, one call per vehicle instance with no stack arguments | `0x00ac3f40` → `0x00c561d0` | CONFIRMED (Part 2 §2.3) |

**The other direction: direct calls from Havok to code outside the ranges (CONFIRMED counts, HIGH reading).**
- Havok code makes **474 direct calls to 63 functions outside A/B/C** (functions of more than 3 instructions).
  - **268 sites go into 22 CRT functions** (`0x00ea0000`–`0x00ecb3ff`).
  - **206 sites go into 41 other functions.** All are small (at most 110 instructions), and most have 0 to 3 game callers. They are the header-inline and identical-code-folded copies described in §1.1, for example `0x00756b10` (38 Havok sites), `0x00ae4390` (20) and `0x0079e7f0` (21).
- Two of the 41 have a known identity:
  - **`0x007557e0` is Havok's product-features initialiser compiled into a game object file.** It installs the `hkProductFeatures` vtable `0x01153958` and calls five range-A installers (`0x00c28bf0`, `0x00c254f0`, `0x00c23080`, `0x00c88f10`, `0x00c5f2a0`), each of which stores Havok's own function pointers into Havok globals. Its only caller is Havok `0x00bec9a0` (Part 2 §2.1). CONFIRMED.
  - **`0x00756cb0`** (31 instructions, 30 game callers, 1 Havok caller) searches a 16-byte-stride `{key, …, 64-bit value}` array at object +0x7c, with its count at +0x80. **HIGH:** this is a header-inline copy of Havok's world-object property lookup, which means the game keeps its own data in Havok body properties (a P2 item).
- **None of the 41 is Volition gameplay logic** (HIGH). Havok reaches game logic only through the 72 slots of §1.2 and the one function pointer `0x00aeee60`.

---

## Part 2 — Lifecycle and step

All five functions were read in full in this unit. The source was the Ghidra listing dumps made on the private copies before the interruption (kept in the session scratchpad), each one re-read and cross-checked in this pass against capstone disassembly of the raw file. Every direct callee was read too, except two: the 22,979-instruction table builder `0x00769c30` and the vehicle custom sub-steps, both out of scope (see the notes in place). Global values are static values read from the file unless a runtime writer is named.

### 2.1 Bring-up — `0x0075f370` — CONFIRMED call sequence

- **Call site.** Called once, from the game initialisation routine `0x005d25f0` at `0x005d2d0c`. That call sits between two calls to the stub `0x006948c0`.
- **Guard.** The byte `0x0161d39f` ("physics initialised"):
  - bring-up returns at once if it is already set;
  - bring-up sets it last;
  - shutdown clears it.
- **Order.** The table follows the code's own order. Left column: **G** = game code only; **H** = crosses into Havok (target address given).

| # | | What happens | Label |
|---|---|---|---|
| 1 | G→H | **Havok memory and base system.** `0x00788a60` builds two game memory blocks named `havok bmem 0` and `havok bmem 1`. `0x00779da0` then crosses into Havok: `0x00bee200` installs the `hkFreeListAllocator` and `hkFixedSizeAllocator` vtables; `0x00bed470` installs `hkFreeListMemorySystem`; `0x00bec630` stores the memory-system instance pointer at `0x029c0684` (read back by the getter `0x00bec640`); `0x00bec620` follows; then `0x00bec9a0` installs `hkNativeFileSystem`, writes the error-object pointer `0x029c0680`, and calls the game-object-file product-features initialiser `0x007557e0` (§1.3). The game passes its no-op stub `0x00754410` to `0x00bec9a0` as a pointer argument. A game timer `0x00d9e140` is reset at the end. | CONFIRMED (vtables by RTTI). HIGH: this is Havok's memory-system and base-system initialisation. |
| 2 | G→H | **Two report IDs switched off.** Two virtual calls through the object at `0x029c0680`, slot +0x10, with the 32-bit IDs `0xf03421de` and `0x56f80833` and a one-byte false. | CONFIRMED shape. HIGH: `0x029c0680` is Havok's error object (written by `0x00bec9a0`, cleared by the base-system quit `0x00becb90`, read by the world constructor). HYPOTHESIS: the method disables those two warnings. |
| 3 | G | Six game globals are zeroed (`0x0161cb70`/`74`, `0x0161caf8`/`fc`, `0x0161ca80`/`84`). | CONFIRMED |
| 4 | H | **World construction info built on the stack.** `0x00c2b870` installs the `hkpWorldCinfo` vtable `0x01153a70` into a stack block. `0x00c2b750` is then called on it with the single argument 5. | CONFIRMED (vtable). HYPOTHESIS: argument 5 picks a solver preset. |
| 5 | G | **Values written into the construction info.** Offsets are from the block's start. **Gravity** at +0x10 = (0, **−9.8**, 0, 0), so the game is Y-up. **+0x20** = 4096. **+0x28** (byte) = 3. **+0x30 / +0x40** = the broadphase box, made by `0x005810d0` and `0x00778210`. `0x005810d0` returns the fixed city box x ∈ [−3000, 3000], z ∈ [−3000, 2700], y = 0. Bring-up widens it by 500 on x and z and sets y to [−500, 1500], giving **min (−3500, −500, −3500), max (3500, 1500, 3200)**. **+0x54** = **0.04**. **+0xd9** (byte) = **3**. **+0xe8** (byte) = 1. A copy of the box, shrunk by 1.0 on each side, is kept for game use at `0x0163a8c0`/`0x0163a8d0`. | CONFIRMED values and offsets. HYPOTHESIS field names from Havok 2010's public header order: +0x20 broadphase query size, +0x28 broadphase border behaviour (3 = do nothing; the game supplies its own border, step 11), +0x54 collision tolerance, +0xd9 simulation type. +0xd9 = 3 is HIGH as "multithreaded": step 9 builds the job queue only when it equals 3. |
| 6 | G→H | **Game collision filter.** A 0x3c-byte block is taken from Havok's thread heap (TLS `0x029bfc48`, router +0x2c, slot +0x04) and built by `0x00778b30`. That function calls the Havok base constructor `0x00d54570`, installs the five `havok_game_filter` vtables, stores 13 and the table address `0x01607900`, sets the filter type field +0x20 to 4, and fills the 2 KB table at `0x01607900` with 0xff. The result is kept at **`0x0161ca7c`**. | CONFIRMED. HIGH: type 4 is Havok's "custom filter". The layer rules belong to P2. |
| 7 | H | **Havok's constraint filter wraps it.** `0x00c2b380` builds a 0x44-byte **`hkpConstraintCollisionFilter`** (vtables `0x0119c480`–`0x0119c4d8`, via its `hkpPairCollisionFilter` base `0x00c44fd0`) around the game filter, with an `hkpConstraintListener` sub-object at +0x40. It is kept at `0x0161d3a4`, given a reference (`0x00be6060`), and stored as the construction info's collision filter at +0x58. | CONFIRMED (vtables by RTTI; sub-object offset from the vtable store at `0x00c2b38d`) |
| 8 | G | **Default convex radius.** The global `0x0131e81c` changes from its static **0.05** to **0.005** (`0x01153b04`). | CONFIRMED value. HIGH: this is Havok's global default convex radius (Havok's default is 0.05, and Havok's own shape code in range B reads this global). Raises the scoping note's HYPOTHESIS. |
| 9 | G→H | **World.** A 0x370-byte block from the thread heap goes to `0x007572f0(info, 20100200)`; see §2.2. The result is kept at **`0x0161d378`**, and the filter and game-filter references are released. `0x00d529e0` is then called with world +0x80. | CONFIRMED. HYPOTHESIS: `0x00d529e0` registers the collision agents on the world's dispatcher; its callees are all range B. |
| 10 | H | **Job queue.** Built only when +0xd9 = 3. `0x00be8910` builds a queue-construction block on the stack (fields +0x00 = 1, +0x1c = 21); a 0x290-byte block goes to `0x00be8960`. The queue is kept at **`0x0161d388`**, and +0x30 of its internal block at +0x18 is set to 1. **`0x00c2b220(queue)`** then registers Havok's job functions (its callees reach range-B `0x00d65f10`/`0x00d65f90`). `0x0075f2c0` frees the construction block. | CONFIRMED sequence. HYPOTHESIS for the field meanings and for the identity of `0x00c2b220`. Note: no `hkJobThreadPool`-family class has RTTI anywhere in the image (CONFIRMED), so no Havok thread pool is ever built (§2.4). |
| 11 | G→H | **Broadphase border.** A 0x44-byte block goes to `0x00755b50(world, 1)`. That calls the Havok base `0x00c2a960` (installs `hkpBroadPhaseBorder`), passing world, 1 and a one-byte false, then installs the four `hk_broadphase_border` vtables. The result is kept at `0x0161d3a8` and its reference released (the world holds it). | CONFIRMED |
| 12 | G | **Game tables.** The 5,000-slot game body table `0x0161d400`–`0x0163a8c0` (24-byte slots; slot +0 holds the Havok body pointer) is cleared in place. `0x0075f8f0` then sets up a second 5,000-entry pool: 0x40-byte records at `0x0163f720`, and an index table at `0x0163a8f0` filled with 0…4999 (a free list). Game code refers to a body by its slot index; the readers check index ≤ 0x1387. | CONFIRMED |
| 13 | G | `0x00769c30(game filter)`, 22,979 instructions, not opened (P2/P3: collision-material and layer tables). | — |
| 14 | H | **Suspend-inactive-agents utility.** A 0x1c-byte block goes to Havok `0x00c88750(world, 1, 1)` (installs `hkpSuspendInactiveAgentsUtil`). The game then overwrites the three vtables with its own (`0x01153944`, `0x01153934`, `0x01153924`): the subclass is built inline. Kept at `0x0161d37c`. | CONFIRMED |
| 15 | G | **Sub-inits, in order:** (a) **`0x00779f00`**, Havok memory on every worker thread, §2.4; (b) **`0x0077a2c0`**, which builds a watchdog with Havok `0x00c2f820(0x100000)` (installs `hkpDefaultWorldMemoryWatchDog`), installs the `havok_memory_watchdog` vtable, hands it to the world with **`0x00c17030(world, watchdog)`** and releases it; (c) `0x0079af20(world)`, which stores the world pointer for the character code at `0x0223ac48`; (d) `0x007669d0`, game tables only, no Havok call; (e) **`0x00757100`**, two query phantoms (below); (f) `0x007920c0`, water set-up from the `water_physics` table, no direct Havok call. | CONFIRMED. HIGH: `0x00c17030` sets the world's memory watchdog. |
| 16 | G | Flag `0x0161d39f` = 1. The construction info is torn down by `0x00756b80` (re-installs the `hkpWorldCinfo` vtable and releases its references). | CONFIRMED |

**The two query phantoms (step 15e, CONFIRMED).** Each is built and registered the same way:
1. A Havok shape is built.
2. A 0x170-byte block goes to `0x00c1e1d0(shape, 0x029c0640, 0x80002)` (installs `hkpSimpleShapePhantom`).
3. `0x00c21b20(3, 0, 0)` is called on it.
4. **World add `0x00c18930`**.
5. The shape reference is released.

| Phantom | Shape | Constructor | Stored at |
|---|---|---|---|
| 1 | sphere, radius **1.0** | `0x00d50670` | `0x01617654` |
| 2 | box, half-extents **(1, 1, 1)**, convex radius from `0x0131e81c` | `0x00d4d7f0` | `0x01617650` |

- `0x029c0640` is a global (**HYPOTHESIS:** an identity transform).
- `0x80002` is the phantoms' collision-filter word.
- **HIGH:** these two are the overlap-query probes used by `0x00757ec0`, `0x007581b0` and `0x0075a480`.

**Not seen in bring-up.** The contact listener is not added here. It is added inside the world wrapper (§2.2). The constraint filter's listener is removed at shutdown (§2.5), but **its matching add is not in any bring-up body** (OPEN: Havok may do it inside the world constructor).

### 2.2 World construction — `0x00c1c4f0` — CONFIRMED (cited)

The identity of `0x00c1c4f0` as the `hkpWorld` constructor is already CONFIRMED, and folded into `spec-physics-format.md` §3. It is cited from there, not re-derived. That entry also covers the derived-class wrapper `0x007572f0` and the two installed vtables.

This pass adds the call contract, read from the wrapper.

**`0x007572f0` (34 instructions, `this` + 2 arguments).** In order, it:
1. calls **`0x00c1c4f0(info, 20100200)`** on its own block;
2. installs the `game_havok_world` vtable `0x011539e8` at +0;
3. installs the `custom_collision_listener` vtable `0x011539c8` at **+0x360**, so the contact listener is a member of the world object;
4. calls **`0x00c18cb0(world, world+0x360)`**.

- **The second argument.** It is the 32-bit value `0x0132b468`, which is **20,100,200 in decimal: Havok's SDK version number for 2010.2.0**. It matches the `hk_2010.2.0-r1` version string. Ghidra presents it as a pointer to an unrelated RTTI string, which is wrong. CONFIRMED value. HIGH meaning.
- **`0x00c18cb0`:** HIGH, it adds the contact listener to the world.
- **Size.** The world object is 0x370 bytes: Havok's world plus the 0x10-byte listener member.

### 2.3 The per-frame driver — `0x0075ac20` — CONFIRMED

**Caller.** The game frame `0x00702a50` calls it once per frame, at `0x00702c46`. The surroundings, in order:
1. `0x00ac1950`, vehicle frame work, runs earlier in the same frame (`0x00702b9a`).
2. `0x00dc19a0(2, 2)` and `0x00dc19a0(3, 2)` set the work mode of scheduler worker slots 2 and 3 (and wake them).
3. `0x005fb4c0` runs.
4. The frame time `0x0132a0ac` is saved, and **`0x0075ac20` is called with no arguments**.
5. `0x0099e7e0(saved frame time)` and `0x00594bf0` run.
6. `0x00dc19a0(2, previous value)` and `0x00dc19a0(3, 1)` set the worker modes back.

**HYPOTHESIS:** the mode changes let workers 2 and 3 take physics-class jobs only for the length of the physics frame.

**Correction, and the most important finding of Part 2: the "1/30 s constant" is the game's frame time, and by default physics takes one variable step per frame.**
- **`0x0132a0ac` is not a constant.** Its static value is 1/30. The game frame timer **`0x00db9970`** rewrites it every frame (4 write sites), and the timer reset `0x00db97e0` resets it to 1/30. There are 395 reads in 208 functions.
  - The timer measures wall time and clamps it to [`0x0132a0c0`, `0x0132a0c4`].
  - It then divides by a game time scale `0x0132a0bc` (static 1.0).
  - So **`0x0132a0ac` is the game's frame Δt**.
- **The flag `0x012f8c1e` decides the step mode, and its static value is 1.** The scoping note took it for a debug single-step flag, which reads it backwards:
  - **1:** one physics step per frame, with Δt = frame Δt;
  - **0:** a fixed-step accumulator.
- **Who can change the flag.** Its only writer is the console handler `0x00754c30` (`Single Step is %s`, TRUE/FALSE/toggle). **Nothing references that handler:** Ghidra lists no reference, and no dword in the file equals its address. All 10 raw references to the flag's address are accounted for: 6 in the handler and 4 in readers (`0x007553c0`, `0x00755410`, `0x00756790`, `0x0075ac20`).
  - **HIGH:** in the shipped build the flag stays 1, so **physics always takes exactly one step per frame of length frame Δt.**
- **Clamps set at boot.** In the game set-up function `0x005d1a30`, at `0x005d21ea`, the clamps are chosen from the flag: [1/120, **0.05**] when it is 1; [1/120, 1/15] when it is 0.
  - **So the default physics step Δt lies in [1/120 s, 1/20 s]**, divided by the game time scale.
  - The console handler swaps between the same two clamp pairs (saving and restoring them through `0x012f8d10`/`0x012f8d14`, static 1/120 and 1/15).

**The driver, in order:**
1. **Sub-step cap.** cap = round(N × k), where:
   - N = `0x012f8c18` (unsigned, static **3**, read only here and in `0x00756790`);
   - k = `0x012f8c14` (static **1.0**);
   - the rounding is +0.5 then truncate, in `0x00dad930`.
2. **Enter the physics window.** **`InterlockedIncrement(0x0161d394)`**. This is **an in-progress counter, not a critical section** (CONFIRMED by import resolution: `0x0101c300` = `InterlockedIncrement`, `0x0101c1b4` = `InterlockedDecrement`). See "the physics window" below.
3. **Pre-step, inside the window:**
   - **`0x0079a380`**, water/buoyancy. It asks `0x00756790` (below) how much time this frame will simulate, then runs the water updates (`0x0078deb0`, `0x00796f60`, `0x00797fd0`, and `0x0079a150` for each listed object). It makes no direct Havok call.
   - **`0x00785cb0`**, ragdoll. Its direct Havok calls are the world method `0x00c1bc10`, `0x00c1f160` and `0x00c150c0`.
   - the no-op stub.
4. **Choose the step.** The time stepped this frame, `0x0161d3b0`, is set to 0. The job count J is read from `0x012f8c20` (static **2**; only reader).
   - **Flag set (default):** Δt = frame Δt, and the accumulator `0x0161d3ac` = frame Δt.
   - **Flag clear:** Δt = `0x0161d380` / k, and the accumulator += frame Δt. `0x0161d380` is set once, by `0x007553f0` from `0x005d1a30`, to **1/30**.
5. **Sub-step loop.** While Δt ≤ accumulator and i < cap:
   1. vehicle pre-step **`0x00ac3f40(Δt)`**;
   2. stub;
   3. **the step `0x00755c80(Δt, J)`**, with **i passed in EAX**;
   4. stub;
   5. stepped time += Δt; accumulator −= Δt; i += 1.

   Default mode therefore runs exactly once.
6. **Cap reached.** If i = cap, the accumulator is reduced modulo Δt by a two-argument CRT intrinsic, `0x00ea582a` → `__cintrindisp2`. HIGH: this is `fmod`. It is reachable only in fixed mode.
7. **Bookkeeping.** `0x012f8c2c` = the frame counter `0x029f4da4`, which marks "stepped this frame" for `0x00756790`. Then **k is reset to 1.0**.
8. **Post-step, still inside the window:**
   - `0x0077c9d0(0x00756790())`. Game-side, no direct Havok call. HYPOTHESIS: it hands the stepped time to gameplay.
   - **`0x00755bb0`**, the deferred activation queue (see "the physics window").
   - **`0x00758f00`**, the deferred body-state queue.
   - **`0x007699a0`**, game-side with no direct Havok call; it calls into the contact-listener cluster (`0x007677c0`, `0x00767d90`, `0x00768060`, `0x00768840`). HYPOTHESIS: contact events queued during the step are dispatched to gameplay here.
9. **Leave the window:** `InterlockedDecrement(0x0161d394)`.
10. **After the window:**
    - **`0x0075abd0`** replays the deferred queue at `0x0160c750` (count `0x0161d3a0`) through `0x0075a060`, which makes eight direct Havok calls including the world methods `0x00c16840` and `0x00c16990`.
    - It then tail-jumps to **`0x00779b50`**, Havok memory upkeep. If `0x0171acc4` is set and the game timer `0x012fbfc8` has reached 30,000, it resets the timer and calls the memory-system object (`0x00bec640`) at slot +0x3c. HYPOTHESIS: a periodic garbage collect.

**Related accessors (CONFIRMED, all tiny):**

| Address | What it does | Callers |
|---|---|---|
| `0x007553b0` | returns "physics in progress" (counter non-zero) | 8 gameplay callers |
| `0x007553c0` | returns the current step length (frame Δt in default mode, else `0x0161d380` / k) | called twice by the frame function `0x00702070` |
| `0x007553f0` | sets the fixed step | — |
| `0x00755400` | sets k | its only caller is the vehicle function `0x00a86620`, reached from `0x00ac1950`, which sets **k = 2.0** under conditions not traced (HYPOTHESIS: vehicles asking for finer sub-steps); it has an effect only in fixed mode |
| `0x00755410` | returns the flag | — |
| `0x00756790` | **predicts this frame's physics time**: before the frame steps, min(floor((accumulator + frame Δt) / Δt), cap) × Δt (`0x00ea4e80`, HIGH: `floor`); after, the time actually stepped | 10 callers (buoyancy, vehicles, gameplay) |

**The physics window: deferred and dropped mutations (CONFIRMED).** While the counter `0x0161d394` is non-zero, the game's own body-mutation wrappers do not call Havok directly:
- **`0x00755e10`** (17 callers) is the activation wrapper.
  - Outside the window it calls `0x00c150c0(entity)` and then `world.0x00c19330(entity, 0, 1)`.
  - Inside the window it appends the entity, without duplicates, to the list at `0x01617858` (count `0x0161d38c`). `0x00755bb0` replays that list in step 8.
- `0x00758f00` replays a second list of body-table indices (`0x01617658`, count `0x0161d390`). For each, it calls `0x00c14cc0` and, if that returned false, `0x00c150c0`, then `0x00c1eca0(1, 1, 0)`.
- **`0x00755ea0`, `0x00759ee0` and `0x00759f80` simply do nothing inside the window.** Outside it, each passes a vector to `0x00c1f190`, `0x00c1f1f0` or `0x00c1f160` on the body in a body-table slot. HYPOTHESIS: position or velocity setters.
  - **HIGH:** any request of that kind made during the step is lost, by design.
- **Contract for a replacement:** world mutation from game code is forbidden between the counter's increment and decrement. The game queues some requests and drops others.

### 2.4 The step — `0x00755c80` — and the job fan-out — CONFIRMED

**Inputs:**
- Δt (float, stack);
- J, the job count (stack; = 2);
- the sub-step index i in **EAX** (a register argument, set by the driver);
- world `0x0161d378` and job queue `0x0161d388` from globals.

**In order:**
1. **Begin, crossing into Havok.** Call **`0x00c17920`** with the world as `this`, the job queue and Δt.
   - The call shape is confirmed (`this` + 2 stack arguments, callee pops 8).
   - HIGH: Havok 2010's public `hkpWorld::initMtStep(jobQueue, Δt)`.
   - Only this call site exists.
2. **Describe a game job, game side.** A 40-byte descriptor is built on the stack:
   - +0x00 = the worker entry **`0x00754dd0`**;
   - +0x0c = (i << 16) | job index;
   - +0x10 = **3**, the job class;
   - +0x18 = the calling thread's id (`GetCurrentThreadId`);
   - the rest zero.
3. **Open a job group:** **`0x00dc2160("…\havok\havok.cpp", 3426, frame serial 0x029f4dac)`**. The record comes from the scheduler pool `0x0132a168`. The source path and line are diagnostics.
4. **Submit J jobs:** **`0x00dc21f0(0, &descriptor, group)`**, J times.
   - Each call copies the descriptor into a record from pool `0x0132a148` and stamps a per-frame sequence number (`0x029f56a8`).
   - It attaches the record to the group through the group's vtable +0x04.
   - With no explicit target, it queues the record on class queue 3 of `0x02a07100` and wakes a suitable worker (`0x00dc1b40`).
5. **Drain on this thread, crossing into Havok.** **`0x00be88c0`** with the job queue as `this`: it loops "get next job" (`0x00be88a0`) and dispatches each through the queue's per-type function table at queue +0x128. HIGH: `hkJobQueue::processAllJobs`.
6. **Finish, crossing into Havok.** **`0x00c16fc0`** with the world as `this`, the job queue and **0**. HIGH: `hkpWorld::finishMtStep(jobQueue, null thread pool)`. Only this call site exists.
7. **Wait for the game jobs:** **`0x00dc22c0(group, 2)`**.
   - It finds which of three known scheduler threads is calling (`0x029f5694`, `0x029f5690`, `0x029f5698`).
   - It sets that slot's work mode (the same field `0x00dc19a0` writes) to 2.
   - It waits by calling the group's vtable +0x10.
   - It restores the mode and returns the group record to its pool.

**The worker job `0x00754dd0`** is a 6-instruction entry point that Ghidra never made a function; it is reached only as data from `0x00755cd3`. In order it:
1. looks up its scheduler slot (`0x00dc1960`);
2. calls the stub;
3. calls **`0x00be88c0(job queue)`** (the same drain as step 5);
4. calls **`0x0077a280`**, per-thread Havok memory upkeep. With the same 30,000 timer test as `0x00779b50`, it calls the memory system at slot +0x34 with this thread's router from TLS;
5. ends with the stub.

**So, per sub-step, Havok's job queue is drained by the main thread plus J = 2 game worker jobs**, at most 3 threads. Havok's own thread pool is never used:
- no `hkJobThreadPool`-family class has RTTI in the image;
- `finishMtStep` is given a null pool.

**The vehicle pre-step `0x00ac3f40` also uses the scheduler (CONFIRMED; vehicle internals not opened).** It walks the three vehicle lists at `0x0130dfc4`–`0x0130dfcf`. For each vehicle that has all of the following:
- flag +0xbd0 = 1;
- a Havok body at +0x81c, for which **`0x00c14cc0`** returns true;
- non-null +0x808 and +0x7fc;

it does three things:
1. calls `0x00a8f9b0(Δt, vehicle)` for each sub-object of type 0x16;
2. calls `0x00ab9d80(vehicle)`;
3. collects the vehicle-instance pointer at +0x808, up to 48 of them.

Then, for each collected instance:
- **`0x00c561d0(instance)`** (`this` only). Four Havok vehicle-module functions (`0x00c58b80`, `0x00c5ab30`, `0x00c5b0b0`, `0x00c5b7f0`) are its other callers, so **the game does the Havok vehicle manager's per-vehicle job itself**. HIGH. This corrects the scoping note's "vehicle manager `0x00c561d0`": it is a per-instance call. HYPOTHESIS: the pre-collision update.
- then **`0x00ac1f80(instances, count)`**, which fans out one scheduler job per vehicle (entry `0x00ab9d40`, class 3, user word = the instance, group tagged `vehicle_system.cpp` line 657) and waits for them.

All of this happens inside the physics window and before `initMtStep`. **For a replacement:** the vehicle pre-collision work is driven by the game, per vehicle, in parallel, before each step.

**Per-thread Havok memory (CONFIRMED). This corrects scoping note §1.1's "job thread pool".**
- Bring-up's **`0x00779f00`** submits one class-3 job per active scheduler worker (`0x00dc12e0` counts the slots whose flag +0x40 is set, out of 4 slots of 0x114 bytes at `0x029f56b0`). The job entry is `0x00779cf0`.
- Each job runs **`0x00779ba0`**, which does five things:
  1. takes a ticket with `InterlockedExchangeAdd` on `0x0171acc8`;
  2. builds a Havok memory router in table `0x01715190` (stride 0x3c) with Havok `0x00be5e60`, recording its thread id in `0x0171516c` + 8·n;
  3. calls the memory system's slot +0x0c with (router, the string `hkCpuJobThreadPool`, 3);
  4. installs the router for the thread (`0x00bec720`);
  5. marks the scheduler slot (+0x41 = 1).

  The job then spins (`0x00dc4440`) until every worker has arrived.
- **`0x007acfe0`** has no direct caller. HIGH: it is another thread's entry point. It does the same set-up for its own thread, also labelled `hkCpuJobThreadPool`.
- **So that string is only the name Havok's memory system is given for each thread. No thread pool object exists** (HIGH: no RTTI for one; CONFIRMED that neither function constructs anything but a memory router).
- Shutdown mirrors it: `0x00779fa0` → job `0x00779d30` → `0x00779c70` (memory system slot +0x10, `InterlockedDecrement`, clear the slot mark).

### 2.5 Shutdown — `0x007590d0` — CONFIRMED call sequence

Called once, from the game shutdown routine `0x005d39e0` at `0x005d3a60`. It does nothing unless `0x0161d39f` is set.

| # | | What happens |
|---|---|---|
| 1 | G→H | Stub. Then `0x0075fb10(0)`: for each of the 5,000 pool records at `0x0163f720` holding a Havok object at +0x08, release it (`0x00be6100`) and return the slot to the free list (`0x0075f9b0`). |
| 2 | H | **World removal `0x00c177b0`** of phantom 1 (`0x01617654`), then release; the same for phantom 2 (`0x01617650`). |
| 3 | H | If the constraint filter exists, **`0x00c16ac0(world, filter + 0x40)`**, which detaches the filter's constraint-listener sub-object (HIGH: remove constraint listener). The filter itself is not released here; the world owns the last reference. |
| 4 | G | `0x0161d3a8` (border) is cleared without a release, since the world owns it. `0x0161d384` is released if non-null. It is never written non-null anywhere: a dead slot. |
| 5 | H | **Job queue:** `0x00be8aa0` (HIGH: its destructor), then the 0x290-byte block goes back to the thread heap (router +0x2c, slot +0x08). `0x0161d388` = 0. |
| 6 | H | **World:** release (`0x00be6100`), `0x0161d378` = 0. The world's own teardown is assumed to release the contact listener, filter, border and watchdog through their world-deletion paths (HIGH). |
| 7 | H | Suspend-inactive-agents utility: release, `0x0161d37c` = 0. |
| 8 | G→H | Stub. **`0x00779fa0`**: per-worker Havok memory quit (§2.4). **`0x0077a350`**: Havok base-system quit `0x00becb90`; the memory-system objects at `0x0171acc4` (slot +0x08 with 3, then its deleting destructor) and `0x0171acc0` (deleting destructor); the three per-thread router entries at `0x01715170` reset; the ticket counter `0x0171acc8` = 0. |
| 9 | G | `0x0161d39f` = 0. |

- **Order contract:** game pool → phantoms → listeners → job queue → world → utility → per-thread memory → base system.
- The job queue is destroyed **before** the world.

### 2.6 The frame contract a replacement engine must fit (summary)

1. **Bring-up, once:**
   - memory and base system;
   - one world: gravity (0, −9.8, 0); broadphase box (−3500, −500, −3500)–(3500, 1500, 3200); collision tolerance 0.04; default convex radius 0.005; multithreaded;
   - a collision filter chain: Havok's constraint filter wrapping the game filter;
   - the contact listener, a member of the world;
   - a broadphase-border handler and the suspend-inactive-agents utility, both game subclasses;
   - a memory watchdog;
   - two 1-metre query phantoms (sphere and box);
   - per-thread memory on every game worker.
2. **Per frame, between the two interlocked calls:**
   - water pre-step, ragdoll pre-step;
   - then once (shipped default) or up to round(3·k) times (fixed mode): vehicle pre-step (per-vehicle Havok call + parallel game jobs), then **one multithreaded step of Δt**: begin(queue, Δt); 2 game worker jobs + the main thread drain the queue; finish(queue, none); wait;
   - then the deferred activation/body-state queues and contact dispatch.

   Δt = the frame time clamped to [1/120, 1/20] s in the shipped build.
3. **After the window:** replay the deferred body operations, then periodic memory upkeep.
4. **Game rule:** no world mutation from game code inside the window. Requests are queued or dropped.
5. **Callbacks during the step** come only through the §1.2 slots and the door-motor pointer. Most of them run on worker threads, so **the game callbacks must be thread-safe**. HIGH: Havok's multithreaded step runs actions, filters and listeners inside jobs.

### 2.7 Corrections to the scoping note (§1.1, §1.2, §2, §3.3–§3.4)

| Scoping note says | This pass |
|---|---|
| critical section `0x0161d394` around the physics frame | an **interlocked in-progress counter**; game code tests it to defer or drop body mutations (§2.3) |
| `0x0132a0ac` = 1/30 s constant, a fixed step | `0x0132a0ac` is the **game frame Δt** (static 1/30, rewritten every frame by `0x00db9970`). **The shipped build takes one variable step per frame**, Δt ∈ [1/120, 1/20] s. The 1/30 fixed step (`0x0161d380`) exists only behind a flag whose sole writer is an unreferenced console handler (§2.3) |
| `0x012f8c1e` = debug single-step flag | the step-mode flag, **static 1 = one variable step per frame** |
| `0x012f8c18` = 3, HYPOTHESIS sub-step cap; `0x012f8c14` = 1.0 | confirmed: cap = round(3 × k), with k = `0x012f8c14` (reset to 1.0 every frame; set to 2.0 by vehicle code). Used only in fixed mode |
| vehicle pre-step drives "Havok's vehicle manager `0x00c561d0`" | `0x00c561d0` is a **per-vehicle-instance** call that the game makes in place of Havok's manager, followed by a game job per vehicle (§2.4) |
| job thread pool created by `0x00779ba0` and `0x007acfe0` | no thread pool exists. Both set up **per-thread Havok memory**, using `hkCpuJobThreadPool` only as a thread name (§2.4) |
| `0x00c150c0` / `0x00c14790` = world lock/unlock pair | both are called on bodies (or a body sub-object at +0xe0), never on the world (§1.3) |
| step = begin, game scheduler fan-out, drain, finish, wait | confirmed in that order. The begin/finish arguments are (queue, Δt) and (queue, 0); J = 2 jobs; the sub-step index travels in EAX into the job tag (§2.4) |
| bring-up "installs the game's filter and listeners" | the game filter is wrapped by Havok's `hkpConstraintCollisionFilter`; the contact listener is added in the world wrapper, not in bring-up; there is also a memory watchdog, a border handler, the suspend-agents utility and two query phantoms (§2.1) |
| world ctor second argument unknown | the Havok SDK version number 20100200 (§2.2) |

---

## Part 3 — Open items

1. **Where the constraint filter's listener is added.** Shutdown removes it (`0x00c16ac0`), but no bring-up body adds it. It may happen inside Havok's world constructor. Only the observable effect matters for a replacement: constrained pairs do not collide.
2. **Construction-info field names** (§2.1 steps 4, 5 and 10). The offsets and values are CONFIRMED; the field names are HYPOTHESIS from Havok 2010's public header order. Most open: the byte at +0xe8 = 1, the solver-preset argument 5, and the job-queue fields 1 and 21. These matter only to someone mapping the values onto another engine's world settings.
3. **When vehicle code sets the sub-step multiplier k = 2.0** (`0x00a86620`, tests through `0x00d9e4c0` / `0x00d9e1e0`). It has no effect in the shipped default mode. P4.
4. **Post-step roles of `0x0077c9d0` and `0x007699a0`.** The likely reading is contact-event dispatch and handing the stepped time to gameplay. Both are game code with no direct Havok call. P3.
5. **Which callbacks run on worker threads**, and so must be thread-safe in a port. HIGH that actions, filters and listeners run inside Havok jobs. P3/P4.
6. **The 41 small functions Havok calls outside the ranges** (§1.3). They are taken as folded or header-inline shared code as a class. Only `0x007557e0` and `0x00756cb0` were identified individually.
7. **`0x00769c30`** (22,979 instructions), called in bring-up with the game filter. Not opened. P2/P3.
8. **The unattributed range `0x00d1d570`–`0x00d4c800`** (§1.1). Still unidentified, and out of scope.

## Part 4 — Notes for whoever folds this in (no spec was edited)

- **`spec-physics-format.md` §3.** Nothing there changes. One small addition could be raised for review:
  - the world wrapper passes **20100200**, Havok's SDK version number for 2010.2.0, as the constructor's second argument (§2.2);
  - this is a further, code-level confirmation of the version identification in its §1.

  This pass did not re-examine the scoping note's point (a) about `0x00bf81a0`.
- **`spec-physics-format.md` §4.4.6 and `spec-vehicle-geometry.md`.** Both cite the global `0x0131e81c`. It can now be described as **Havok's default convex radius, static 0.05, set to 0.005 by physics bring-up** (HIGH, §2.1 step 8). So any box shape built after bring-up gets a 5 mm radius.
- **A future physics spec, P1 section.** It should take §2.6 as its frame contract. It must state plainly that **the shipped build steps physics once per frame with a variable Δt ∈ [1/120, 1/20] s**, and that the 1/30 s fixed-step path is an unreachable alternative. This changes the scoping note's §5 item 2 rationale ("the 1/30 s fixed step").
- **The scoping note itself** carries the errors listed in §1.2 (the corrections list), §1.3 and §2.7. It is a review document, so this note does not edit it.
- **For P2:**
  - `0x00d50670` = `hkpSphereShape` constructor and `0x00c1e1d0` = `hkpSimpleShapePhantom` constructor (both CONFIRMED by vtable);
  - the body handle is an index into the 5,000-slot table `0x0161d400`;
  - the game keeps data in Havok body properties (`0x00756cb0`, HIGH);
  - the filter object's layout starts at `0x00778b30`.

## Part 5 — Clean-room check and housekeeping

- **Naming.** Addresses are plain hex. The class names are RTTI type-descriptor strings read from the binary, or Havok's public 2010.2 API names, quoted as data. Volition source-file names are quoted from shipped diagnostic strings. No decompiler output or pseudocode is reproduced. Every behavioural description is my own wording from listings and disassembly. The Havok keycode is not reproduced.
- **Havok internals.** None were read beyond the call boundary. The only look inside a Havok function was to see which vtable its first instructions install (for class identity), or one constant store (the sub-object offset at `0x00c2b38d`). The vehicle custom sub-steps (`0x00ae98f0`, `0x00ae9570`, `0x00ae7470`, and the per-vehicle job `0x00ab9d40`) and `.czn_pc` were not opened.
- **Auto-name scan.** The project's standard auto-name regex (decompiler variables, stack slots, register-derived names, label/function/data auto-names, undefined types, parameter names) was run over this file. A plain-substring backstop for the same families covers the one known regex gap: a register-derived name whose register part itself has an underscore.
  - Results:
    - **Python `re`: 0 hits.**
    - **ripgrep: 0 matching lines** (exit status 1, "no match").
    - **Backstop: 0 hits** in each of its 10 families.
  - **Positive control, same scanners, same run.**
    - A control line holding eight planted auto-names matched **7 by the regex**, missing only the underscore-register case, which is the known gap.
    - It matched **8 by the backstop**, one per planted name. Every planted family fired. The two families at 0 had no planted example.
  - So the zeros above are real.
  - The scan was re-run after this section was written; the result is recorded at the end of the file.
- **No spec file was edited.** Only this note was written.
- **Private Ghidra copies.** `tools\gp_physA0` and `tools\gp_physA0b` (left behind by the interrupted session) were both checked intact: same 10 files and 488,965,201 bytes as the shared project, and no lock file. Neither was needed again, because the earlier dumps sufficed. **Both were deleted at the end of this unit**; a check afterwards confirmed both paths are gone. The shared project at `tools\ghidra_projects` was not touched (still 10 files, same byte count).

**Final scan of the finished file.** It was run after Part 5 was written, and again after this table was added; both runs gave the same result. The positive control fired in the same run, with the same 7 regex and 8 backstop matches as above.

| Scanner | Hits |
|---|---|
| Python `re` | **0** |
| ripgrep, regex | **0 lines** (exit 1) |
| ripgrep, plain-substring backstop | **0 lines** (exit 1) |
| Python backstop, each of the 10 families | **0** |
