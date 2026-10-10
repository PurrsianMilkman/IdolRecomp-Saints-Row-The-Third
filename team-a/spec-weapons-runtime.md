# Saints Row: The Third — Weapons Runtime: Firing, Hit Detection, Damage Application, Ammo (`SaintsRowTheThird.exe`)

**Scope.** The runtime (CODE) half of the weapon system — hit detection, damage application, fire-rate
gating, and ammo tracking — as distinct from `spec-tables-weapons-combat.md`'s DATA (XTBL loader/schema)
side, which this file cross-references but does not duplicate. Also distinct from
`spec-player-runtime.md` §15 (the player-specific aim-assist/melee-trigger/death-respawn material) and
`spec-physics-runtime.md` §8.8-§8.11 (the generic Havok on-demand collision-query wrappers this file's
own hit-detection consumers reuse).

**Method.** Ghidra-free throughout (`pefile`+`capstone`, following `tools/harnesses/exe_va.py`'s PE/VA
mapping convention — faster than the shared, busy persistent Ghidra server). No leaked Volition/Havok/
Audiokinetic/RAD material. Addresses are plain hex; `FUN_`/`DAT_` never used as if real identifiers.

**Confidence key:** **CONFIRMED** (read directly in disassembly), **HIGH** (solid structural inference,
not itself a literal instruction-level read of the exact claim), **HYPOTHESIS** (plausible, materially
uncertain), **OPEN** (not resolved — reported as a gap, not guessed).

## 1. A shared entity-type capability table, `0x2cc9900`

A candidate game object `X` carries a one-byte type/category id at `X+0x34`. Both hit-detection
consumers below (§2) index a dword-pointer array at `0x2cc9900` with that byte (`[0x2cc9900 + id*4]`) to
get a small per-type attribute record, then test bits inside it:

| Field | Bit | Meaning (as used by a confirmed consumer) |
|---|---|---|
| `+6` | `0x08` | "damageable" — gates whether either consumer even attempts to resolve this candidate |
| `+6` | `0x80` | "is a vehicle" — branch dispatch |
| `+0xa` | `0x04` | "is a character" — branch dispatch |
| `+6` | `0x20`, `0x01`, `0x40`; `+0xa`; `+9` | further per-type flags, combined into ≥4 boolean outcomes — individual roles OPEN |
| `+7` | `0x01` | gates whether a *previously cached* target is worth releasing/notifying-about at all (seen in `0x005922e0`/`0x009270e0`, §2.2a) — HYPOTHESIS: "was ever a valid damage/interaction target," a coarser gate than `+6`/`0x08` |
| `+0xa` | `0x01` | gates a UI-classification/aim-assist-eligibility branch (seen in `0x00927030`/`0x00b87680`, §2.2a) — HYPOTHESIS: "eligible for aim-assist/lock-on," distinct from the `+0xa`/`0x04` "is a character" bit |

This is a game-side RTTI/type-id capability table, analogous in purpose to Havok's own collision-layer
concept but implemented entirely in game code (a flat array indexed by a type-id byte, not a vtable and
not a Havok collision filter) — the same general shape `spec-physics-runtime.md` §8.9 found for its own,
narrower, query-local filter `0x0075b780`. `0x2cc9900` is a different, earlier-stage filter: it runs on
every resolved hit candidate regardless of caller, before any caller-specific mode byte is consulted.

## 2. Hit detection

### 2.1 The shared candidate-resolution chain (CONFIRMED, both consumers below)

Both hit-detection consumers, after getting a raw hit record back from a Havok wrapper
(`spec-physics-runtime.md` §8.8), run this identical sequence to turn it into a damageable game object:

1. Read a 64-bit handle pair at hit-record `+0x38`/`+0x3c`.
2. Resolve it via `0x00458230(this=0x024433a8, &handle_pair)` with selector `0x031d152c` — a generic
   keyed-object-store lookup. Null/zero result → rejected.
3. Reject if `candidate+0x33` bit `0x10` is set (exclusion flag).
4. Look up `0x2cc9900[candidate+0x34]` (§1), require attribute bit `+6`/`0x08` ("damageable").
5. Call the liveness gate `0x00853b10(candidate)` — the exact gate `spec-lua-api-behaviour.md` §3.13
   documents at all four of `on_take_damage`'s own hook-registration branches. Reject if false.
6. Call `0x0091f5e0(candidate)` — a "valid target for this attacker" predicate, also reused by the
   vehicle-destruction function `0x00a9d6c0` (§2.4).

Both raw hit-record arrays are walked with the same `0x50`-byte stride in both consumers — a new data
point about the shared wrappers' caller-visible per-hit record shape (`spec-physics-runtime.md` §8.8 only
documented the shape-cast wrapper's own *internal* buffer strides, `0x60`/`0x2c` — not reconciled against
this `0x50` figure, flagged for whoever next opens either wrapper's body).

### 2.2 `0x00b86b20` — a hitscan ray-cast consumer that is a debug/target-info overlay, not the fire-damage path

Full body read (504 instructions, single clean exit). Resolves the "current context" object via
`0x009da4e0()` (§5.3), calls `0x00941f40(context)` to read its result's `+0x19c` field — **the same
convention `spec-lua-api-behaviour.md` §4.10/§4.11 use for "the equipped weapon-definition pointer"** —
then builds a ray-cast scaled by that pointer's own `+0x120` field, **exactly the weapon record's
`Range_Max` offset** (`spec-tables-weapons-combat.md` §2.2). Calls `0x0075d7e0` (the shared ray wrapper)
directly, runs §2.1's resolution chain, picks the first passing candidate. On a hit, branches on the
`0x2cc9900` type attributes into a "character" display path (time-to-kill/health-ratio, `0x009715d0`/
`0x00971610`) or a "vehicle" display path (`0x00a77e20`/`0x00a77e10` — **new finding: HIGH confidence
these are the vehicle current-HP/max-HP accessors**, divides and displays a percentage). Every branch
ends by calling a formatted on-screen debug-text printer, `0x00754410`. **It never calls `0x0096fa20`
(§4).** It is a read-only "what is my crosshair aimed at" debug/dev overlay reusing the exact machinery a
real fire-damage consumer would need. Zero static call sites found anywhere — consistent with being
reached only through an indirect/function-pointer debug-draw registry, not identified.

**Honest gap:** this demonstrates the hitscan *targeting* path end-to-end but stops short of applying
damage. The actual fire-trigger function that performs this cast and calls `0x0096fa20` was not located
in the original pass. **A dedicated follow-up pass (2026-10-08) opened `0x00b865f0` and traced its full
negative space — see §2.2a below: the gap is now far more precisely bounded but still OPEN.**

### 2.2a Follow-up: `0x00b865f0` and its neighbourhood fully traced — still not the fire-damage path (2026-10-08)

**`0x00b865f0` is CONFIRMED NOT the hitscan fire-damage function.** Full body read (261 instructions):
it is a ray-cast + impact-FX/UI dispatcher — resolves a *cached* target-handle pair already stored on its
own `this` object (`this+0xf0`/`this+0xf4`), runs only a **partial** 4-of-6-step version of §2.1's chain
(it never calls the final `0x0091f5e0` "valid target" check), then routes to `0x00594460` — independently
fully traced (522 instructions, 23 callers across wildly different subsystems including the generic
despawn sweep `0x0099ebe0`) and confirmed to be a generic impact-FX/sound dispatcher, never calling
`0x0096fa20` either.

**Both real callers of `0x00b865f0`** — `0x009270e0` and `0x005922e0` — were fully traced and together
map a real "per-shot fire orchestration" sub-system: caching a global "current target" handle pair
(`0x116fae0`/`0x116fae4`) onto the weapon holder, setting an "already fired this cycle" flag
(`+0x200` bit `0x4`), playing hashed-ID muzzle FX/sound (`0x50d893ff`/`0x5ae33e86`), conditionally calling
`0x00b865f0`, then reading a 16-bit per-weapon stat off `holder+0xdc → +0x374` to set a **second generic
timer instance at `holder+0x134`** (distinct from §3.1's `holder+0x1c4`) via `0x00d9e140` — see §3.1's
own update below. Neither calls `0x0096fa20`. The dispatch above them (`0x00929450` and a multi-exit
region at `0x0092969d` that calls `0x009270e0` five times, plausibly one per pellet/fire-mode) was also
checked end to end — zero calls to `0x0096fa20` anywhere in that region either.

**A targeted fingerprint search across all ~90 previously-unattributed `0x0075d7e0` callers** (matching
`0x00b86b20`'s own four-part signature: equip-holder + `+0x19c` + `+0x120` + the final `0x0091f5e0`
check) found three closer structural matches than `0x00b865f0`: `0x009cb3c0`/`0x009cbcf0`/`0x009d1b30`.
All three were fully read (1207/653/700 instructions) — dominated by calls into the confirmed
`weapons.xtbl` UI/stat address range and camera-matrix helpers, HIGH confidence a **weapon-inspect/
aim-camera HUD system** (a real shipped sibling of `0x00b86b20`'s own debug overlay, not fire-damage).
None call `0x0096fa20`. A parallel aim-assist/target-lead chain (`0x00b87680`→`0x00b872a0`) was also
fully traced and terminates in 3D positional smoothing via a vtable call — not damage.

**Net: the literal hitscan fire→damage call site remains OPEN**, but the negative space is now much
better mapped — 13 functions across three independent branches of the ray-wrapper caller tree were
opened, and every address traced was cross-checked against the damage sink's own 53-function caller
census with **zero overlap in either direction**: the hitscan-fire-orchestration neighbourhood mapped
here (`0x0092xxxx`-`0x0093xxxx`, `0x0059xxxx`-`0x005axxxx`, `0x00b7xxxx`-`0x00b8xxxx`) is address-space-
disjoint from where the damage sink's 53 known callers actually cluster
(`0x0060xxxx`-`0x0070xxxx`, `0x0096xxxx`-`0x009cxxxx`, `0x00a4xxxx`/`0x00a9xxxx`/`0x00abxxxx`/
`0x00afxxxx`/`0x00b0xxxx`-`0x00b2xxxx`). Named, not-yet-chased leads for a future pass: `0x00b05b90`/
`0x00b19ad0` (two damage-sink callers sharing a megabyte region with two originally-named neighbours,
but not themselves ray-wrapper callers — if weapon-related, they'd reach the sink via a non-ray-cast
path); the remaining ~86 unattributed `0x0075d7e0` callers that didn't match the fingerprint (a hitscan
function that, like `0x00b865f0` itself, doesn't read the weapon-definition record directly would be
missed by that search). Full detail: `scratchpad/hitscan_fire_damage_consumer_2026-10-08.md`.

**Both named leads closed, 2026-10-08 — neither is the hitscan fire-damage path, but for two different
reasons.** `0x00b05b90` is CONFIRMED NOT a hitscan function — it is the vehicle extract-state `0x1d`
("extract") per-frame handler's "kill" extract-kind branch, **already fully documented at
`spec-player-runtime.md` §11.4.2**, independently re-derived here with an exact match on every detail. It
reaches `0x0096fa20` via a per-seat, `0xd0`-byte-stride occupant-handle table resolved through
`0x004d7f30`, not a ray-cast; the weapon-definition read inside it exists only to **attribute** the kill
to whatever the resolved seat occupant has equipped, not to drive the damage computation — exactly the
vehicle-related, non-ray-cast path this item itself predicted.

`0x00b19ad0` is CONFIRMED NOT a hitscan function and is a **genuinely new finding**: a **vehicle-vs-
vehicle occupant crush/pancake kill**. For up to 2 cached "vehicle I am in contact with" handles on
`this` vehicle (`this+0x16f0`/`+0x16f8`), it walks `this`'s own 8 seats (`this+0x1358+0x30·seat` =
`spec-player-runtime.md` §10.1's documented seat-occupant field) and tests whether each occupant's
position now falls inside the contact vehicle's own bounding box (`0x00a7a550` fills the box, `0x4aac40`
reads its corners, a 4-lane `cmpltps`/`movmskps` "fully inside" test). On a hit: instant-lethal damage
(`-1.0`, the same literal constant the melee finisher and the vehicle-kill extract both use) via
`0x0096fa20`, **crediting the contact vehicle's own driver** as instigator, then despawns the corpse. **No
weapon-definition read anywhere** (`+0x19c`/`0x00941f40`/`0x0075d7e0` never appear) — a clean, complete
rule-out of weapon involvement. New territory for `spec-vehicle-damage-runtime.md`, not this file's own.

**The ~86-of-91 remaining unattributed `0x0075d7e0` callers: a broadened pre-filter (equip-holder,
`+0x19c`/`+0x120`/`+0x130`/`+0x110`/`+0x374` references, deliberately looser than `0x00b86b20`'s exact
4-part fingerprint) was run across all 91, and the top 8 candidates were fully opened — all confirmed
**zero** calls to `0x0096fa20`:**

| Address | Verdict |
|---|---|
| `0x007f6f90` | HUD cooldown-bar readout (refire-remaining ratio via `holder+0x1c4`'s timer ÷ `weapon_def+0x130`) — sibling of the weapon-inspect/HUD family |
| `0x009c8f40` | Reads `weapon_def+0x110`, the same field `0x00b87680`'s aim-assist/homing chain reads — sibling of the aim-assist/homing family |
| `0x004ffd30` | UI/targeting-eligibility helper (candidate flag bits + a weapon-stat threshold gate), resolved via a different resolver (`0x004fae00`) |
| `0x005004a0` | Weapon-inspect/zoom-camera-targeting consumer — the closest structural match of these 8 to `0x00b86b20`'s own family |
| `0x009c9c60` | Another weapon-inspect/HUD-family member (runs the shared resolution chain 5× in a candidate loop) |
| `0x009e4dd0` | A **new shape**: a weapon-pickup/ammo-compatibility eligibility predicate (classifies ammo type, checks a per-weapon pool index, compares two different `weapon_def`s) — not previously named in this file |
| `0x005b6af0` | Weapon-inspect/HUD-family member |
| `0x00925ff0` | Weapon-inspect/HUD-family member |

All 8 extend the already-known weapon-inspect/HUD-readout or aim-assist/homing families except
`0x009e4dd0`'s new ammo-eligibility shape. **The literal hitscan fire→damage call site remains OPEN** —
23 of the ray-wrapper's 91 callers are now opened across two passes (13 + these 8 + the 2 lead
addresses), ~70 remain genuinely unopened; a hitscan function that, like `0x00b865f0` itself, never
reads the weapon-definition record directly would still be missed by this pre-filter. Full detail:
`scratchpad/weapons_fire_damage_and_player_blip_2026-10-08.md`.

### 2.2b Why the fire→damage call site can never be found by static caller audit — the player-input side (2026-10-08)

**Re-bounds §2.2a's/§4.4's own OPEN item, rather than closing it.** The player's own on-foot attack-input
function **`0x005ae3f0`** (reads `CBA_OFC_ATTACK_PRIMARY`/`SECONDARY`, `spec-player-runtime.md` §2.7) —
previously flagged by §8 below as "the predecessor's unexplained 367KB lead" and read only from the
opposite direction (outward from a ray-wrapper-caller fingerprint match) to the verdict "a close sibling
of the fire path, not the fire path itself" — is now confirmed, read from the player-input side in full
(`0x005ae3f0`-`0x005aebad`), to BE the fire path's own entry point on this input. It does **not** reach
fire logic through a static call chain at all: it dispatches through an **indirect, polymorphic virtual
call**, `CALL dword ptr [[this]+0x8c]` on the currently-equipped inventory-slot record (resolved via the
already-documented `0x00941f40`/`+0x1b78` convention, §3.1), with the player object passed as the explicit
argument. CONFIRMED at the instruction level at two internal call sites — primary-weapon:
`0x005ae535`/`0x005ae53e` (vtable-slot load / `CALL EAX`); secondary/grenade-slot-10:
`0x005ae5b0`/`0x005ae5b9` — both thiscall, `this`=the item-slot record in ECX, player as the one stack
argument. Confirmed `0x00600380` is called immediately before each dispatch as a pre-flight "is the
current target/holder state still valid" check (touches 2 of §2.1's 6-step chain plus `character_is_dead`/
is-a-player — role HIGH confidence, not fully traced).

**Two further, independent sightings of the identical mechanism** confirm this is a real (if not yet
concretely identified) virtual method on the item-record class hierarchy, not a mis-read: inside a
previously unnamed function **`0x0073e310`** (call site `0x0073e5db`), which independently resolves its
item record via the same `0x00941f40`/`0x005fe3e0(this+0x1b78,1)` convention before the same
`[vtable+0x8c]` call — possibly the AI/NPC or vehicle-turret equivalent of this same dispatch, OPEN,
flagged for follow-up, not chased further; and inside the shared equip dispatcher `0x00600720` itself
(§9.19/§21.23 of `spec-lua-api-behaviour.md`) at `0x006011fe`/`0x00601205`. A whole-binary, Ghidra-free
`pefile` byte-pattern scan for this `MOV reg,[reg+0x8c]`+adjacent-`CALL`-same-reg shape found **56
confirmed sites** project-wide (of 447 raw `+0x8c` slot-load hits before the call-adjacency filter — the
slot index is evidently reused by many unrelated class hierarchies, so the raw count alone is not
evidence of anything). One raw-scan hit, `0x005ae331`, was checked and rejected as a byte-scan false
positive (empty Ghidra caller list, bogus `stackPurge`) landing mid-instruction in unrelated padding.

**Net effect on the OPEN item: because the dispatch is indirect, no amount of auditing `0x0075d7e0`'s
remaining ~68 static callers can ever find the concrete per-weapon-class fire implementation this way —
that implementation's identity depends on which concrete item-record vtable is installed when a given
weapon is equipped, which was not resolved this pass.** This replaces "which remaining `0x0075d7e0`
caller is the fire function" with a narrower, more tractable question: **which vtable(s) does the
equip/pickup-time item-slot-record constructor install, and what sits at their own `+0x8c` slot.** A
fresh fingerprint for that follow-up: search for `MOV dword ptr [reg], <imm32 in .rdata>` (a vtable
install) in functions that also write a byte-sized "weapon category" field matching
`spec-tables-weapons-combat.md`'s `Weapon_Class` encoding, near the already-known weapon-class dispatch
family (`0x00b81440`/`0x00b81600`/`0x00b81630`/`0x00b81680`/`0x00b816d0`/`0x00b81850`/`0x00b81870`/
`0x00b83a00`/`0x00b83b10`/`0x00b88610`/`0x00b88640`/`0x00b869b0`). Full detail:
`scratchpad/player_combat_aim_fire_reload_2026-10-08.md`.

### 2.2c The vtable resolved: `weapon::+0x8c` = `0x00b86030` — CONFIRMED (2026-10-08)

**§2.2b's own suggested fingerprint was tried and refuted: all 12 weapon-class dispatch functions are
tiny (0x1f-0x320 bytes) membership predicates on `weapon_def+0x4c`, not constructors.** Tracing the
actual equipped-item handle-resolution chain (`0x005fe3e0`/`0x005fec20`/`0x005fed90`/`0x008dc820`/
`0x005ff220`) shows every one bottoms out in the engine's one universal handle→object resolver
(`0x00458230`/`0x004f0400`) — there is no per-weapon-category constructor to find anywhere, **because
this engine uses exactly ONE concrete `weapon` C++ class for every category** (pistol, rifle, shotgun,
melee alike), with category differences handled entirely through DATA (`Weapon_Class` at
`weapon_def+0x4c`), not polymorphism — confirmed by a broad RTTI keyword sweep finding zero
further-derived classes (`firearm`/`gun`/`melee`/`thrown`/`grenade`/`rifle`/`pistol`/`shotgun`/
`explosive`/`rocket`/`launcher`/`knife`/`sword`/`fist`/`unarmed`/`ranged`/`pickup`, all zero hits).

**The route that worked instead: direct MSVC RTTI recovery of the real, compiled `weapon`/`item`/
`projectile` class vtables out of `.rdata`** (Ghidra-free, validated first against two already-known
RTTI'd classes). `weapon`'s real vtable (`0x0118b1ec`, 36 slots) derives from `item`'s (`0x0116e6cc`,
also 36 slots, confirmed by a shared inherited slot 3); `projectile`'s vtable (`0x0116fafc`) is only 32
slots and has no `+0x8c` slot at all, ruling it out. **`weapon::+0x8c` = `0x00b86030`, CONFIRMED as
the fire/use dispatch** — matching, at the instruction level, the exact indirect-dispatch shape §2.2b
already confirmed at the player-input call sites (reads `[esi+0x19c]`, the already-established
weapon-definition pointer, as its first act; one stack argument, the player).

**Body (CONFIRMED, full raw disassembly, `0x00b86030`-`0x00b86107`):** a vehicle-weapon short-circuit
(`Weapon_Class == 0x16` aborts immediately — vehicle-mounted weapons never fire through this dispatch
at all); a "refuse" gate (`weapon::+0x88`, `0x00b81390`, the same vehicle-class test, else tail-calls
`item::+0x88`, OPEN); delegation into `item::+0x8c` (`0x00902760`, the base "use" implementation,
below); the weapon-specific sound/FX dispatch (`0x009ba4a0`: dry-fire vs normal hashed sound
selection, 2 FX ids off `weapon_def+0x47c`/`+0x480`); and — **closing §6's own stated gap** — the
literal rounds-in-clip decrement at weapon-instance `this+0x1ac` (`add [esi+0x1ac],eax` at
`0x00b860e9`, clamped at zero; set to `0x7fffffff` for "infinite ammo," `this+0x1a0` bit `0x40`),
sourced from a per-character, per-ammo-type pool array (resolved via `0x00b6e9d0`, keyed by
`weapon_def+0x110` — a confirmed third consumer/firmer identity for that field) and consumed via
`0x00b6e300`'s clamp/subtract/floor primitive.

**`item::+0x8c` (`0x00902760`, ~370 instructions, CONFIRMED):** generic listener-notify broadcasts
(`0x008ccb90`, a linked-list walk); equip-status bitwork (`+0xa8`/`+0xa9`/`+0x1a1` family, already
documented); a gated aim-lead block (only when a target argument was supplied AND `this+0xda` bit
`0x20` is set) that calls **`0x0075df80`** — a newly-confirmed, previously-unattributed caller of the
shared ray wrapper `0x0075d7e0` (a 3-instruction kind-tag(`0x5a`) thin wrapper, tail-jump; 4 sibling
wrappers of the identical shape found alongside it, tags `0x59`/`0x57`, none previously in §2.2a's
23-opened-caller list) — role HIGH-confidence aim-assist/target-lead-shaped (gated on a pre-existing
locked target), **not confirmed as the base per-shot hit-detection ray**; the muzzle FX/particle spawn
(`0x007562c0`, checked and ruled out as a damage path — pure visual, no call to `0x0096fa20` or
`0x0075d7e0` anywhere in its body); and, **unconditionally, regardless of the aim-lead gate**, three
generic timer-primitive (`0x00d9e140`) writes on the weapon INSTANCE itself (`this+0xf4`/`+0xfc`/
`+0x104`) — a THIRD base object for this primitive beyond the two already-documented holder-based
ones (§3.1 below), with `+0xfc` set to a literal `1000`ms, a strong HIGH-CONFIDENCE candidate for the
real per-shot refire-delay gate.

**Honest remaining gap, precisely bounded, not a dead end from insufficient budget:** none of
`0x00b86030`/`0x00902760`/`0x009ba4a0`/`0x007562c0` call `0x0096fa20` anywhere in their traced bodies.
For an UN-assisted shot (no pre-existing target — the ordinary trigger-pull case), the function skips
the aim-lead ray block entirely and goes straight to muzzle-FX and ammo/timer bookkeeping, with no
ray-cast and no damage call anywhere in that path. Two honest readings, neither chased further:
(1) hit detection happens asynchronously, outside this synchronous call chain — a physical
projectile/ray armed here whose Havok collision-RESPONSE callback (not reachable by static tracing)
calls `0x0096fa20` on contact, consistent with this engine's on-demand collision-query architecture
(§8.8-§8.11 of `spec-physics-runtime.md`); (2) the aim-lead ray is more central to ordinary fire than
its gating suggests, if some upstream auto-target mechanism usually makes the target handle non-null
in practice (not traced). Concrete next steps if revisited: read `0x008ccb90`/`0x00920170` in full
(both called unconditionally in `item::+0x8c`, role-only so far); or a live capture on `0x0096fa20`'s
own callers during an actual fire sequence. Full detail:
`scratchpad/weapons_hitscan_vtable_install_2026-10-08.md`.

### 2.3 `0x009c26f0` — a melee/AoE sphere-cast hit-resolution function, confirmed to reach the damage sink

Full body read through its damage call (~760 instructions). Signature: 5 explicit parameters plus
register-passed `ecx`/`edx`. Computes a swept cast target by interpolating between two global constants
(`0x0262e64c`/`0x0262e650`) using a trailing "progress" float argument and a fixed time constant
(`0x012a2d70`). Calls the shared shape/sphere-cast wrapper **`0x0075c880`** directly (tag/kind field set
to literal `0x6f` before the call — HYPOTHESIS role, not matched against the ray wrapper's own kind-byte
documentation, which describes a different input struct). Gathers up to 64 unique hit-body handles
(deduplicated against two sentinel globals, `0x011753b8`/`0x011753bc`), runs §2.1's resolution chain on
each. For a resolved candidate:

- If the "progress" float is at/above a threshold (`0x01117a4c`), an unidentified predicate `0x0096f990`
  passes, and the candidate isn't already dead (`0x0096f4f0`, `character_is_dead`): zero-initializes a
  local damage-request record via **`0x0067b050`** (the exact helper `spec-lua-api-behaviour.md` §3.15
  documents `character_kill` using) and calls **`0x0096fa20`** with literal lethal constant **`-1.0`**
  (`0x012a2d54`) and flag `0x2000` — an instant-lethal hit, HIGH confidence a melee finisher/takedown or a
  point-blank/critical mechanic (genuinely ambiguous vs. an explosion-proximity-kill from static reading
  alone, §7 — OPEN).
- If already dead: skips the damage call, calls a ragdoll/impulse-only finalizer `0x00970e70` instead
  ("don't re-kill a corpse, just push it").
- If progress is below threshold (ordinary case): checks the "suppress flinch" bit (`candidate+0x1c98`
  bit `0x800`, the exact bit `spec-lua-api-behaviour.md` §3.14 documents for
  `character_prevent_flinching`) and begins a relative-position/dot-product computation, not fully traced
  (OPEN).

**This is the strongest evidence that melee/AoE hit resolution reuses the already-documented shared
sphere-cast wrapper and damage sink** — resolving `spec-physics-runtime.md` §8.11's open item for this
case in favor of hypothesis (a) (reuse of the existing wrappers), not (b) or (c).

### 2.4 Corroboration: vehicle destruction shares the same "valid target" predicate

`0x00a9d6c0` (traced because it also calls `0x0096fa20`, three times — §4.2) opens with a vehicle-shaped
predicate (`0x00a77a10`) and calls **`0x0091f5e0`** — the exact same predicate §2.1 found shared between
the two hit-resolution consumers. Independent, structural evidence the candidate-validity layer is a
general "can X damage Y right now" check reused across melee, hitscan-targeting, and vehicle destruction,
not weapon-specific.

## 2a. Grenades, explosives, and RPG-style physical projectiles (2026-10-09)

### 2a.1 Grenade throw-trajectory — cross-reference only

The AI "throw grenade"/"throw weapon" trajectory solve is already fully specced at
`spec-ai-runtime.md` §13.2/§13.2.1/§13.2.2 (not re-derived here): both reduce to a shared
ballistic-angle core `0x0054c630`. **Citation fix:** §3.2 below's `0x0054dce0` (previously an
unattributed weapon-timer caller) IS the AI "throw grenade" action (action id 4) documented there —
not a separate function. The grenade solver reads its own `arc`/`speed` tunables directly from
`Projectile_Info.Gravity`/a working-struct speed field, unconditionally (no player/NPC branch,
unlike the thrown-weapon case which does branch via `0x00853b30`).

### 2a.2 The `projectile` class: constructor, destructor, object pool — CONFIRMED

A whole-image scan for the `projectile` vtable literal (`0x0116fafc`, §2.2c) finds exactly two
vtable-install sites:

- **`0x00921c70`** — the constructor. Defaults ~30 fields, including **6 independent 64-bit handle
  pairs** (owner, attacker/instigator, last-target, etc. — `+0xf0`/`+0x110`/`+0x140`/`+0x188`/
  `+0x1a0`/`+0x1a8`) all to a baked null-handle constant (`0x0116fae0`/`0x0116fae4`). Instance size
  at least `0x208` bytes.
- **`0x00924e40`** — the scalar-deleting destructor (reached via vtable slot 0, thunk `0x00927800`).
  On teardown, resolves up to two handle pairs via the project's universal keyed-object resolver
  (`0x00458230`/table `0x024433a8`, the same chain §2.1 documents) and, on a successful second
  resolve, calls `0x00941f40` (equip-holder→weapon-definition resolver) and updates that weapon
  instance's own **newly-found field `+0x2e0`/`+0x2e4`** — a "last spawned projectile" handle
  back-reference, overwritten on the owning projectile's destruction.
- **`0x009276f0`** — the object POOL ALLOCATOR, CONFIRMED (full body read): a fixed-size free-list
  pool, slot stride `0x208` bytes matching the constructor's field range. **This confirms projectile
  instances are created from a fixed-capacity pool, not individual heap allocation** — directly
  useful for a clean-room port (the pool's own capacity field not read, OPEN).

### 2a.3 `0x00927820` — launch/physics-parameter setup, CONFIRMED (a virtual method, vtable slot not pinned down)

A `projectile`-class virtual method (DATA xref from vtable slot `0x0116fb0c`). Gates on whether the
firing weapon's `Projectile_Info` block exists (null `Model` field → different branch, not traced) —
**the first direct code-side confirmation that `Projectile_Info.Model` presence is a real runtime
branch, not just a loader-side check.** On the present path, reads `Mass` (`+0x37c`),
`Post_Ignition_Speed`/`.NPC` (`+0x354`/`+0x358`, selected via the player-vs-NPC test `0x00853b30`),
damping/restitution/friction fields (`+0x384`/`+0x388`/`+0x38c` — HIGH CONFIDENCE but not
byte-verified against which of `Linear_Damp`/`Angular_Damp` this corresponds to), four
`Projectile_Flags` bits (`0x400` "bullet collision quality," `0x80` "attach effect after ignition,"
`0x8000` "does not fire from muzzle," `0x4` "sticky") and `Projectile_Ignition_Delay_MS` (`+0x376`).
**Ends by calling `0x009270e0` (§2.2a) — the exact per-shot fire-orchestration function already
documented for hitscan** — confirming the globals `0x0116fae0`/`0x0116fae4` (the projectile
constructor's own "null owner" default, §2a.2) are the SAME general-purpose "current target" cache
`0x009270e0` uses, not projectile-specific. **This is the new link found this pass: the physical-
projectile spawn/launch path feeds directly into the same per-shot fire machinery already documented
for hitscan** (HIGH CONFIDENCE — the exact upstream weapon-fire dispatch call into `0x00927820`
itself was not walked back to a specific vtable slot, a half-step short of CONFIRMED).

Cross-referenced against the already-confirmed `Projectile_Info` DATA schema
(`spec-tables-weapons-combat.md` §2.3): `AI_Can_Guide` and the homing-related `Projectile_Flags`
bits (`0x20`/`0x40`/`0x2000`/`0x4000`) were **not** observed read by `0x00927820` — confirmed, since
it is a one-time spawn-setup call, not a per-frame tick.

**The per-frame flight/homing-update consumer, HIGH CONFIDENCE, found (2026-10-09 follow-up):**
`0x00925180` — vtable slot index 1 (`0x0116fb00`, the entry right after the destructor thunk). Of
all 32 slots on the `projectile` vtable, this is the *only* one that reads `AI_Can_Guide`
(`weapon_def+0x378`) together with the homing-relevant `Projectile_Flags` bits `0x20`/`0x40`/
`0x2000` (plus `0x4`/`0x800`/`0x80000`, nine distinct bit tests total) — exactly where the schema
predicted. It reads/writes the object's own position and velocity fields, computing `position +
velocity*2000.0` (a forward lookahead point) fed into raycast/occlusion calls (`0x00da05a0`/
`0x00da38e0`/`0x0075d7e0`) — the classic homing-missile sensor-check shape. **One caveat:** it has a
standard SEH prologue and a large (~0xe6f-byte) body, not the "small, no SEH" heuristic originally
guessed for a per-frame tick — the identification rests on the unique field-read match across all 32
slots, not on that size heuristic. Full detail: `scratchpad/wave12_projectile_fuse_homing_2026-10-09.md`.

### 2a.4 Fuse timer / impact detonation — DATA fully mapped, CODE-side consumer OPEN

`spec-tables-weapons-combat.md` §2.3 already maps `Projectile_Info.Fuse_Time`/`.NPC_Fuse_Time` and
the full `Projectile_Flags` vocabulary including `detonate on vehicle/human collision` (`0x100`/
`0x200`), `sticky` (`0x4`), `satchel charge` (`0x10`). Neither the fuse-timer countdown consumer nor
the literal impact-detonation trigger were located as individual functions; neither was a call to
the generic explosion-apply routine `0x00594460` found in the projectile constructor/destructor
(the wrong place to look — destruction is cleanup, after any gameplay effect already fired). **OPEN
either way; narrowed, not closed, 2026-10-09 follow-up:** the `projectile` vtable (`0x0116fafc`) is
now CONFIRMED to have exactly **32 slots** (`0x0116fafc`-`0x0116fb78`) — there is no slot at `+0x80`,
so the generic "damage-apply" virtual method §2a.5 finds at the TARGET object's own `+0x80` slot
cannot be on the projectile's own vtable; it belongs to whatever the projectile collides with
(vehicle/character), ruling out that lead cleanly. Neither the launch-setup function (`0x00927820`),
the homing-update function (`0x00925180`, §2a.3), nor any of the other 30 vtable slots (checked one
call-level deep) reference `Fuse_Time`/`NPC_Fuse_Time` or reach `0x00594460`. The fuse/detonation
consumer is a genuinely separate subsystem from both the spawn-setup and homing-tick virtual methods
— most plausibly a generic engine timer/entity component outside the projectile vtable entirely, or
reachable two call-levels deep (not pursued, kept lean per scope). Full detail:
`scratchpad/wave12_projectile_fuse_homing_2026-10-09.md`.

### 2a.5 The Havok wall, narrowed: new traceable ground on the contact-listener dispatch side

`spec-physics-runtime.md` §10a already fully traces the generic in-tick contact-listener pipeline
for vehicle collisions (type id `7`). This pass found the SAME registration function
(`0x00766990`→`0x007669d0`) also covers type id **`0x20`**, paired against `5`/`7`/`0xa`/`0xff`, plus
a reverse `(7, 0x20)` pairing (handler `0x00765c70`, not opened). `0x00765fa0`/`0x00765f20` (both
read in full) route contacts through the same classifier `0x00763350` the vehicle bridge uses, then
walk an embedded position-history array (the classic fast-mover/tunnelling-mitigation shape — a
vehicle or character object would not carry this) and dispatch through the target object's own
vtable slot `+0x80`, a generic "apply collision-derived damage/force" virtual method. **HIGH
CONFIDENCE type `0x20` = "projectile," and capability bit `0x02cc9900[+6]`/`0x20` = "carries a
projectile-shaped position-history buffer"** (resolving part of §1's own "role OPEN" note for that
bit) — not a byte-for-byte CONFIRMED identification by name, since no RTTI/string name was read for
type `0x20`. **This narrows, but does not remove, the Havok wall**: the gameplay-side dispatch for
"what happens when a projectile touches something" is now traced to a specific virtual method slot;
the raw Havok-internal contact-listener callback body that decides *when* that contact fires for
these specific type-pairs remains unreachable by static tracing, same as the already-exhausted
hitscan-callback search (a different, not re-attempted dead end). Full detail for all of §2a:
`scratchpad/weapons_melee_explosives_projectiles_2026-10-09.md`.

## 3. Fire gating — cooldown / rate-of-fire

### 3.1 The timer primitive

A per-weapon-holder sub-object at **holder`+0x1c4`** (holder = `0x00941f40(character)`'s result) is
tested/mutated through a generic, engine-wide timer-object interface: `0x00d9e4c0(this)` ("has a
value"), `0x00d9e210(this)` ("get value"), `0x00d9e140(this, value)` ("set value").

**A second instance of this same timer interface found at `holder+0x134`, HIGH confidence the real
per-shot refire-delay gate (2026-10-08 follow-up, §2.2a).** Inside the fire-orchestration functions
`0x009270e0`/`0x005922e0` (§2.2a), both read a **16-bit** value off `holder+0xdc → +0x374` (a weapon
sub-table reached off the equip-holder, not the weapon-definition record's own `+0x130` field directly)
and feed it straight into `0x00d9e140` on `holder+0x134` — a different timer instance from this
section's own `holder+0x1c4`. This is read and applied in exactly the "about to fire" code path, unlike
`0x0050c930`'s six flat-literal callers below (§3.2) — **HIGH** confidence this is the real per-shot
refire-delay mechanism. **OPEN:** whether `holder+0xdc → +0x374` is a cached/pre-converted copy of the
weapon-definition's own `Time_Management.Refire_Delay` (`+0x130`) or a distinct field — not cross-checked
against each other's live values. A narrow follow-up: read what populates `holder+0xdc → +0x374` from
the `weapons.xtbl` loader side and check whether it sources from `weapon_def+0x130`.

### 3.2 `0x0050c930` — a weapon action-timer gate, HIGH confidence NOT the bullet-cadence gate

Full body read (54 instructions to its shortest exit). `0x0050c930(character, threshold)`: refuses if a
per-character "can't use weapon now" flag (`+0x317` bit `0x4`) is set; resolves the holder, reads its
weapon-definition pointer at `+0x19c`; if the §3.1 timer **has a value** and it's greater than
`threshold`, refuses immediately; otherwise checks an unidentified "busy/stunned"-shaped predicate
(`0x009b9160`) and, if true, calls `0x00b869b0` (§3.3) as a per-weapon-class validity filter; final
fallback checks holder`+0x1a4` ("has an active/loaded weapon instance").

**6 call sites found** (exactly matching the predecessor's own xref census): `0x0051f542`, `0x00541272`
(literal `1000`), `0x0054300f`/`0x005436fb`/`0x0054deef` (literal `750`), `0x0054494c` (literal `1200`).
**None pass a value read from the weapon record's own `Time_Management.Refire_Delay` (`+0x130`)** — every
threshold is a hardcoded literal. A sibling in the same cluster (`0x0054dce0`) reads
`Projectile_Info.Speed_NPC` (`+0x350`) directly — confirming this cluster
(`0x0051f040`/`0x00541230`/`0x00542ea0`/`0x005436f0`/`0x00544930`/`0x0054dce0`) is genuinely
weapon-subsystem code, just not the bullet-cadence check. **Verdict: HIGH confidence this cluster
handles weapon equip/holster/reload-step/melee-swing-recovery timing**, each its own flat-millisecond
cooldown, not the data-driven per-shot refire delay.

### 3.3 `0x00b869b0` — the per-weapon-class validity filter

A tiny, register-heavy predicate (`ebp` used as a raw data pointer, no real stack frame — the same idiom
`spec-physics-runtime.md` §8.9 found for its own tiny Havok filter callback `0x0075b780`). Early-outs on
a weapon-class field at `ebp+0x4c` (**the exact offset of `Weapon_Class`**, `spec-tables-weapons-combat.md`
§2.2) and `ebp+0x14`, then dispatches through a 22-entry jump table (of the 23-value `Weapon_Class` enum)
into per-class validity checks; at least one branch reaches the `0xac`-`0xaf` neighbourhood shared with
the vehicle HP accessors (§2.2) — consistent with a vehicle-turret-mounted weapon class having its own
extra validity rule. Individual case bodies not opened (OPEN). 20/20 call sites cross-checked against the
predecessor's own existing census.

## 4. Damage application

### 4.1 `0x0096fa20` is a pure damage sink, agnostic to weapon data

Already CONFIRMED in `spec-lua-api-behaviour.md` §3.15 as the generic damage-application routine, via
`character_kill`'s own call. Independently re-confirmed from the non-Lua side: a fresh whole-binary
census finds **70 call/jmp sites across 53 distinct functions** (matches the predecessor's own existing
census exactly). Direct disassembly of four callers (`0x009c26f0` §2.3, `0x0099ebe0` §8.2,
`0x00a9d6c0` §2.4, `character_kill` itself) shows a consistent shape: **the caller always computes the
damage amount itself** (a literal `-1.0` lethal constant in three of four; no per-weapon damage value was
ever observed being read inside `0x0096fa20` or immediately before any of these calls) and passes it as a
float with a flags word and a small local "damage-request" record zero-initialized by `0x0067b050`.
**Architectural conclusion: `0x0096fa20` is a pure sink** — it doesn't know or care whether damage came
from a gun, a melee hit, an explosion, or a scripted forced-kill; every caller computes the value first.
**This confirms the generic apply-damage chokepoint is reached by melee (§2.3), vehicle destruction
(§4.2), and non-combat forced-death/despawn housekeeping (§8.2), in addition to the Lua-native paths
already on file.**

### 4.2 Vehicle destruction also routes through the same sink

`0x00a9d6c0` calls `0x0096fa20` three times (`0x00a9d999`/`0x00a9da61`/`0x00a9db41`). Its entry gates on
a vehicle-shaped predicate (`0x00a77a10`) and reuses `0x0091f5e0` (§2.4). ~~Not fully traced past the
entry gate and the three call sites. A nearby, likely-sibling "vehicle-damage/detonation dispatch" at
`0x00a9f4f0` was not cross-checked against this address — OPEN, narrow.~~ **CLOSED, 2026-10-08, see new
`spec-vehicle-damage-runtime.md`.** `0x00a9d6c0` has exactly one caller in the whole binary —
`0x00a9e1d0`, the real (previously unnamed) vehicle-destruction trigger — and `0x00a9f4f0` is confirmed
a **separate**, vehicle-specific HP-apply dispatcher, not a sibling/reuse of `0x00a9d6c0`: the two
damage economies (character-sink-via-occupant vs. vehicle-HP-via-`0x00a9c1f0`) meet only at the occupant
layer. Vehicle explosions are also confirmed to reuse the generic `explosions.xtbl` apply routine
(`0x00594460`, the same target `explosion_create` uses) — see the new file for the full trace.

### 4.3 The generic damage-request record and `character_is_dead` reuse

Every traced damage-sink caller applying a *forced* death re-queries `0x0096f4f0` (`character_is_dead`)
around the call; the zero-init helper `0x0067b050` is reused verbatim by at least three independent call
sites across two functions (`character_kill`, both relevant branches of `0x009c26f0`, and `0x0099ebe0`)
— strong, consistent evidence of one single damage-request record shape used project-wide.

### 4.4 Honest gap: the hitscan-weapon-fire → damage call site was not found

Despite tracing the hitscan *targeting* path end-to-end (§2.2) and the generic damage sink end-to-end
(§4.1), the specific function that performs a live-fire ray cast and then calls `0x0096fa20` was not
located. `0x00b86b20` performs the cast and identical resolve/filter chain but is a debug overlay that
stops at displaying stats. **Still OPEN after a dedicated follow-up pass (§2.2a):** `0x00b865f0` and its
full neighbourhood (13 functions across 3 branches of the ray-wrapper caller tree, plus a fingerprint
search of all ~90 unattributed callers) were traced and confirmed NOT to be the answer — the gap is now
precisely bounded (zero address-space overlap with the damage sink's own 53-function caller census) but
still genuinely open. Named, not-yet-chased leads: `0x00b05b90`/`0x00b19ad0` (both since closed, neither
is hitscan-fire — see §2.2a), and the ~86 unattributed `0x0075d7e0` callers that didn't match the
fingerprint search. **Re-bounded by a 2026-10-08 player-input-side pass (§2.2b): the fire dispatch is an
indirect virtual call (`[item-record-vtable+0x8c]`), not a static call chain, so no amount of auditing
`0x0075d7e0`'s remaining static callers can find it — the real next step is finding the equip-time
vtable-install site(s) for item-slot records, per §2.2b.** **OPEN, not guessed.**

**Both of §2.2c's own named next steps tried, 2026-10-09 — neither closes the gap.** `0x00920170` is
directly confirmed by full disassembly to be the unrelated vehicle-seat "Hide Occupant" toggle (no call
to the damage sink or either Havok query wrapper anywhere in its body) — matching, not just inferring,
this file's own existing identification. `0x008ccb90` (first ~470 instructions, fully covered) likewise
contains no such call — it shares some generic record-building helper calls with `0x0096fa20`'s own
early bookkeeping, not a call relationship. **The gap remains genuinely open, but the far side of the
chain is now fully mapped** (`spec-player-runtime.md` §15.3a: health storage, the damage sink's own
internal health-mutation calls, the death-trigger function, and the real ragdoll/finalize branch point)
— a future pass has a precise, concrete target chain to search for a caller of. The two live candidates
remain: a physical projectile/ray whose Havok collision-response callback (not reachable by static
call-graph tracing) is the real caller; or a live capture directly on `0x0096fa20`'s own callers during
one real fire sequence. Full detail: `scratchpad/character_damage_death_2026-10-09.md`.

**Full backward caller census re-derived, 2026-10-09 — statically exhausted, gap NOT closed.** A fresh,
unbounded re-derivation of `0x0096fa20`'s whole-binary caller set (the earlier count of "70 sites / 53
functions" was an artifact of a `-noanalysis`/`maxinsn=600`-bounded Ghidra run) finds **75 call sites in
58 distinct functions**. A whole-file raw dword scan for the literal `0x0096fa20` found **zero**
occurrences anywhere outside real call instructions — i.e. no vtable slot, function-pointer table, or
other indirect-install site installs this address anywhere in the binary, so no indirect caller can have
been missed by the static census. Every one of the 58 callers was individually characterized: melee/AoE
hit resolution, scripted boss abilities, vehicle destruction/collision/ram/hijack (§4.2's own cluster),
falling/environmental damage, mission-scripted kills, AI combat-reaction finishers, a generic per-frame
despawn sweep (§8's `0x0099ebe0`), three debug-console commands plus one scripted named-object kill (all
gated behind either the debug flag `0x29cdb88` or a hardcoded object name — not reachable from live
unscripted play), and exactly one genuinely indirect-shaped caller, `0x0072e390`, which traces fully to a
mundane notification/despawn handler — not weapon/hitscan-shaped. The 5 previously-named aim-assist
wrapper functions are confirmed **absent** from this entire 58-function set. The ray-wrapper caller
census (§2.2a/§2.2b's own ~90-callers-of-`0x0075d7e0` side, re-run at 99 sites for this pass) was
re-checked against this set and still shows **zero overlap**, including at the two closest
address-proximity near-misses, both ruled out at the instruction level (neither contains a call to
`0x0096fa20` or to any of its 58 known callers). **Verdict: this avenue is now genuinely exhausted by
static analysis** — three independent passes across this whole effort (the original forward hitscan
trace, §2.2a/§2.2b/§2.2c's named next-steps, and this backward census) have each closed with no
hitscan-shaped caller found anywhere reachable by disassembly/call-graph methods. The only remaining path
is a live capture on `0x0096fa20`'s own callers during one real fire sequence — not performed as part of
this pass; whether to pursue it is a scope decision, not a static-analysis one. Full detail:
`scratchpad/hitscan_damage_sink_caller_search_2026-10-09.md`.

## 5. Ammo tracking

### 5.1 The per-character, per-weapon inventory-slot mechanism

Already CONFIRMED in `spec-lua-api-behaviour.md` §4.10/§4.11: a per-target, per-weapon ammo/flags slot
record, located through a weapon-definition `+0x1b78` sub-object via two locator-function pairs —
primary `0x008dc820`+`0x005fed90`, secondary `0x008dc690`+`0x005ff110`. Independently confirmed from the
non-Lua side: a fresh whole-binary census of `0x008dc820`'s callers finds **9 sites** — three inside the
already-documented Lua apply routines' neighbourhood, one inside `inv_weapon_remove_temporary`'s own
body, and **five more in a previously undocumented cluster at `0x00815xxx`-`0x00817xxx`**, not reached
from any Lua native — core, non-scripted gameplay code independently reaching the same locator.
`0x005ff110` is confirmed a 1-instruction thunk (`jmp 0x005fec20`) — the "secondary locator" is literally
the same function already named elsewhere as "a further weapon-select dispatch" for `inv_item_equip`,
another cross-confirmation this is shared, general-purpose code.

### 5.2 `0x00816430` — a concrete, newly-traced gameplay (non-Lua) consumer

Full body read (130 instructions, clean single exit). `0x00816430(ecx=weapon-definition, eax=mode)`:
if `eax == 1000`, iterates a fixed global array of ammo-type handles (`0x0022ce8b0`, count
`0x0022ce35c`), calling `0x008dc820` for each — the exact primary locator (§5.1) — comparing against a
caller-supplied slot pointer; on a match, resolves a sub-locator (`0x005fe900`) and setter
(`0x00601380`) and **clears the holder's `+0x1a4` "active weapon instance" marker** — the same field
`0x0050c930` reads as its final fallback (§3.2). HIGH confidence an ammo-type switch/invalidate path
(e.g. a multi-grenade-type launcher switching active type). Otherwise, uses the secondary locator
(`0x005ff110`→`0x005fec20`), keyed off the "current context" object's own `+0x1b78`; on a resolved slot,
reads `+0x19c` and calls a probable ammo-record function at **`0x00b6ee40`** — **not the same address as
`spec-tables-weapons-combat.md`'s own documented ammo-record resolver `0x00b6e4e0`** (differ by `0xa60` —
flagged, not claimed identical; `0x00b6ee40`'s own identity is OPEN). After either branch, fires a
UI-shaped notification (`0x0094d920(context, event=11, &value)`) — HIGH confidence the ammo-count HUD
update, strong indirect evidence an ammo count just changed, though the exact live-count field offset
inside the resolved slot record was not pinned down.

### 5.3 The "current context" global, `0x0262edfc`

`0x009da4e0()` is a 2-instruction accessor (`mov eax,[0x0262edfc]; ret`) used throughout this report.
**Exactly three write sites exist anywhere in the binary**: `0x009da1c4` (zeros it); `0x009e2065`
(inside `0x009e1fc0`, resolves it once at boot via the project's 2,999-slot named-object registry,
`0x00456e30`/`this=0x02442750`/name-id `0x21` — apparently a named weapon or weapon-adjacent object,
resolved once and cached; the same init function also registers several named boot-time toggle flags
through `0x0086d770`, the identical "register a named toggle" primitive `spec-ai-runtime.md` §17
documents); `0x009e3d0f` (a second, not-fully-characterized setter — OPEN).

**Revised role, HYPOTHESIS:** given at least one consumer (`0x00816430`, §5.2) treats this pointer's own
`+0x1b78` field exactly like a real weapon-definition's ammo-bucket sub-object, and given it is resolved
once at boot via a name, the most likely identity is a **cached default/fallback weapon-definition
record (e.g. "unarmed"/fists)**, not a per-shot "current attacker's weapon" pointer. Not confirmed — a
good next step: read `0x009e1fc0`'s own name-id `0x21` resolution target and `0x009e3d0f`'s full body.

## 6. What this pass did NOT establish

~~Stated plainly: **the single instruction that decrements a "rounds remaining in clip" counter when a
shot is fired was not found**, nor was the exact byte offset of that live counter inside the resolved
ammo-inventory-slot record pinned down.~~ **CLOSED, 2026-10-08 (§2.2c): the decrement is
`add dword ptr [esi+0x1ac],eax` at `0x00b860e9`, inside `weapon::+0x8c` (`0x00b86030`) — `this+0x1ac`
on the weapon INSTANCE is the live rounds-in-clip counter, clamped at zero, set to `0x7fffffff` for
"infinite ammo."** **The exact call site that reads `Time_Management.Refire_Delay`
(`+0x130`) and gates the next shot on it was not found** — only the generic timer primitive it almost
certainly reuses (§3.1) and a sibling gate (`0x0050c930`, §3.2) confirmed to be a *different* use of the
same primitive. **Narrowed, 2026-10-08 (§2.2c): a third base object for this same timer primitive was
found on the weapon instance itself (`this+0xfc`, set to a literal 1000ms inside `item::+0x8c`) — HIGH
CONFIDENCE this is the real refire-delay gate (more directly tied to the fire action than either
holder-based instance), not independently cross-checked against its own reader.**

## 7. Melee vs. explosion ambiguity — CLOSED 2026-10-09: neither. A shotgun/Sonic-Wave-Gun AoE mechanic.

§2.3's "progress ≥ threshold → instant lethal hit" branch (`0x009c26f0`) has exactly **one caller in
the whole binary**: `0x009ccda0`. **The entire function (908 instructions, `0x009ccda0`-`0x009cdda1`, entry through return) has now
been read in a single exhaustive pass, with no gap.** Every access to
`Weapon_Class` (`weapon_def+0x4c`) in the function was grepped and checked for jump-table/switch
dispatch: exactly two accesses exist, both `CMP ... 0x3` (shotgun) at `0x009cd775`/`0x009cd8a5` — no
comparison against any melee-shaped `Weapon_Class` value (`knife` 6, `bat` 9, `sword` 10, `pimp slap`
11, `knuckles` 13, `chainsaw` 19) exists anywhere, and there is no indirect/jump-table dispatch on
that field either. The only other gate reaching the one call site into `0x009c26f0`
(`0x009cdbcb`) is `Special_Case_Type == 2` (Sonic Wave Gun) — there is no second call site anywhere
in the function. **CONFIRMED clean negative, both ways:** the instant-lethal branch is not an
explosion-proximity-kill (the caller never resolves `explosions.xtbl` or calls the generic
explosion-apply routine `0x00594460`, `spec-lua-api-behaviour.md` §9.3/`spec-vehicle-damage-runtime.md`
§4) and it is not a melee finisher either (no melee-class branch reaches this call site, in a now
fully-read, not sampled, function body). **`0x009c26f0` is purely a shotgun/Sonic-Wave-Gun AoE
mechanic.** Since `0x009c26f0` has exactly one caller project-wide and that caller is now fully
read, this is conclusively ruled out as melee's own hit-resolution path — melee's own mechanism (if
any shape-cast is involved) lives elsewhere, not yet located. Full detail:
`scratchpad/wave12_melee_confirm_2026-10-09.md`, `scratchpad/weapons_melee_explosives_projectiles_2026-10-09.md`.

**Melee's own hit-resolution mechanism, HIGH CONFIDENCE candidate found (2026-10-09 follow-up),
with a new OPEN gap of its own.** `0x008cfeb0` (previously undocumented, full body read): melee's
swing-time hit test. Runs a Havok ray-cast (`0x0075c850`→`0x0075d7e0`, the same wrapper
`spec-physics-runtime.md` §8.8 documents) from a weapon-reach-offset position (holder position
fields `+0x3c`/`+0x40`/`+0x44` plus a global reach vector), filters candidates through the shared
class-capability table `0x02cc9900` (the same table §2.1's resolver chain uses, but testing bits
`+0xb`/`0x2` and `+0xa`/`0x2` — melee's own narrower filter, not the generic `+6`/`0x08`
"damageable" bit), and on success caches a hit-target handle plus two new flag bits (`0x10`/`0x20`)
on the swing's own sub-object byte `+0x8d`. **It sits directly in the already-identified
swing-recovery timer cluster's control flow**: `0x00542ea0` (one of
`0x0051f040`/`0x00541230`/`0x00542ea0`/`0x005436f0`/`0x00544930`, §3.2's callers of the action-timer
gate `0x0050c930`) calls it *before* reaching its own `0x0050c930` gate call. Exactly 5 static
callers project-wide: `0x00542ea0` (swing-recovery), `0x005403c0` (combo-continuation — fires
named-event hashes `0xda1f932c`/`0x16de18a1` via the already-documented dispatcher `0x0070dcf0`/
table `0x01504088` depending on the hit-test result), `0x0050a690` (melee target-acquisition/
candidate-validation, reusing §2.1's exact resolver calls `0x00458230`/`0x00853b10`), plus 2 more
(`0x00543320`, `0x008d10a0`) not opened. **New OPEN gap, narrowed not closed:** none of
`0x008cfeb0` or its 5 callers calls the generic damage sink `0x0096fa20` (consistent with, not
contradicting, §4.4's already-exhausted 58-function sink-caller census) — the melee hit-confirmed
state (the `+0x8d` flag byte) must be consumed by some further, not-yet-located function, most
plausibly a per-frame tick by analogy with other deferred-flag patterns this document already
documents (§2a.5, §8). Same shape of gap as the hitscan fire→damage gap, now re-created for melee
but at a much narrower, concretely-named choke point (one flag byte on one named sub-object)
instead of an unbounded search. Full detail: `scratchpad/wave13_melee_hit_resolution_2026-10-09.md`.

**Consumer search narrowed further (2026-10-09 follow-up) — still OPEN for the final hit→damage
hand-off.** The 2 previously-unopened callers were opened: `0x00543320` is just a 6th member of the
swing-recovery/combo-continuation cluster, adds nothing new. `0x008d10a0` checks a new adjacent
byte `+0x8e` (state code, must equal `5`) and a third, previously-undocumented handle field
`+0x10`/`+0x14` (distinct from the `+0x78`/`+0x7c` hit-target cache), then calls `0x00950550` — a
second "can X damage Y"-shaped predicate (sibling to, but distinct from, the already-documented
`0x0091f5e0`), itself called from inside `0x0096fa20`'s own body — tying melee's re-validation gate
into the sink's own eligibility layer, though not itself a call edge into the sink. A new function
found via `0x008d10a0`'s own caller list, **`0x008d1110`**, is the first located beyond the known 5
to directly read the swing sub-object's `+0x8d` bit `0x8` (active-swing): it scans the global
entity list for live characters with an active/untargeted swing, re-runs the hit-test against a
reference entity, and on a hit builds a short record containing the literal `-1.0f` (the same
constant this document already associates with melee-finisher/instant-lethal outcomes, §7 above)
and calls `0x00a12a80` — **HIGH CONFIDENCE a reactive per-entity consumer of the hit-confirmed
state** (shape matches a hit-reaction/parry/finisher-camera trigger), but `0x00a12a80` itself was
not opened and no call to `0x0096fa20` exists anywhere in `0x008d1110`'s body (nor in its own
caller, `0x008d1520`, checked at depth 0). **Concrete next addresses for a future pass:**
`0x00a12a80`, `0x008d1110`'s other 2 callers (`0x008d1d80`/`0x008d1750`), and `0x00942350`. Full
detail: `scratchpad/melee_hit_confirmed_consumer_2026-10-09.md`.

**All 4 addresses opened (2026-10-09, second follow-up) — one candidate ruled out, still OPEN.**
`0x008d1110` re-read in full via its 3 callers' own depth-1 expansion: a generic AABB-bounded
candidate-query + weighted-pick dispatcher, not melee-specific itself — scans the global live-entity
registry (`0x03171a64`, §8) via a dynamic per-candidate accept callback (passed in on its own
caller's stack struct), then picks one surviving candidate and calls `0x00a12a80`. **`0x00a12a80`
— CONFIRMED NOT the damage hand-off.** Full body and depth-1 callees read; none is `0x0096fa20` or
near it — the shape matches a hit-reaction/audio-cue/variant-selection function (picks one entry
from a table via a modulo-reduced index, writes the chosen id/handle back), with no damage-amount
or HP field anywhere. This rules out the prior pass's own top candidate. `0x008d1d80`/`0x008d1750`
(both fully read): AABB-builders sampling points around the swing-reach arc, each calling
`0x008d1110` with their own distinct accept callback (`0x008ce2e0`/`0x008cdbc0` respectively) —
neither function nor its depth-1 callees reaches the sink. `0x00942350` turned out to be a
damage-SCALING helper called FROM INSIDE `0x0096fa20`'s own body (a sink internal, not a caller
reaching it) — its only link to the melee cluster is via the same shared-gate pattern already seen
with `0x00950550`/`0x0091f5e0`/`0x0096f4f0` (validity/eligibility predicates shared between melee's
own filtering and the sink's internals, never a forward call edge). **Net: the hit→damage hand-off
remains genuinely OPEN after three passes — not forced.** New concrete leads: `0x008ce2e0` and
`0x008cdbc0`, the two dynamically-dispatched per-candidate accept callbacks `0x008d1110` calls right
before a candidate is accepted into the pick array feeding the now-ruled-out `0x00a12a80` — the
most likely remaining location for an actual damage-apply call, if one exists in this cluster at
all. Full detail: `scratchpad/melee_hit_confirmed_consumer_round2_2026-10-09.md`.

**Both accept callbacks opened (2026-10-09, third follow-up) — the statically-reachable call graph
rooted at `0x008cfeb0` is now exhausted, with no edge into the damage sink anywhere.**
`0x008ce2e0`/`0x008cdbc0` (both fully read, depth-1 callees too) are **pure geometric accept/reject
predicates, CONFIRMED** — squared-distance gate, forward-facing dot-product/cone check, range and
AABB-epsilon checks, and a shared ~600-instruction line-of-sight/FOV-cone test (`0x008cd1d0`, which
itself calls LOS/raycast/bearing-angle primitives `0x00ad5020`/`0x004de500`/`0x00da3910`/
`0x00dae5b0`). **No HP/health field, no damage-amount parameter, no entity-state write, and no call
to `0x0096fa20` anywhere** in either callback's body or any of their depth-1 callees — this rules
out the hypothesis that damage applies as a side effect of the accept decision itself. **Net: the
melee hit→damage hand-off remains genuinely OPEN after four passes.** Every statically-reachable
function from `0x008cfeb0`'s own swing-time hit test — through `0x008d1110`, both caller/callback
pairs, and the already-ruled-out `0x00a12a80` — has now been walked to exhaustion with no edge into
the sink. The only untried functions remaining are the geometry primitives one level deeper
(`0x00ad5020`/`0x004de500`), low priority given their already-established shape. **This is now a
real, structural negative over a concretely-enumerated search space** (the same standard this
project applies to other exhausted static-analysis questions, e.g. §4.4's hitscan fire→damage
search) — either melee damage is applied through a mechanism this reachability search can't see
(an indirect/vtable call, a Havok collision-response callback the way §2a.5 already documents for
projectiles), or the swing's hit-confirmed state feeds something entirely outside this call graph.
Full detail: `scratchpad/melee_hit_confirmed_consumer_round3_2026-10-09.md`.

## 8. Three predecessor leads resolved

**`0x005ae3f0`** (the predecessor's unexplained 367KB lead): independently verified weapon-relevant
(reads the weapon-definition `+0x19c` field and ammo-bucket `+0x1b78` offset dozens of times, calls into
the confirmed `weapons.xtbl` subsystem address range), but does **not** read any ballistics-shaped field
(`Range_Max`, `Damage_Max`/`Min`, `Magazine_Size`/`Ammo_per_Shot`, `Time_Management.Refire_Delay`) — it
resets the holder's cooldown-timer sub-object instead. **Verdict: HIGH confidence this is the weapon
equip/select/switch state-machine entry point**, a close sibling of the fire path, not the fire path
itself.

**`0x0099ebe0`**: reached while walking the ray-wrapper caller census for a plausible fire-trigger
function — the wrong branch. It's a per-frame sweep over a global registry (`0x03171a64`) dispatching on
an 8-9-case removal-reason code; one branch reaches `0x0096fa20` with the lethal constant and
`0x0067b050` idiom (a fourth independent confirmation of §4.1's shared-sink claim, not a weapon-fire
finding). A later part calls the already-documented camera-pivot trampoline `0x0075df80`
(`spec-physics-runtime.md` §8.8) for an unrelated camera-nudge purpose, confirming this is a large,
multi-concern per-frame maintenance function.

**`0x00b865f0`** (the top-named unexamined neighbour as of the original pass): **resolved, 2026-10-08
(§2.2a) — CONFIRMED NOT the fire-damage function.** A ray-cast + impact-FX/UI dispatcher routing to a
generic impact-FX dispatcher (`0x00594460`), never reaching `0x0096fa20`. Its own two callers
(`0x009270e0`/`0x005922e0`) and the dispatch above them were also fully traced — see §2.2a.

**`0x00b87680`**: also resolved, 2026-10-08 (§2.2a) — a parallel aim-assist/target-lead prediction chain
terminating in 3D positional smoothing (`0x00b872a0`, a vtable call), not damage.

**Promising unexamined neighbours remaining for a future pass:** `0x00b0d500`/`0x00b0f060`/
`0x00b39ad0`/`0x00b720c0`/`0x00b74c90` (triaged shallowly in the §2.2a follow-up — none read `+0x19c`/
`+0x120`/call `0x0096fa20`/`0x00941f40`/`0x0091f5e0`, but not fully opened); `0x00b05b90`/`0x00b19ad0`
(two damage-sink callers sharing a megabyte region with the above but not themselves ray-wrapper
callers — a non-ray-cast path, if weapon-related); the ~86 of ~90 unattributed `0x0075d7e0` callers that
didn't match §2.2a's fingerprint search; the rest of the `0x00815xxx`-`0x00817xxx` ammo-locator cluster
(§5.1).

## 9. Confidence summary

**CONFIRMED:** the shared candidate-resolution chain and `0x2cc9900` table (§1-§2.1); `0x009c26f0`'s full
body through its damage call (§2.3); `0x0096fa20`'s pure-sink architecture and 70-site census (§4.1);
`0x008dc820`'s 9-site census and `0x005ff110`'s thunk identity (§5.1); `0x00816430`'s full body (§5.2);
`0x0262edfc`'s 3 write sites (§5.3); both predecessor-lead resolutions (§8).

**HIGH CONFIDENCE:** `0x00b86b20`'s debug-overlay role and the vehicle HP-accessor identification (§2.2);
`0x0050c930`'s equip/holster/reload-timing role, not bullet-cadence (§3.2); vehicle destruction's shared
sink usage (§4.2); the ammo-type switch/validate role of `0x00816430`'s first branch (§5.2);
`0x0262edfc`'s cached-fallback-weapon identity (§5.3).

**OPEN:** the literal hitscan fire→damage call site (§2.2/§4.4); the melee-vs-explosion ambiguity in
`0x009c26f0`'s instant-lethal branch (§7); the live ammo-count field's byte offset (§5.2/§6); the true
per-shot refire-delay consumer (§6); `0x00b6ee40`'s own identity (§5.2); `0x009e3d0f`'s full body (§5.3);
individual `0x00b869b0` case bodies (§3.3); ~89/91 and ~28/30 of the two Havok wrappers' own caller
censuses remain unattributed beyond this report's two traced consumers.

## 10. Clean-room check

Plain hex addresses throughout; no Ghidra decompiler output quoted (one predecessor input file was itself
a decompile dump and was read for orientation only — every claim sourced from it is restated here in
plain disassembly/offset terms, independently). No `FUN_`/`DAT_`/`LAB_` names used as real identifiers.
No leaked Volition/Havok/Audiokinetic/RAD material.

## Changelog

- **2026-10-09 — §4.4 extended: full backward caller census of the damage sink re-derived, statically
  exhausted, gap still NOT closed** (SPEC TEAM, agent `a4a54eda95f4932b7`, wave-7 item 1). A fresh,
  unbounded re-census of `0x0096fa20`'s callers finds **75 call sites in 58 functions** (corrected from
  the earlier `-noanalysis`/`maxinsn=600`-bounded figure of 70/53). A whole-file raw dword scan for the
  literal address found zero indirect/vtable-install sites, proving the static census cannot have missed
  an indirect caller. All 58 callers were characterized (melee/AoE, scripted boss abilities, vehicle
  destruction, falling/environmental damage, mission-scripted kills, AI combat finishers, a generic
  despawn sweep, 3 debug-console commands + 1 debug-gated scripted kill, and one genuinely indirect
  caller `0x0072e390` traced to a mundane notification/despawn handler) — none hitscan-shaped, and the 5
  named aim-assist wrappers are confirmed absent from the set. Re-checked against the ray-wrapper's own
  99-site caller census: zero overlap, including at the two closest address-proximity near-misses, both
  ruled out at the instruction level. Verdict: three independent passes (forward hitscan trace,
  §2.2a/§2.2b/§2.2c, and this backward census) have now each closed clean with no hitscan-shaped caller
  found by static methods — this avenue is genuinely exhausted; a live capture is the only remaining path
  and was not performed as part of this pass. Full detail:
  `scratchpad/hitscan_damage_sink_caller_search_2026-10-09.md`. Cleanroom self-check: strict regex 0
  hits, C-cast-dereference check 0 hits.

- **2026-10-09 — §4.4's hitscan-damage gap: both of §2.2c's own named next steps tried, neither closes
  it, but the far side of the chain is now fully mapped** (SPEC TEAM, agent `a9c8ae1260dd97f53`, wave 6
  item 3, dispatched primarily for the character-damage/death/ragdoll chain in
  `spec-player-runtime.md` §15.3a). `0x00920170` and `0x008ccb90` were both read in full this pass and
  directly confirmed to contain no call to the damage sink, the kill trigger, `character_is_dead`, or
  either Havok query wrapper. The gap remains open — narrowed, not closed — with a precise, concrete
  target chain now available for a future pass to search callers of
  (`0x0096fa20`→`0x0094d520`/`0x0094cfc0`→`0x00970e70`→`0x0098ce40`'s `+0xcce` state
  machine→`0x009a5520`/`0x00952760`, all newly opened in `spec-player-runtime.md` §15.3a). Full detail:
  `scratchpad/character_damage_death_2026-10-09.md`. Cleanroom self-check: strict regex 0 hits,
  C-cast-dereference check 0 hits.

- **2026-10-08 — new §2.2c: the equip-time vtable resolved — `weapon::+0x8c` = `0x00b86030`,
  CONFIRMED as the fire/use dispatch** (SPEC TEAM, agent `a177cd89e41ffc4a5`). §2.2b's own suggested
  fingerprint was tried and refuted (the weapon-class dispatch family are tiny predicates, not
  constructors); the real route was direct MSVC RTTI recovery of the compiled `weapon`/`item`/
  `projectile` vtables — this engine uses exactly one concrete `weapon` class for every category,
  confirmed by a zero-hit RTTI keyword sweep for derived classes. Closes §6's rounds-in-clip-decrement
  gap (`this+0x1ac`, weapon instance); narrows §6's refire-delay-gate gap (a third timer-primitive base
  object found on the weapon instance, `this+0xfc`=1000ms, HIGH CONFIDENCE the real gate); extends
  §2.2a's ray-wrapper caller census with 5 new, previously-unattributed sightings
  (`0x0075df80`/`0x0075dfa0`/`0x0075dfc0`/`0x0075dfe0`/`0x0075e170`, role HIGH-confidence aim-assist,
  not fire-damage). Honest gap, precisely bounded: the literal damage-apply trigger for an
  un-assisted hitscan shot still not found in this traced closure — two concrete next steps named
  (read `0x008ccb90`/`0x00920170` in full, or a live capture on `0x0096fa20`'s own callers). Full
  detail: `scratchpad/weapons_hitscan_vtable_install_2026-10-08.md`. Cleanroom self-check: strict
  regex 0 hits, C-cast-dereference check 0 hits.

- **2026-10-08 — new §2.2b: the hitscan fire→damage gap re-bounded, not closed, from the player-input
  side** (SPEC TEAM, agent `a4fe952ab41afa206`). The player's on-foot attack-input function `0x005ae3f0`
  (previously only seen from the opposite direction, §8) is CONFIRMED to be the fire path's own entry
  point — it dispatches fire via an indirect, polymorphic virtual call (`[item-record-vtable+0x8c]`), not
  a static call chain, at two internal sites (`0x005ae535`/`0x005ae53e`, `0x005ae5b0`/`0x005ae5b9`),
  cross-confirmed by two further independent sightings of the same mechanism (`0x0073e5db` inside
  `0x0073e310`; `0x006011fe` inside `0x00600720`) and a 56-site whole-binary byte-pattern scan. §4.4
  updated to reflect that no static caller-audit of `0x0075d7e0` can find the concrete implementation —
  the real next step is finding the equip-time vtable-install site(s) for item-slot records. Full detail:
  `scratchpad/player_combat_aim_fire_reload_2026-10-08.md`. Cleanroom self-check: strict regex 0 hits,
  C-cast-dereference check 0 hits.

- 2026-10-08: file created (SPEC TEAM, weapons runtime dispatch, redispatch of a session-restart-
  interrupted task). Resolved `spec-physics-runtime.md` §8.11's open item for melee/AoE hit detection
  (CONFIRMED) and hitscan targeting (HIGH); independently re-confirmed `spec-lua-api-behaviour.md`'s
  `0x0096fa20` damage-sink and ammo-slot-locator findings from the non-Lua gameplay-code side; traced a
  weapon action-timer gate and per-weapon-class validity filter; triaged all of the predecessor's
  unverified leads. Left open: the exact hitscan-weapon fire→damage call site, the live ammo-count
  field's byte offset, and the true per-shot refire-delay consumer. Full detail:
  `scratchpad/weapons_runtime_firing_hit_damage_ammo_2026-10-08b.md`. Cleanroom self-check: 0 hits.
  Synced to Team B.

- **2026-10-08 — follow-up pass closes the `0x00b865f0` lead and its full neighbourhood (new §2.2a);
  the hitscan fire→damage call site remains OPEN but far more precisely bounded.** Fully traced
  `0x00b865f0`/`0x00594460`/`0x009270e0`/`0x005922e0`/`0x00929450` and the `0x0092969d` dispatch region —
  all confirmed NOT the fire-damage path (ray-cast/impact-FX/orchestration, never reaching `0x0096fa20`).
  A fingerprint search across all ~90 unattributed `0x0075d7e0` callers found and fully traced 3 closer
  structural matches (`0x009cb3c0`/`0x009cbcf0`/`0x009d1b30`, HIGH confidence a weapon-inspect/aim-camera
  HUD system) plus a parallel aim-assist/target-lead chain (`0x00b87680`/`0x00b872a0`, 3D positional
  smoothing) — neither reaches the sink either. Found 2 new `0x2cc9900` entity-type bits (§1); a strong
  new candidate for the per-shot refire-delay gate at `holder+0x134` (§3.1); zero address-space overlap
  between the entire mapped hitscan-orchestration neighbourhood and the damage sink's 53-function caller
  census — a genuine structural finding, not just an absence of evidence. Full detail:
  `scratchpad/hitscan_fire_damage_consumer_2026-10-08.md`. Cleanroom self-check: 0 hits. Synced to Team B.

- **2026-10-08 — §4.2 closed: `0x00a9d6c0`'s single caller and the real vehicle-destruction trigger are
  now identified.** A dedicated vehicle-damage follow-up pass found `0x00a9d6c0` has exactly one caller
  in the whole binary, `0x00a9e1d0` — a previously-unnamed 662-instruction function that is the real
  vehicle-destruction trigger. Confirmed `0x00a9f4f0` is a **separate** vehicle-specific HP-apply
  dispatcher, not a sibling/reuse of `0x00a9d6c0` — the character-sink and vehicle-HP economies meet
  only at the occupant layer. Full trace in new `spec-vehicle-damage-runtime.md`. Full detail:
  `scratchpad/vehicle_damage_deformation_fire_explosion_2026-10-08.md`. Cleanroom self-check: 0 hits.
  Synced to Team B.

- **2026-10-09 — WAVE 11 item 4: melee/explosives/projectiles extension** (SPEC TEAM, agent
  `a5dac4238395b84fe`). §7's melee-vs-explosion ambiguity narrowed: `0x009c26f0`'s one and only
  caller (`0x009ccda0`) is entirely weapon-record-driven and never touches explosion machinery —
  HIGH CONFIDENCE not an explosion, revised to "AoE-attack executor shared by shotgun/Sonic-Wave-Gun/
  plausibly melee," not CONFIRMED melee specifically (~1,130 unread instructions remain). New §2a:
  the `projectile` class's constructor/destructor/pool allocator (`0x00921c70`/`0x00924e40`/
  `0x009276f0`) and launch/physics-setup virtual method `0x00927820` (CONFIRMED to feed into the
  already-documented per-shot fire-orchestrator `0x009270e0`) — a new link between the physical-
  projectile and hitscan fire machinery. New weapon-instance field `+0x2e0`/`+0x2e4` ("last spawned
  projectile" back-reference). The Havok wall narrowed, not re-butted: new `0x20`-type-pair
  (projectile) contact-listener registrations found and traced down to a generic damage-apply vtable
  slot — cross-referenced in `spec-physics-runtime.md` §10a. Fuse-timer/impact-detonation consumer
  and the per-frame homing-update function both remain OPEN. Full detail:
  `scratchpad/weapons_melee_explosives_projectiles_2026-10-09.md`. Cleanroom self-check: strict
  regex 0 hits, C-cast-dereference check 0 hits.

- **2026-10-09 — WAVE 12: §7 CLOSED — `0x009c26f0` is purely a shotgun/Sonic-Wave-Gun AoE
  mechanic, not melee** (SPEC TEAM, agent `a6c63e4c600f9fe53`). The remaining ~1,130 instructions
  of `0x009ccda0` (the one and only caller of `0x009c26f0`) were read, completing a full,
  gap-free pass over the whole 908-instruction function. Exhaustive grep over every `Weapon_Class`
  access confirms only the shotgun comparison exists — no melee-class branch, no jump-table
  dispatch. CONFIRMED clean negative both ways (not explosion, not melee). Full detail:
  `scratchpad/wave12_melee_confirm_2026-10-09.md`. Cleanroom self-check: strict regex 0 hits,
  C-cast-dereference check 0 hits.

- **2026-10-09 — WAVE 12: §2a.3 per-frame homing-update function found (HIGH CONFIDENCE); §2a.4
  fuse/detonation consumer narrowed, not closed** (SPEC TEAM, agent `ab59fee679c2587e1`).
  `0x00925180` (vtable slot 1, `0x0116fb00`) is the only one of all 32 `projectile`-vtable slots
  reading `AI_Can_Guide` together with the homing `Projectile_Flags` bits — computes a forward
  lookahead point fed into raycast/occlusion calls, the classic homing-sensor shape. The
  `projectile` vtable confirmed exactly 32 slots, no `+0x80` entry — ruling out the lead that the
  generic damage-apply slot §2a.5 found could be on the projectile's own vtable (it belongs to the
  collision target instead). Fuse-timer/impact-detonation consumer remains genuinely unlocated.
  Full detail: `scratchpad/wave12_projectile_fuse_homing_2026-10-09.md`. Cleanroom self-check:
  strict regex 0 hits, C-cast-dereference check 0 hits.

- **2026-10-09 — WAVE 13: melee's own hit-resolution mechanism found, HIGH CONFIDENCE, new §7
  addendum** (SPEC TEAM, agent `ab3c6a0c490e38bbc`). `0x008cfeb0` is melee's swing-time hit test —
  a Havok ray-cast from a weapon-reach offset, filtered through the shared class-capability table
  with melee's own narrower bits, caching a hit-target handle and flag bits on the swing's own
  sub-object. Sits directly in the already-identified swing-recovery timer cluster's control flow.
  New gap: none of its 5 callers reaches the generic damage sink — the hit-confirmed state must be
  consumed by a further, not-yet-located function. Full detail:
  `scratchpad/wave13_melee_hit_resolution_2026-10-09.md`. Cleanroom self-check: strict regex 0
  hits, C-cast-dereference check 0 hits.

- **2026-10-09 — melee hit-confirmed consumer narrowed further, still OPEN for the final
  hit→damage hand-off** (SPEC TEAM, agent `a6e68347893074672`). New function `0x008d1110` found —
  HIGH CONFIDENCE a reactive per-entity consumer of the swing's hit-confirmed state (scans the
  global entity list for active swings, re-runs the hit-test, builds a record with the same `-1.0f`
  melee-finisher literal §7 already associates with instant-lethal outcomes), but no call to the
  damage sink found in its body either. Narrowed to 4 concrete next addresses for a future pass.
  Full detail: `scratchpad/melee_hit_confirmed_consumer_2026-10-09.md`. Cleanroom self-check:
  strict regex 0 hits, C-cast-dereference check 0 hits.

- **2026-10-09 — melee hit-confirmed consumer, round 2: `0x00a12a80` ruled OUT, still OPEN after
  three passes** (SPEC TEAM, agent `a755921eba0be570e`). All 4 of the prior round's concrete
  addresses opened. `0x00a12a80` confirmed NOT the damage hand-off — a hit-reaction/audio-cue/
  variant-selection function, no damage field anywhere. The other 3 (`0x008d1d80`/`0x008d1750`/
  `0x00942350`) all turned out to be either AABB-builders feeding the now-ruled-out candidate, or a
  sink-internal helper reached from a shared gate, not a forward call edge. New leads:
  `0x008ce2e0`/`0x008cdbc0`, the two dynamically-dispatched accept callbacks feeding the pick array
  — not opened, budget-conscious stop. Full detail:
  `scratchpad/melee_hit_confirmed_consumer_round2_2026-10-09.md`. Cleanroom self-check: strict
  regex 0 hits, C-cast-dereference check 0 hits.

- **2026-10-09 — melee hit-confirmed consumer, round 3: both accept callbacks exhausted — the
  reachable call graph has no edge into the damage sink** (SPEC TEAM, agent `aac0db08cc6f230b6`).
  `0x008ce2e0`/`0x008cdbc0` confirmed pure geometric accept/reject predicates (distance, facing,
  AABB, a shared LOS/FOV test) — no damage field, no sink call anywhere, in either callback or
  their depth-1 callees. Every statically-reachable function from the swing-time hit test has now
  been walked to exhaustion over 4 passes with no edge found — a real structural negative, same
  standard as §4.4's hitscan search. Either the mechanism is reached through an indirect/vtable
  call or a Havok collision-response callback (the §2a.5 shape), or the hit-confirmed state feeds
  something outside this call graph entirely. Full detail:
  `scratchpad/melee_hit_confirmed_consumer_round3_2026-10-09.md`. Cleanroom self-check: strict
  regex 0 hits, C-cast-dereference check 0 hits.
