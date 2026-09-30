# Saints Row: The Third — Generic Resource-Construction Dispatch Specification

**Prepared by:** SPEC TEAM (cleanroom reverse-engineering process)
**Phase:** 1, follow-on target scoped and approved by the peer after the `.ccmesh_pc`/`.gcmesh_pc` count×stride investigation (`spec-geometry-format.md` §4.1.2) traced a real scope boundary: the constructor for every registered resource type (`spec-geometry-format.md` §4.3's registration table) is only ever invoked indirectly, through a shared, generic dispatch mechanism — never called directly by name anywhere in the binary. This document is that mechanism.
**Scope:** How the engine generically resolves a resource's paired `c`/`g` files by name, and how it invokes a registered type's constructor with both files' data — the mechanism underlying *every* `c`/`g`-paired format in this project (`.cpeg_pc`/`.gpeg_pc`, `.ccmesh_pc`/`.gcmesh_pc`, `.csmesh_pc`/`.gsmesh_pc`, `.clmesh_pc`/`.glmesh_pc`, and others), not any one format specifically.
**Method:** Ghidra static analysis (decompiler + raw x86 disassembly). Started from the registration table's own consumer function (`FUN_00daf4e0`, `spec-geometry-format.md` §4.3) and walked every other reader of the global arrays it populates, until the actual constructor-invocation call site was found and its argument wiring confirmed byte-for-byte via raw instruction reading (not decompiler output, which drops at least one argument at this call site, consistent with a pattern seen throughout this project wherever a function is reached via an unusual calling convention).
**Cleanroom compliance:** No decompiled code or original identifiers appear below. Function addresses are cited as evidence, not reproduced as identifiers. No literal format/game data is discussed in this document — it's entirely about generic loader mechanics.

**Confidence key** (as in prior specs): **CONFIRMED — empirical**, **CONFIRMED — disassembly**, **HIGH CONFIDENCE — inferred**, **HYPOTHESIS — unconfirmed**, **OPEN / UNKNOWN**.

---

## 1. Overview and headline result

Every one of the ~45 resource types in the registration table (`spec-geometry-format.md` §4.3, `FUN_00700780`) has its constructor/destructor function pointers stored into a shared, global, struct-of-arrays table (one row per type, indexed by type ID) by the registration consumer (`FUN_00daf4e0`). That table is read by a small, cohesive cluster of generic functions — none of them specific to any one resource type — that together: resolve a resource's on-disk filename(s) from its base name plus the type's registered extension(s), open/load the file(s), and invoke the registered constructor with the results. **This is the literal mechanism by which a `c`-side file's constructor receives its paired `g`-side file's data: the pairing is resolved generically, by simple filename+extension substitution, before the format-specific constructor ever runs — not via any content-based cross-reference read from within the `c`-file itself.** This directly and fully closes the scope boundary hit in `spec-geometry-format.md` §4.1.2 items 7–8.

## 2. The per-type registration row — corrected field layout

**Important correction to prior documentation:** the registration table's consumer (`FUN_00daf4e0`) writes each registered type's fields into a shared global array with a 48-byte (`0x30`) stride per type (indexed by type ID). Earlier documentation (inherited into this project's own Ghidra bookmarks) described the two leading fields of each row as "size fields" and a later pair as "ctor/dtor fn ptrs." **Tracing actual usage of these fields (this pass) shows that attribution was backwards for two of the four function-pointer-region fields, and incomplete for the two leading fields.** The corrected, disassembly-confirmed layout (relative to the row's base, i.e. relative to the global `DAT_02a39158`):

| Row offset | Confirmed content | Evidence |
|---|---|---|
| `+0x00` | **Constructor function pointer** (corrected — previously mislabeled "size field") | **[CONFIRMED — disassembly]**: fed from registration record offset `+0x54`; a real registration site's corresponding local variable is a function address; and this exact row field is the one dereferenced and called through by the dispatcher (§4). |
| `+0x04` | **Destructor function pointer** (corrected — previously mislabeled "size field") | **[CONFIRMED — disassembly]**: fed from registration record `+0x58`, same evidence pattern as `+0x00`. |
| `+0x08` | A count/size field, minimum-clamped to `1` if the registration record's value is less | **[CONFIRMED — disassembly, the field and its clamping logic]**; **ANSWERED 2026-09-20 (§8): alignment, in bytes, of the entry's primary (`c`-side) block** (start-address alignment when the container allocator packs entries). Fed from record `+0x5c`. |
| `+0x0C` | A second count/size field, same minimum-`1` clamping | **[CONFIRMED — disassembly]**; **ANSWERED 2026-09-20 (§8): alignment, in bytes, of the entry's secondary (`g`-side) block.** Fed from record `+0x60`. |
| `+0x10` (byte) | A packed flags byte. **Bit `0x4` is confirmed to mean "this type has a second, paired file to resolve"** — it gates the paired-file-resolution logic in §3 and the 6-vs-4-argument constructor call in §4. | **[⚠ CORRECTED 2026-09-20 — see §8.6 item 2: a set *row* bit `0x4` means "no file resolution for this type" (only ID 39); "has a pair" is bit `0x4` of the per-*entry* flags byte, not this row byte. The original attribution is retained above as written.]** **OPEN — the other bits** (row bit `0x02`: see §8.3). |
| `+0x14` | Pointer to an allocated, copied type-name string | **[CONFIRMED — disassembly.]** |
| `+0x18` | **Primary (`c`-side) file extension string** (e.g. the literal text `.ccmesh_pc`) | **[CONFIRMED — disassembly]**: fed from registration record `+0x44`; directly observed being formatted together with a resource's base name to build the primary file's path (§3). |
| `+0x1C` | A secondary field, frequently zero in observed registrations | **[CONFIRMED present]**; **OPEN — purpose.** Fed from record `+0x48`. |
| `+0x20` | **Secondary (`g`-side) file extension string** (e.g. `.gcmesh_pc`) | **[CONFIRMED — disassembly]**: fed from record `+0x4c`, same evidence pattern as `+0x18` — used identically, to build the second file's path. |
| `+0x24` | A fourth field, frequently zero in observed registrations | **[CONFIRMED present]**; **OPEN — purpose.** Fed from record `+0x50`. |
| `+0x2C` (byte) | A copy of the type ID itself | **[CONFIRMED — disassembly.]** |

This correction should be treated as superseding the field-purpose portion of the original `FUN_00daf4e0` Ghidra bookmark comment (the addresses themselves are unaffected — only what several of the fields actually mean).

## 3. Resolving the paired file by name — `FUN_00dd2a70`

This is the actual answer to "how does a `c`-file locate its paired `g`-file." Given a resource descriptor object (built elsewhere, containing at minimum: the resource's base name, its resolved type ID, and the flags byte from §2), and gated on that type's row `+0x10` flag bit `0x4` (the "has a pair" bit):

1. Build the **primary** filename by combining the resource's base name with the type's `+0x18` extension string (§2).
2. Resolve/open that file, store the result into the descriptor's own slot 1 (its `[1]`-th field, in 4-byte-element terms).
3. Build the **secondary** filename the same way, using the `+0x20` extension string.
4. Resolve/open that file too, store the result into the descriptor's slot 2 (`[2]`).

**[CONFIRMED — disassembly, both branches, the shared helper function used for resolving each name, and the exact fields written.]** Two sibling functions (`FUN_00dd35e0`, `FUN_00dd29c0`) independently confirm the same "format the base name with a type's stored extension string" pattern, reinforcing that `+0x18`/`+0x20` are genuinely extension strings and not some other kind of per-type data.

**This mechanism is completely generic** — it runs identically for any type whose registration row has the "has a pair" flag set, regardless of what the two extensions actually are. It does not special-case `.ccmesh_pc`/`.gcmesh_pc`, or read anything from either file's *content* to find the other — the pairing is pure filename convention (same base name, different extension), resolved before any format-specific parsing code runs.

## 4. Invoking the constructor — `FUN_00dd2e30`, the generic dispatcher

This is the function that actually calls through each registered type's constructor pointer (§2's `+0x00` field) — the piece of code the earlier investigation established must exist but hadn't located (`spec-geometry-format.md` §4.1.2 item 8). Given the same resource descriptor object from §3:

1. First makes a call through a **separate backend-object vtable** (indexed by a byte field elsewhere in the descriptor, into a different global array, `DAT_02a3c170`) to load the **primary** file's raw content into a local buffer. This is the same general backend-dispatch-by-index pattern already documented dynamically for the container format itself (`spec-vpp-container.md` §3.6–3.7) — **[HIGH CONFIDENCE — inferred, same pattern, not re-confirmed as the identical mechanism this pass]**.
2. Fetches the constructor pointer from the type's row (§2 `+0x00`).
3. **Branches on the same "has a pair" flag bit from §2/§3**: 
   - If **not** paired, calls the constructor with 4 arguments: `(name, ?, primary-file-buffer, descriptor-slot-1)`, plus two hardcoded zero arguments.
   - If **paired**, calls the constructor with 6 arguments: `(name, ?, primary-file-buffer, descriptor-slot-1, descriptor-field-4, descriptor-slot-2)` — **where `descriptor-slot-2` is exactly the secondary (`g`-side) file's resolved buffer from §3.**

**[CONFIRMED — disassembly, exact argument count and exact source of every argument, verified by reading the raw x86 instructions at the indirect call site (`CALL EAX`/`CALL EDX`), not decompiler output — the decompiler renders both call sites with fewer visible arguments than are actually pushed, the same argument-dropping behavior seen at every other call boundary traced during the `.ccmesh_pc` investigation.]**

This is the literal mechanism: **the constructor receives the paired file's already-resolved buffer directly as a plain argument, only when its type is registered as paired.** **⚠ Refined 2026-09-20 (§8.6 item 1): of the six arguments, the 4th and 6th are the primary and secondary byte *sizes*, and the secondary *buffer* is the 5th — the labels "descriptor-slot-1/-2" above name the sizes, not buffers.** No format-specific code — not `.ccmesh_pc`'s, not any other type's — participates in finding or opening the second file; it's handed over, already loaded, by this one generic function.

## 5. Closing the loop: this *is* what `.ccmesh_pc`'s constructor receives

`spec-geometry-format.md` §4.1.2 traced a value — there called `.ccmesh_pc`'s constructor (`FUN_00751f60`)'s 5th parameter — all the way down through 5 more function boundaries to become the base pointer `FUN_00e71740` uses to read vertex data out of the cross-referenced buffer, and confirmed that value is never computed by any `.ccmesh_pc`-specific code. **Matching the call site found in §4 against `FUN_00751f60`'s known parameters (its 3rd parameter is independently confirmed elsewhere to be the primary-file buffer, matching this call's 3rd argument exactly) shows `FUN_00751f60`'s mystery 5th parameter is exactly this dispatcher's 6th, paired-only argument — `descriptor-slot-2`, i.e. `.gcmesh_pc`'s buffer, resolved by simple filename substitution in §3, before `FUN_00751f60` ever runs.** **[CONFIRMED — disassembly, positional argument match against the independently-confirmed 3rd-parameter identity; the exact mapping of the dispatcher's other arguments to `FUN_00751f60`'s remaining parameters was not individually re-verified, since it isn't needed to answer the original question.]**

**This resolves `spec-geometry-format.md` §4.1.2 items 7–9 completely at the architectural level.** The remaining gap in that document (the *exact byte offset* within `.gcmesh_pc` where the count×stride vertex data begins) is now a much narrower, format-specific question again — `.gcmesh_pc`'s buffer is confirmed to arrive as a plain, already-loaded pointer; finding the exact offset within it is back to being a `.ccmesh_pc`-specific disassembly task (tracing `FUN_00e71310`'s own cursor logic against this now-known buffer), not a generic-infrastructure one.

## 6. What this unlocks for other formats

Because §3–§4's mechanism is completely generic (gated only on the registration table's per-type "has a pair" flag, §2), **it applies identically to every other paired format's constructor** — including `.clmesh_pc`/`.glmesh_pc` and `.csmesh_pc`/`.gsmesh_pc`, both already known to share this project's registration table. Any of those formats' constructors, once found and decompiled, can be expected to receive their paired `g`-file's buffer the exact same way (a plain incoming argument, not a content-derived cross-reference) — this is now a standing expectation for this project, not something that needs re-discovering per format.

## 6a. The null-constructor mode (added 2026-09-10)

The dispatcher described above tests the type's constructor pointer for null **in both the paired and unpaired branches**, and on null jumps past the call straight to the shared success tail — which sets the "loaded" bit and returns success. **A constructor that runs and returns false is a failure; a constructor that is absent is a success.** Five registered types (29, 30, 39, 44, 45) rely on this: the generic pipeline resolves, reads and decompresses their files and leaves the buffer registered, but no per-type parse ever happens. **[CONFIRMED — disassembly.]** See `spec-ctorless-types.md`.

## 7. Open Items

0. **A complete inventory of every registered type is now available** — see **`spec-format-inventory.md`**: all 43 registered resource types with their type IDs, internal names, per-platform extensions, constructor/destructor addresses, and characterization status. That document also resolves the per-platform extension-table indirection referenced throughout this one (index `0 = pc`, `1 = ps2`, `2 = ps3`, `3 = xbox`, `4 = xbox2`), and confirms the corrected field layout in §2 against all 43 entries rather than the handful originally sampled.
1. **The backend-object vtable** (`DAT_02a3c170`, §4) that actually performs the primary file's load — not characterized this pass beyond noting the pattern matches the already-documented container-backend-dispatch concept. Understanding it fully would explain exactly *how* a file gets from "on disk inside a `.vpp_pc`" to "loaded buffer," generically, for every resource type — a natural next layer if this thread is picked up again.
2. **Row fields `+0x1C`/`+0x24`** (§2) — confirmed to exist, usually zero in the registrations observed, purpose unknown. Possibly per-type callback/validation function pointers used only by some types.
3. **The descriptor object's own full field layout** — only the specific fields touched by §3/§4 were identified (name, type ID, flags, slots 1/2, a "field 4"). The object is clearly richer than what was needed to answer the immediate question.
4. ~~**`FUN_00dd2e30`'s "field 4" argument** (passed as the paired-case constructor's 5th argument, alongside the `g`-buffer as its 6th) — not identified.~~ **ANSWERED 2026-09-20 (§8.6 item 1): the 5th argument *is* the secondary (`g`-side) buffer pointer; the 6th is the secondary size. [CONFIRMED — disassembly; HIGH CONFIDENCE for the backend-call role.]**
5a. **The paired-file resolver must tolerate an absent secondary file.** `spec-morph-format.md` §6 shows the `.cmorph_pc`/`.gmorph_pc` type registers a secondary extension that **never ships** (2,946 primaries, 0 secondaries) because every shipped morph uses an inline-payload mode. So a missing `g`-file is a normal, successful load for at least one registered type — an implementation of §3 must not treat it as an error. **[CONFIRMED — empirical.]**
5. **Whether every paired type actually receives its `g`-buffer at the same argument position** (§6) — asserted by mechanism, not individually re-verified per format. A quick check against a second format's constructor (e.g. `.clmesh_pc`'s, once its own stub constructor is resolved — `spec-geometry-format.md` §4.2) would upgrade this from "expected" to "confirmed for 2/N formats."

## 8. The two per-type "size" fields are alignments — the container allocator (added 2026-09-20)

**Added 2026-09-20 (agent T, `gp_sz1`).** Answers `spec-format-inventory.md` §6 item 5 and the two rows §2 left as "count/size field — OPEN" (`+0x08`, `+0x0C`). **Result: they are per-type *alignments, in bytes*, used when the container allocator packs a container's resources into one memory block — `+0x08` for a resource's primary (`c`-side) block, `+0x0C` for its secondary (`g`-side) block.** Not granularity, not a budget class, not dead data. Method: anchored on the two record fields and the two tables, followed only real cross-references (no predicate search), plus one bounded census of the row array's own address range. Cleanroom: no decompiled code below; addresses are evidence.

### 8.1 Provenance of the two values **[CONFIRMED — disassembly]**

- The registration function fills, in each on-stack record, offsets `+0x5c` and `+0x60`; the record consumer (`FUN_00daf4e0`) copies them to row `+0x08` / `+0x0C`, each clamped to at least `1`. **The clamp now has its reason: both are used as divisors (§8.3), and a registered `0` means "no requirement" = alignment 1.**
- Only **6 of the 43 types** take them from the two platform-indexed tables: IDs **3, 10, 14, 16, 17, 18** (the PEG family — Vehicle PEG, Pcust peg, Pcust logo peg, Peg, Low Mips, AL peg). The other 37 use per-type literals identical on every platform. Replayed write-by-write (`tools/harnesses/sz_align_replay.py`, the inventory §5 stale-value discipline) — `(row +0x08, row +0x0C)`:

| `(+0x08, +0x0C)` | Type IDs |
|---|---|
| tables `A[p]`, `B[p]` (PC = `8`, `16`) | 3, 10, 14, 16, 17, 18 |
| `16, 16` | 1, 4, 7, 11, 12, 13, 15, 20, 21, 29, 30, 36, 39 |
| `16, 128` | 5, 9, 19 (Character mesh, Pcust mesh, Static Mesh) |
| `128, 128` | 31 (Level Mesh) |
| `4, 4` | 2, 8, 23, 24, 25, 26, 27, 32, 33, 34, 43, 44, 45 |
| `16, 0` | 22, 35, 37, 41 |
| `4, 0` | 38, 42 |
| `128, 0` | 40 (Level Mesh Collision) |

- Tables: **A** at `0x0124afec` = `8/0/4/0/4` (read only by the registration function, 6 sites); **B** at `0x0124b000` = `16/0/128/0/4096` (registration function 6 sites, **plus 7 sites in the memory-pool builder `FUN_005d58f0`**, §8.4).
- **The platform index is the literal `0` in this executable**: the registration function's sole call (`0x005d2333`, inside `FUN_005d1a30`) passes `0`, and the pool builder is called with `0` (`0x005d53d8`, in `FUN_005d4a60`). So the PC build always uses **`8` (primary) and `16` (secondary)** for the six table-driven types.
- A static initializer (`0x01014d30`; `0x33` iterations of 5 rows) presets every row's `+0x08`/`+0x0C` to `0xFFFFFFFF` before registration.

### 8.2 Who reads them — exhaustive **[CONFIRMED — disassembly + array census]**

A census of every reference to a `+0x08`/`+0x0C` cell anywhere in the row array (stride `0x30`, base `0x02a39158`) finds exactly: writers = the record consumer and the static initializer; readers =
- **`FUN_00dd25c0`** — 3 reads of `+0x08`, 6 of `+0x0C`. Callers: `FUN_00db24e0` (`0x00db2771`) and `FUN_00db1d50` (`0x00db1dcc`; itself called from `FUN_00db2f00`, `FUN_008570a0`).
- **`FUN_00db24e0`** (the container allocation routine; callers `FUN_00daf330`, `FUN_00db3450`) — one direct read of each (`0x00db2904`, `0x00db294c`).

Nothing else reads them. The functions that hold a row *pointer* (`FUN_00dd2e30`, `FUN_00dd22c0`, `FUN_00dd2f50`, `FUN_00dd2a70`, `FUN_00daf5c0`/`FUN_00dd2350`) touch only `+0x00`, `+0x10`, `+0x14`, `+0x18`–`+0x20` and `+0x2C`. **The fields are live on the PC path** — the container allocator is the same generic code for every platform.

### 8.3 What the code does with them **[CONFIRMED — disassembly]**

The unit is the runtime **container** (its own strings: "Error allocating container … Primitive … was already allocated"): an array of resource entries, one per manifest sub-block (§8.5). An entry, as read by these functions: `+0x04` size of the primary block, `+0x08` size of the secondary block, `+0x0C`/`+0x10` allocation handles, `+0x14` type ID, `+0x15` flags, `+0x16` group ID, `+0x17` pool ID.

- **Sizing pass (`FUN_00dd25c0`), per entry, over a running CPU offset and a running "max alignment" (start `0` and `1`)**: raise max-alignment to `row+0x08` if larger; round the offset **up to a multiple of `row+0x08`** (`x + (a − x mod a)` when `x mod a ≠ 0`); add entry `+0x04`. Then, if entry `+0x08 ≠ 0`, the same with **`row+0x0C`** — into a **separate GPU-side offset/alignment pair** when the row's flag byte has bit `0x02` *and* the entry's flag byte has bit `0x04`, otherwise **into the same CPU pair** (secondary block packed after the primary).
- Result `(cpuSize, cpuAlign, gpuSize, gpuAlign)` goes to the pool object's allocate slot (vtable `+0x2C`; for one pool class `FUN_00dd5830`, which forwards `(size, alignment)` pairs to the underlying heap's allocate slot `+0x38`). **The block's own alignment is the maximum member alignment; each member sits at an offset that is a multiple of its own type's alignment.** The placement pass in `FUN_00db24e0` repeats the same round-ups to assign each entry's address.
- So it is a **start-address alignment**, not a size granularity: sizes themselves are never rounded up; only the offset at which the next block begins is.

### 8.4 The other platforms fit **[CONFIRMED — disassembly for the mirroring; HIGH CONFIDENCE — inferred for the rationale]**

`FUN_005d58f0` builds the engine's startup memory pools and contains a per-platform switch (`0` PC, `2` PS3, `4` Xbox 360) with hard-coded CPU-side and GPU-side alignments: **PC `8`/`16`, PS3 `4`/`128`, Xbox 360 `4`/`4096`** — exactly `A[p]`/`B[p]`. Table **B** is also passed **directly as the alignment argument of `FUN_00ea3362`** (a wrapper over the CRT aligned allocator `FUN_00ea329a`, which rejects non-powers-of-two) for GPU-side arenas ("weapon high res gpu", "interface image gpu compacting", "lut gpu"). So `128` (PS3) and `4096` (360) are large GPU-memory alignments; 128 = a cache-line/DMA size and 4 KiB = a page size are the natural readings, but **which hardware rule motivated them is not derivable from this binary.** All registered values are powers of two, matching the CRT requirement.

### 8.5 Something observable **[HIGH CONFIDENCE — inferred; small n where stated]**

Harness `tools/harnesses/sz_asm_trailer_probe.py`. The entry reader `FUN_00dd2f50` fills entry `+0x04`/`+0x08` from a stream; after the name it consumes exactly **13 bytes** (five 1-byte reads and two 4-byte reads — widths checked; the 4-byte read byte-swaps when the stream is flagged big-endian). `.asm_pc` sub-blocks (`spec-asm-format.md` §4) end in exactly such a 13-byte trailer: byte 0 = **the registered type ID** (35 in `decals`, 15 in `effects`/`preload_effects`, 16/17 on `.cvbm_pc` entries, 21 on an `.anim_pc`), then `u32` primary size (= the entry's own uncompressed size), `u32` secondary size, then a group byte (`0xFF` = none). The layout match is exact, but the manifest reader was not traced back to `FUN_00dd2f50`'s caller. The sequential walk of `decals.vpp_pc`'s manifest ends on exact EOF (14/14 records). Findings:
- **Sizes are stored unpadded**, so the padding really is applied at runtime: a Peg (ID 16, PC alignment 8) `.cvbm_pc` entry has primary size `116` (= 14×8 + 4) and secondary `87,376` (= 16 × 5,461); an Anim File (alignment 16) has `90`. *(n = 1 for the Peg with a secondary; illustrative, not proof.)* Other types happen to be pre-padded (314/314 distinct VFX and 12/12 Material Library sizes are multiples of 16) — no evidence either way.
- **The secondary alignment tracks the secondary extension**: all **19/19** types with a registered `g`-side extension have `+0x0C ≠ 0`; all **7/7** types with `+0x0C = 0` (22, 35, 37, 38, 40, 41, 42) have none, and Material Library entries carry secondary size `0` (12/12 distinct). *(The converse does not hold: several `g`-less types carry a nonzero, never-used `+0x0C`.)*
- Trailer byte 1 looks like the memory-pool ID: `0x1E` on `.cvbm_pc` Low-Mips entries, and `FUN_005d58f0` registers pool `0x1E` as "mip_streaming". *(HYPOTHESIS-grade, n = 2; not pursued.)*
- Incidental, for `spec-asm-format.md`'s owner: the per-record header before `entry_count` is **3** bytes in the decals sample (`1a 80 00`), not the 1 byte modelled in §4 (the documented 19-byte zero-entry tail is consistent with 3), and `entry_count` needs reading as `u16`; the sequential walker stops on some `effects` record variants it does not model. **Resolved 2026-09-20 (agent Y): the 3 bytes are `container_kind` + a `u16` flags word, `entry_count` is a signed `s16`, and the sequential walker stops because the per-entry `(primary, secondary)` size table precedes all names rather than being interleaved with them — `spec-asm-format.md` §6–§10.**

### 8.6 Corrections to §2–§4 that this pass forced **[CONFIRMED — disassembly]**

1. **Entry `+0x04`/`+0x08` are byte *sizes*, not resolved buffers.** They are what `FUN_00dd25c0` adds into running offsets, and they are the constructor's **4th** and **6th** arguments. The constructor's **5th** argument is the **secondary buffer pointer** (and the 3rd the primary buffer), both produced by a backend call that resolves the entry's two allocation handles (`+0x0C`, `+0x10`). So §4's "(name, ?, primary buffer, slot-1, field-4, slot-2)" reads, in truth, **(name, caller arg, primary buffer, primary size, secondary buffer, secondary size)** — and the mesh constructor's `param_5` (§5) is the 5th argument, not the 6th. **§7 item 4 ("field 4") = the secondary buffer pointer** (HIGH CONFIDENCE for the "resolve handles" role of the backend call; the call shape, not its callee, was read).
2. **Row byte `+0x10` bit `0x4` is not "has a paired file".** In the pair-resolution routine (`FUN_00dd2a70`) a set *row* bit `0x4` makes the routine **return without resolving anything** (only ID 39, `Buffer`, sets it — per the replay); the "paired" test is the **entry's own** flags byte (`+0x15`) bit `0x4`, which is also what selects the 6-vs-4-argument call in `FUN_00dd2e30`. (Row bit `0x02` selects the separate-GPU-accumulator behaviour in §8.3; PC additionally sets `0x10` on IDs 17 and 18.)

### 8.7 Open

- The heap allocate slot's (`+0x38`) own handling of the alignment argument was not traced (it sits in the position every other caller uses for alignment; `FUN_00daf4e0` passes `1` for byte strings).
- The meaning of row flag bit `0x10` and entry flag bits other than `0x04`/`0x02`/`0x80`.
- Why the PEG family is platform-table-driven while meshes are fixed at `128` everywhere is not explained (HYPOTHESIS: GPU texture layout is the platform-variable part).
