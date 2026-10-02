# Named-object resolution: where script names like `'Killbane'` get registered (Team B blocker, mission `dlc1_mm_06`)

Team A, 2026-10-02. Investigative pass against the real executable (local Ghidra project
`tools/ghidra_projects/SR3`, program `SaintsRowTheThird.exe`, headless `-readOnly -noanalysis`,
`ghidra/CrreishDump.java`). Dump output lives in this session's scratchpad (`nor/` sub-folders) and is
not committed; everything below is described in prose.

**The question.** A mission script passes a string such as `'Killbane'` to a Lua-bound engine
function and gets back a live, addressable character/object. The read side is already well mapped:
`spec-lua-api-behaviour.md` has roughly a dozen per-kind resolvers (0x005982e0, 0x005e4dd0,
0x005e4e30, 0x005eab60, 0x005f4c30, 0x00734e90, 0x00739690, 0x004584e0, 0x00a3de90, 0x0071f0d0 and
others) all called `thiscall` on one fixed singleton at 0x02442750, and `review/exe-notes-2026-10-01/
rederive_27.md` already read one of them (0x005f4c30) far enough to see a name-keyed hash map at
singleton offset +0x2660 with an entry count at +0x265c, bucket hash 0x00dab330 and a case-insensitive
compare. What nobody has traced is the **write side**: who inserts a name into that map, and when.
Three candidate answers were posed: (a) data baked into the mission package (`spec-mission-packages.md`);
(b) a separate name table parsed from some other already-documented file format; (c) a mission script
spawning/naming the object at runtime through a Lua call (or the spawn/placement code registering an
authored name when the object comes into existence).

**Standing restriction, kept throughout.** The `.czn_pc` object-placement/property-stream interior is
parked: no real `.czn_pc` file's object/property bytes are opened or read here, and no dump mode that
would print literal data bytes from such a container was requested. Reading the engine's *code* that
parses that stream is in scope and is what this note does. If the trail ends at "the name is a field of
a `.czn_pc` placement record", that is the answer and the trail stops there.

**Inputs read before dumping anything.** `spec-lua-api-behaviour.md` (the 0x02442750 citations, §12.10,
§12.12, §27.2's resolved 0x006d6f30 body); `rederive_27.md` §27.2 (the +0x2660 map); `spec-tables-
traffic-ai.md` §7/§26 (the `Spawn_Point` lookup, flagged OPEN on who owns the registry); `spec-mission-
packages.md` (what is actually in the mission `.str2_pc` packages); `spec-zone-data-format.md` §9.3/§9.5
(the generic hash-keyed property mechanism and the top-level `.czn_pc` tag walker 0x00864c60 /
0x007512f0 / dispatcher 0x008652d0); `WALLS.md` (the census-undercount lesson, and the note that
0x005eb390 is NOT a method of this singleton); `HANDOFF.md` §27 item 1 (the 0x2237 scope question).

Labels follow the house style: **CONFIRMED — disassembly** = read in a listed instruction stream of
this pass's dumps; **HIGH CONFIDENCE** = follows from a dumped instruction or reference list but the
body it points at was not dumped; **HYPOTHESIS** = plausible, not settled; **OPEN** = not settled,
collected at the end.

---

## 0. Starting point (from prior passes, not re-derived here)

- 0x02442750 is one fixed object, not a per-kind table. Every per-kind resolver loads its address
  into ECX as a literal and takes the name string as one stack argument. (`spec-lua-api-behaviour.md`,
  many sightings; CONFIRMED there.)
- 0x005f4c30 (the mission-kind resolver) reads the singleton's +0x265c as an entry count and walks a
  hash map at +0x2660; the hash is 0x00dab330; the compare is case-insensitive; the resolved object's
  +0x33 byte (bit 0x10 = reject) and +0x34 byte (class index into the 0x02cc9900 descriptor rows) are
  then tested on the caller's side. (`rederive_27.md` §27.2; CONFIRMED there.)
- Nothing in `spec-mission-packages.md` describes a name table: the fifteen mission `.str2_pc`
  packages hold only two `.cefct_pc` effect files between them (§2), so candidate (a) has no data
  carrier in the packages this project has opened. That is a negative about the *packages*, not yet
  about the mechanism — a mission could still register names from an `.xtbl` or a Lua table.

---

## 1. Investigation trail

(Appended incrementally below as each dump is read.)

### 1.1 Every reference to the singleton (dump `nor/xref1`, 863 uses in 386 functions) — CONFIRMED

- All but one reference are `MOV ECX, 0x2442750` (or one `PUSH`): the address is only ever used as a
  `this`. The single WRITE is at 0x00ffb76f, inside an undefined-function static initialiser starting
  0x00ffb760, which stores the vtable pointer 0x0116467c into the first dword. So the registry is a
  static C++ object with a vtable, constructed at CRT start-up, never re-pointed. CONFIRMED.
- The references cluster in three places: the Lua binding wrappers (0x00a2xxxx–0x00a6xxxx, as the spec
  already catalogues), the engine's own gameplay code (0x0059xxxx–0x0073xxxx), and a small dense block
  at 0x00853820–0x00854bb0 that sits next to the liveness gate 0x00853b10 — the registry's own method
  module. One reference, 0x00866950, sits in the zone module (0x863000–0x866000, the same module as
  the `.czh_pc` header parser and the `.czn_pc` tag dispatcher 0x008652d0). That one is followed up in
  §1.4.

### 1.2 The name map's shape (dumps `nor/func1`, `nor/func2`) — CONFIRMED

0x005f4c30 (already read by `rederive_27.md`) is a 20-instruction wrapper: if the count at registry
+0x265c is positive it adds 0x2660 to `this` and calls 0x004588f0(name, &result). So the map is an
embedded sub-object at **0x02444db0** (= 0x02442750 + 0x2660), and 0x004588f0 is the map's own
find routine. Read in full:

- The map sub-object holds a bucket-array pointer at +0x1c and a bucket count at +0x20.
- Hash: 0x00dab330(name, bucketCount) — `h = (h * 33) XOR tolower(c)` over the string, then
  `h mod bucketCount`. This is the same multiply-by-33 bucket hash the texture registry uses
  (`spec-texture-format.md` §8.2). Case-folded, so lookups are case-insensitive.
- Chain node layout: +0x00 = the value (an object pointer), +0x04 = next node, +0x0c = the key string
  stored inline (the compare is `_stricmp(name, node + 0xc)`). Offset +0x08 is not touched by find.
- On a hit the node's +0x00 value is written to the caller's out-pointer and AL = 1.
- 0x004588f0 has ~30+ callers, including every per-kind resolver the spec lists (0x005982e0,
  0x005e4dd0, 0x005e4e30, 0x005eab60, 0x004584e0, 0x005f4cd0, 0x0062a1f0 ...). So the per-kind
  resolvers really are thin filters over ONE map, as the spec hypothesised: the kind check is done on
  the resolved object's +0x34 class byte via the 0x02cc9900 descriptor rows, after the lookup.

Direct references to the map sub-object 0x02444db0 (dump `nor/xref2`): 77 uses in 40 functions. The
registry's method module itself does not load that address — it always goes through +0x2660 from
`this` — so the 40 are outside callers that address the map directly (several are Lua wrappers that
inline the lookup). Which of them INSERT rather than find is what §1.3 works out.

### 1.3 Second table: the 8-byte-handle map at 0x024433a8 — CONFIRMED (shape), HIGH CONFIDENCE (role)

Two of the registry-module functions read (0x00854410 and 0x00866950) resolve objects not by name
but by an 8-byte value (two dwords, tested jointly for non-zero) through 0x00458230(&handle,
0x031d152c) with `this` = 0x024433a8 — a different static table with ~2,400 call sites binary-wide.
Both then apply the same +0x33 bit 0x10 reject and the same 0x02cc9900 class-row test (here bit 0x2 at
row +6). So objects have a numeric 8-byte handle as well as an optional name; the name map at
+0x2660 is a secondary index. 0x00854410(handleLo, handleHi, flag, arg) is a guarded "destroy by
handle": unless the object is a currently-protected kind (class-row +6 bit 0x2, +0x33 bit 0x4 clear,
+0x34 != 0xff, global byte 0x024d4461 == 1, 0x008addb0(obj,1) false, obj +0x3c non-null, flag 0) it
forwards to 0x00457820(handleLo, handleHi, arg) on the registry. HIGH CONFIDENCE on "destroy" from
the call shape (the registry-module sibling 0x00853ea0 runs the identical guard and then calls
0x00457730 on the registry); bodies of 0x00457820/0x00457730 are read in §1.5.

### 1.4 The registry object itself (dumps `nor/range1`, `nor/func5/func_0x00854bd0`, `nor/ptrs1`) — CONFIRMED

The static initialiser at 0x00ffb760 calls the constructor 0x00854bd0 on 0x02442750, then overwrites
the vtable with 0x0116467c (a derived class; the constructor had installed the base vtable 0x01164648)
and registers the destructor 0x010189e0 with `atexit`. The constructor lays out, inside the one
object: a handle table at +0xc58 (vtable 0x01164608, count at +0xc64), a class-name table at +0xc6c
(vtable 0x01164630, initialised by 0x00853950), and the name map at +0x2660 (initialised by
0x00d9f7b0 — the engine's general hash-map constructor — then its +0x08..+0x20 fields zeroed: so the
bucket array/count at +0x1c/+0x20 are filled in later, by whoever "enables" the map, which is why every
reader first tests the count at +0x265c). Base vtable 0x01164648, in slot order: 0x00457820 (destroy
by handle), 0x00457730 (destroy object), 0x00457370, 0x00457880, 0x00458c90, 0x004578b0, 0x004578c0,
0x004bf550, 0x00457900, 0x00457340, 0x00456e80, **0x00456d80 (slot 11, offset +0x2c)**. The derived
vtable 0x0116467c starts 0x00853770, 0x008539a0, 0x00457370, 0x00853710, 0x00854900, 0x00854c90,
0x00853740, 0x00853750, 0x00853c70, ... (its slot 11 is read in §1.7).

### 1.5 The ONLY name-insert path (dumps `nor/func4`, `nor/func5`, `nor/calls1`) — CONFIRMED

Walking the map's own code outward:

- 0x00458850(name, createFlag) on the map sub-object is **find-or-insert**: same hash and chain walk
  as find; on a miss, if `createFlag` and the map's free count at +0x0e (u16) is positive, it takes a
  node from the map's own pool (0x00458a10), `strncpy`s the name into node+0x0c with a hard cap of
  **0x21 bytes**, records the bucket index at node+0x30, links the node at the head of its bucket
  (+0x04 next, +0x08 prev), and returns the node. Raw call scan: **one caller**, 0x0045769e.
- 0x004583d0(name) on the map is **remove by name** (unlink, return node to the pool via 0x004587d0).
  Two callers: 0x00456d00 and 0x00457650.
- **0x00457650(obj, name)** on the registry is the **set-object-name** method: refuses if the map's
  count at +0x265c is not positive or the object's +0x33 bit 0x10 is set; with a non-null name it
  requires 1..32 characters (`strlen <= 0x20`, first byte non-zero), calls find-or-insert with
  createFlag = 1, stores the object pointer into node+0x00 and **stores node+0x0c (the name copy)
  into the object's own +0x18** — so an object's +0x18 is a pointer to its registered name, living
  inside the registry's node, which is why the per-kind resolvers never need a second lookup. With a
  null name it removes the object's current name (read from +0x18) and clears +0x18. Raw call scan:
  **two callers**, 0x00456dfe (inside 0x00456d80 = base vtable slot 11) and 0x0085382f (inside the
  two-argument wrapper 0x00853820).
- 0x00853820's only caller is 0x00a28260, which names the player objects: it formats `"#PLAYER%d#"`
  for d = 1, 2, takes the first one not already resolving (through the map, class-row +6 bit 0x1,
  liveness) and assigns it. This is the registered origin of the `#PLAYER#` names the spec's
  resolvers special-case; it is not a mission-object path.
- 0x00456d00(obj) with EAX = registry is the inverse: removes the name (if +0x18 set), removes the
  handle from the +0xc58 table (decrementing +0xc64), then calls the class descriptor's slot 1 on the
  object (class destroy). Called from 0x00457c60, 0x00457a50, 0x00456e30 and 0x00456e80.

So, apart from the player special case, **every name reaches the map through the registry's virtual
slot 11 (0x00456d80 or an override)**. There is no Lua-bound function that calls 0x00457650 or
0x00458850: none of the Lua wrappers in the 863-site reference list call either, and the raw call
scans leave no undisassembled caller. CONFIRMED (two exhaustive raw scans, 2 + 1 hits).

### 1.6 Who calls slot 11: the batch object deserialiser 0x00457c60 (dump `nor/func5`) — CONFIRMED

0x00457c60(batch, ownerHandlePtr, arg3) is a registry method with one caller (the wrapper 0x008538f0,
raw scan: 1 hit), and 0x008538f0 has exactly two callers (raw scan): **0x00863e90 in the zone
module** and 0x00a856d0. Read in full (330 instructions), it is a serialised-object batch loader:

1. Refuses unless the registry's byte +0x26a4 is set and `batch` is non-null; does nothing if the
   batch's record count (`batch`+0x08) is zero.
2. Through the registry's allocator object at +0x54 (vtable +0x38 = allocate, +0x60 = free) it takes
   `count * 0x28` bytes of 40-byte record slots and `count * 4` bytes for an object-pointer array.
3. Opens a read stream over the batch's data at `batch`+0x18/+0x1c via 0x00dab0e0(ptr, len,
   0x0129a85c, 0, 0) — the same generic stream family `spec-zone-data-format.md` §9.3 already names
   (0x00da7f30 = seek, 0x00da8090 = tell, 0x00da8070 = at-end, 0x00da9210 = align-to-4, 0x00daa440 =
   close).
4. Per record: takes the stream's current data pointer (0x00da8090 returns base + cursor of a
   memory-mapped stream, dump `nor/func8`), calls 0x00456760(record, pointer) to copy the record's
   fixed head, seeks forward by `record+0x1a (u16) + 0x1c` bytes (so the head is 0x1c bytes plus a
   variable tail whose length is stored at head+0x16), aligns to 4. If the caller's owner handle is
   non-zero it is copied into record+0x08/+0x0c. **record+0x10 is a 32-bit class-name hash** (an
   integer, not a string: 0x00dab2b0 is the engine's integer mixer, already identified in
   `spec-extensionless-types.md` §4.1, applied here with 0x1fe = 510 buckets — dump `nor/func8`
   confirms the body; an earlier draft of this note called it a string pointer, which was wrong);
   the bucket is looked up in the registry's class-name table at +0xc6c (0x004586a0(hash, bucket)),
   and the byte found at registry+0x245c+index is the object's class index into 0x02cc9900. The
   class descriptor's vtable slot 3 (+0xc) must report capacity > 0.
5. Handle: record+0x00/+0x04 if non-zero, else a fresh one from 0x004576e0; a record whose handle
   already resolves in the +0xc58 handle table is skipped (no duplicates).
6. The class descriptor's slot 0 constructs the object; object+0x10/+0x14 = record+0x08/+0x0c (the
   owner handle), object+0x08/+0x0c = its own handle; 0x00458110 inserts it into the handle table;
   object+0x30 (u16) = record index.
7. Second pass, for every constructed object whose +0x33 bit 0x10 is set (newly constructed, not yet
   initialised): follows the owner-handle chain up to 16 levels and, from the outermost down, calls
   **registry vtable slot 11 (+0x2c) with (object, its 40-byte record, 0)**. If that returns false the
   object is torn down with 0x00456d00.
8. Closes the stream and frees the record slots.

So slot 11 is the per-object "initialise from your serialised record" step, and the base
implementation 0x00456d80 is where 0x00457650 (set name) is called. The name therefore comes from the
serialised record, i.e. from the data the batch was built over. What that data is, is settled by the
callers of 0x008538f0 (§1.7).

### 1.7 The data behind the batch: the zone module and tag 0x2234 (dumps `nor/func6`, `nor/ptrs2`) — CONFIRMED

**0x00863e90 is a step of zone load.** Its one caller is 0x00865f90, a sequence of ten "parse this
part of the zone" steps (0x00863df0, 0x00865ba0, 0x00863c80, 0x00863e90, 0x00864330, 0x008644b0,
0x00864770, 0x00864840, 0x008640a0, 0x008648c0), each returning a boolean and aborting the chain on
failure — the same 0x863000–0x866000 module `spec-zone-data-format.md` §9.5 places the `.czh_pc`
header parser and the `.czn_pc` tag dispatcher in. 0x00863e90 does the following:

1. Calls 0x00863430(buffer, length, cursor, tagId = the dword at 0x013026d0, &recordLength,
   &extra). 0x00863430 is a top-level `{tag, length}` walker in exactly the compiled form §9.5
   describes — `AND 0x80000003`, the branching `(x-1 | 0xfffffffc)+1` round-up, `AND 0x7fffffff` to
   strip the tag's high bit, an 8-byte header widened to 12 when bit 31 is set — stepping record by
   record until the stripped tag equals the wanted id, returning the payload offset or -1.
2. **The dword at 0x013026d0 is 0x2234.** The executable's own `.data` holds, right there, a table
   of `{id, description, name}` triples for the zone record ids: 0x2234 is described by the literal
   string `"Game objects, as placed in the WE"` (next entries: 0x2235 `"Navmesh"` / `"The navmesh
   data for the zone"`, 0x2236 `"Traffic"`, 0x2237 `"WE geom"` / `"Editor created geometry
   (patches, roads, path deform meshes"`, 0x2238 `"Sidewalks"`, 0x2239 `"Trailer"`). This is read
   from the executable's data section, not from any zone file; it names the 0x2237 id the project
   already decoded, and settles which top-level record is the object-placement stream: **0x2234**.
3. If the record is absent (-1) the step succeeds trivially. Otherwise 0x0045bc20(buffer, offset,
   length) builds a "batch" descriptor (count at +0x08, data pointer/length at +0x18/+0x1c — the
   shape 0x00457c60 consumes) and stores it at zone+0x11c; this is the same zone+0x11c that the
   zone-unload step 0x00866950 later walks as a list of object handles (§1.3) to destroy them and
   then clears.
4. Depending on a version value (the `extra` out-parameter of the walker, compared against 0x1c):
   below 0x1c it calls 0x008538f0(batch, 0x01165540, zone+0x7c) — i.e. 0x00457c60 with a null
   owner handle (0x01165540 is eight zero bytes in `.rdata`, followed by the unrelated string
   `missing_mesh.clmesh_pc`); at or above 0x1c it calls 0x00853910(batch, zone+0x130 & 1, zone+0x7c)
   → 0x00457a50, the registry's older one-record-at-a-time loader, which reads the same record head
   (0x00456760), resolves the same class-name table, constructs through the same class descriptor
   slot 0, inserts into the same handle table and calls the same virtual slot 11, tearing the object
   down with 0x00456d00 if slot 11 fails. Either way the objects come from the 0x2234 record.
5. Afterwards 0x00759d60, 0x00a0a640, 0x00a0da80 run (not read; post-placement refresh of some kind).

**The record head, as 0x00456760 copies it (CONFIRMED — disassembly; this is the shape of the code's
own 40-byte in-memory record, built from a 0x1c-byte on-stream head).** With `src` the record's start
in the stream:

| stream offset | size | copied to | meaning as the code uses it |
|---|---|---|---|
| +0x00 | u32 | rec+0x00 | object handle, low dword (0 = allocate a fresh one) |
| +0x04 | u32 | rec+0x04 | object handle, high dword |
| +0x08 | u32 | rec+0x08 | parent/owner handle, low dword |
| +0x0c | u32 | rec+0x0c | parent/owner handle, high dword |
| +0x10 | u32 | rec+0x10 | class-name hash (a 32-bit integer; mixed by the integer mixer 0x00dab2b0 into one of 510 buckets and looked up in the registry's class-name table at +0xc6c, which yields the class index byte) |
| +0x14 | u16 | rec+0x14 | not used by the registry code read this pass |
| +0x16 | u16 | rec+0x18 and rec+0x1a | length of the variable tail; the loader seeks `0x1c + tail` past the head, then aligns to 4 |
| +0x18 | u16 | → rec+0x1c | **offset of the object's name inside the tail**: rec+0x1c = src+0x1c + offset when non-zero, else null (no name) |
| +0x1c | ... | rec+0x20 = src+0x1c | start of the tail (class-specific data); rec+0x24 = 0 |

**Slot 11, the per-object initialiser — base 0x00456d80, derived 0x008544b0 (dump `nor/func7`).**
0x00456d80(obj, rec, flag): if the object's +0x10/+0x14 owner handle is set, resolves the owner
through the handle table, clears the field (writes the null handle at 0x01298318) and calls the
owner's own vtable +0x2c with the child (attach); clears +0x33 bit 0x10 ("uninitialised"); then
**if rec+0x1c (the name) is non-null and 0x004584e0(name) — the spec's per-kind resolver — finds no
object already using it, calls 0x00457650(obj, name)**; finally calls the object's own vtable slot 1
(+0x04) with the record, the class-specific "read my properties from the tail" step, and returns its
result (after 0x00456b40 on success). This is the single place a placed object's authored name enters
the registry. CONFIRMED — disassembly.

---

## 2. Continuation (2026-10-02, second pass after the first was cut off): is the record format `.czn_pc`-specific or shared?

The first pass stopped having shown that a name enters the registry only through virtual slot 11,
fed by a 40-byte record built from a serialised head. What it had not settled is the crux of the
three-way question: whether the serialised stream feeding that record is **only** the `.czn_pc`
0x2234 record (zone object placement), or a generic object-serialisation format that other systems
(save-game load, a script-driven spawn, another container) also feed. This is settled below by call-
graph analysis of the record family — code only; no `.czn_pc` bytes are opened. New dumps live in
the scratchpad under `nor2/`.

### 2.1 Caller census of the whole record family (dump `nor2/calls1`, raw CALL/JMP/dword scan) — CONFIRMED

| function | role (from §1.5–§1.7) | raw hits |
|---|---|---|
| 0x00456760 | copies the 0x1c-byte on-stream head into the 40-byte record | **3**: 0x00457b06 (in 0x00457a50), 0x00457db0 (in 0x00457c60), **0x008ccefc (in 0x008ccec0 — not previously seen; outside both the zone module and the registry module)** |
| 0x00457c60 | batch loader | 1: 0x00853904 (in the wrapper 0x008538f0) |
| 0x008538f0 | batch-loader wrapper | 2: 0x00863ef8 (zone step 0x00863e90), 0x00a85b31 (in 0x00a856d0 — see §2.2) |
| 0x00457a50 | one-record-at-a-time loader | 1: 0x00853924 (in the wrapper 0x00853910) |
| 0x00853910 | its wrapper | 1: 0x00863f17 (zone step 0x00863e90) |
| 0x00456d80 | base slot 11 (the name-setting initialiser) | 1 call, 0x008544c4 — inside the derived override 0x008544b0, which chains to base; plus its vtable entry at 0x01164674 |
| 0x008544b0 | derived slot 11 | 0 direct calls; only its vtable entry at 0x011646a8. So slot 11 is reached only through the vtable, i.e. from whichever code holds a record and calls registry vtable +0x2c — the two loaders above, and any hand-built caller (bounded in §2.5) |
| 0x0045bc20 | builds the batch descriptor `{count @+0x08, data ptr @+0x18, length @+0x1c}` | **2**: 0x00863ec0 (zone step 0x00863e90), **0x00aaf4b6 (in 0x00aaf480 — not previously seen)** |
| 0x00a856d0 | second caller of the batch wrapper | 1: 0x00a8713e (in 0x00a87010) |

So the one-at-a-time path (0x00457a50) is zone-only, but the batch path (0x00457c60) has a second
feeder, and the head-copier 0x00456760 has a third consumer. Both are followed next.

### 2.2 Second feeder of the batch loader: vehicle "cover nodes" (dump `nor/func6/func_0x00a856d0`, read this pass) — CONFIRMED

0x00a856d0(vehicle) is a vehicle-instance set-up/reset routine in the vehicle module, not a Lua
wrapper: it takes a vehicle object pointer, clears and sets bits in the vehicle's +0x16c0 flag word,
reaches the vehicle's definition block through vehicle+0x1e00 (→ +0x10, or the block itself when its
byte +0x60 is 1), rebuilds per-wheel/per-part state, and at its end tests the definition's property
set for the literal strings `"camera"`, `"camFOV"`, `"camDOF"` through 0x004bc880. In the middle of it,
at 0x00a85b09–0x00a85b36: **if the definition block's +0xcc is non-null, it copies the vehicle's own
8-byte handle (+0x08/+0x0c) into a stack pair and calls 0x008538f0(def+0xcc, &handle,
`"vehicle cover nodes"`)** — the same batch loader as the zone path, with the vehicle as the OWNER of
every object in the batch (the owner-handle argument that §1.6 step 4 copies into rec+0x08/+0x0c, so
each cover node is attached to its vehicle by slot 11's owner step) and the string as the third
argument. That third argument is therefore a label (the zone caller passes zone+0x7c in the same
position), not data.

Consequence: the 40-byte record / 0x1c-byte head format is **not** private to `.czn_pc`. It is the
registry's generic "serialised object batch" format, and a vehicle definition carries a batch of it
too (its cover-node objects). What file that vehicle batch is cut from is the question for 0x00aaf480
(§2.3). Whether such objects are *named* (rec+0x18 non-zero) is a property of the data, which this
note does not read; the code path for naming them is identical.

### 2.3 Where the vehicle batch comes from, and the batch header the code expects (dump `nor2/func1`) — CONFIRMED

- 0x00aaf480 (EAX = pointer to the vehicle-definition block) relocates the definition's +0xcc from
  a file-relative offset to an absolute pointer (−1 → null, else `+= block base`), then, if non-null,
  calls 0x0045bc20(def+0xcc, 0, def+0xd0) and stores the returned descriptor back at +0xcc. The
  relocation idiom means the cover-node batch is embedded in the vehicle's loaded binary definition
  (its length at +0xd0) with the same batch layout the zone's 0x2234 payload uses. Its one caller,
  0x00ab1080, is installed as a callback (dword reference at 0x007007e3 in 0x00700780) — HIGH
  CONFIDENCE a post-load fixup of the vehicle definition; its 41 KB body was not read.
- 0x0045bc20(base, offset, length), the batch-descriptor builder, read in full: it requires the dword
  at `base+offset` to be 0x574f4246 (the bytes `F B O W` in memory order) and the dword at +0x04 to be
  exactly 5. It then patches the header in place: +0x14 := base + align4(offset + 0x20) (a table of
  8-byte entries whose count is at +0x0c — the handle list that §1.3's zone-unload walker reads),
  +0x18 := the 4-aligned address after that table (the record stream 0x00457c60 opens), +0x1c :=
  `length − consumed` (the stream length). The record count at +0x08 is used as read. This is the
  executable's own expectation of the payload header, taken from code; no zone file was opened.

### 2.4 The record is also built in memory, and it has a set-name method (dumps `nor2/func2`, `nor2/func3`) — CONFIRMED

- **0x00456330 is the 40-byte record's default constructor**: fills +0x00..+0x0c with the null
  handle (0x01298830/+4) and zeroes +0x10..+0x24. Raw scan: **12 callers** — the two stream loaders
  (0x00457a50; 0x00457c60 twice), the unreferenced wrapper 0x008ccec0 (0 raw callers: a record-from-
  stream constructor nothing uses), and **eight hand-building sites**: 0x008cc120 (a bare ctor wrapper
  with 6 callers, among them 0x00a35fe0 in the Lua module), 0x008cc970 (adds `object_position`, 12
  bytes, and `object_orientation`), 0x009310c0 (`scripted_path_positions`, n×12), 0x008ec040 and
  0x008ebf70 (`navpoint_path_type`, `navpoint_path_handles` n×8), 0x008cead0 (caller 0x00a7e690,
  vehicle module), 0x008ced60 (caller 0x0096ec80), and 0x00a2aee0 (sets the name — §2.6). Those
  property names are the same hash-keyed vocabulary `spec-zone-data-format.md` §9.3 describes; the
  descriptors are `{namePtr, id}` pairs in `.data` (e.g. 0x01308538 → `"object_position"`).
- 0x00456660(rec, &descriptor, size, kind, insertAtFront) is **add-property**: if the record has no
  tail yet it marks the tail as owned (+0x16 = 1); it looks the property up (0x004564f0) and returns
  the existing slot when size and kind match; otherwise it grows the tail (0x004565c0, pool at
  0x031d14b0) by align4(size + 8), writes an 8-byte property header `{u16 kind, u16 size, u32 id}`
  followed by the data, and bumps the count at +0x14 and the tail length at +0x18. When inserting at
  the front it memmoves the old tail up and — unless the property being added is itself the name —
  relocates rec+0x1c by the same amount, so rec+0x1c stays pointing at the name's data inside the tail.
- 0x004568c0(rec, &descriptor, data, len) adds a blob property and copies the data in.
- **0x004563f0(rec, name) is set-name**: it appends a property keyed by the descriptor at 0x01352568
  — whose string is **`ctg_object_name`** — of `strlen+1` bytes, copies the string in, and stores the
  data pointer into **rec+0x1c**, the field slot 11 (§1.7) passes to 0x00457650. So the authored
  name of a serialised object is simply its `ctg_object_name` property, and the on-stream head's
  +0x18 "name offset" (§1.7) is where that property's data sits in the tail. Ghidra lists five
  callers: 0x00a35fe0, 0x008eb600, 0x008eb580, 0x00a2aee0, 0x00458c70 (raw scan in §2.6).
- 0x00456380 → 0x00456390 is the record reset/destructor (returns an owned tail to the pool, nulls
  +0x1c/+0x20/+0x24). The Lua wrappers below call it right after creating the object.
- 0x00a20820(base) calls 0x00a206e0(static buffer 0x029f5200, base, 1) and returns the buffer; in
  0x00a2aee0 it supplies the name when none is given (`name = given ? given : 0x00a20820("group")`).
  So it is a name generator from a base string (body read in §2.6).

### 2.5 The generic create-from-record method: 0x00456e30, 173 call sites (previous pass's dumps `nor/func11`, `nor/calls3`, read this pass) — CONFIRMED

0x00456e30(AL = class index or 0xff, registry, record), `this` = record. It calls 0x00456be0 —
refuses unless registry +0x26a4 (the "open" flag) is set; if AL is 0xff it resolves the class from
rec+0x10 through the same integer mixer (0x00dab2b0, 510 buckets) and class-name table (+0xc6c,
0x004586a0) as the stream loader, reading the class index byte at registry+0x245c+bucket; checks the
class descriptor's capacity (slot 3); takes rec+0x00/+0x04 as the handle or allocates a fresh one
(0x004576e0); refuses a handle already present in the +0xc58 table (0x00458230); constructs through
class descriptor slot 0; sets obj+0x10/+0x14 = rec+0x08/+0x0c (owner) and obj+0x08/+0x0c = handle;
inserts it with 0x00458110 — and then **calls registry vtable +0x2c (slot 11) with (obj, record, 0)**,
tearing the object down with 0x00456d00 if that fails. It is exactly the per-record body of the batch
loader (§1.6 steps 4–7), exposed for records built in memory.

Raw scan: **173 call sites**, in roughly 130 functions, spread over the gameplay module (0x005c–
0x0082), the AI/world modules (0x0089–0x009e), the vehicle module (0x00a7e690, 0x00a8e980,
0x00ac44d0, 0x00b0–0x00bb) and **the Lua binding module**: 0x00a271b0, 0x00a2cc40, 0x00a2e0b0 (×2),
0x00a2e400 (×3), 0x00a31190, 0x00a354e0 (×3), 0x00a3b170 (×3), 0x00a46130, 0x00a5fd60 (×2), plus
four sites Ghidra never defined as functions (0x00a2c347, 0x00a2c71c, 0x00a30977, 0x00a30f1c). The
previous pass had dumped eight of those Lua wrappers (`nor/func12`, `nor/func13`) without writing
them up; §2.6 uses them.

This settles the shape question: **the 40-byte record / property-tail format is the registry's
generic object-construction interface, not a `.czn_pc`-private one.** `.czn_pc`'s 0x2234 record is
one producer of such records (the batch loader); the vehicle definition is a second; and engine code
— including Lua-bound functions — builds the same records in memory and feeds them to the same
slot 11 through 0x00456e30. Whether a given producer *names* its object is decided by whether its
record carries a `ctg_object_name` property (stream) or had 0x004563f0 called on it (in memory).

Registry base-vtable census, now complete (`nor/func9`, `nor2/func3`): slot 0 0x00457820 destroy
by handle; 1 0x00457730 destroy; 2 0x00457370 init(allocator, ?, classCount, nameCapacity) — called
once, from 0x008536b0 ← 0x0091f4f0 ← 0x005d25f0 (game start-up), with classCount 0x3e = 62 and
**name-map capacity 0xbb7 = 2999**, which is what fills +0x265c and the bucket count at +0x2680 —
so the name map is enabled once at start-up, globally, not per zone; 3 0x00457880 copies a 0x40-byte
argument into +0x09 and sets +0x26a4 = 1 (opens the registry); 4 0x00458c90 returns 1; 5 0x004578b0
flush (0x00457210); 6 0x004578c0 destroy-all then close (+0x26a4 = 0); 7 0x004bf550 no-op; 8
0x00457900 writes `"unknown"` into an 8-byte out buffer; 9 0x00457340 (0x004572d0, then optionally
0x005d7a10); 10 0x00456e80 unlinks an object from the per-class index lists and tears it down; 11
0x00456d80 initialise-from-record (names the object).

### 2.6 Who names an in-memory record: the five callers of set-name (dumps `nor2/calls4`, `nor2/func4`) — CONFIRMED

Raw scan of 0x004563f0: **exactly five call sites.**

1. 0x00458c70(obj = `this`, record): `if (obj+0x18) set-name(record, obj+0x18)` — the reverse
   direction: an object writing its own registered name (its +0x18, §1.5) into a record being built
   *from* it. It sits in a class vtable (dword at 0x0111c5ec) and is called from 0x008cc8d0 and from
   three sites Ghidra never defined as functions (0x008ebf19, 0x00931089, 0x00a2e9e5). This is the
   object→record (write) side of the same format; see §2.7 and "what to dump next".
2. 0x008eb580(record, name, navpointType, position, orientation): adds `object_position` and
   `object_orientation` (0x008cc970), then **set-name with the caller-supplied name**, then a
   `navpoint_type` property (0x008ebd90). Five callers, all in engine modules (0x006c9710,
   0x007032c0, 0x00708330, 0x007df8a0, 0x0089b350), none in the Lua binding range.
3. 0x008eb600(record, navpointType, position, orientation): the same, but the name is
   **generated** — 0x00a20820("navpoint") → set-name. Two callers, 0x00695ee0 and 0x006abba0.
4. 0x00a2aee0(record, nameOrNull): a record for a *group* object; name = the argument, else
   0x00a20820("group"). Three callers, each passing a literal and then creating class 0x25 through
   0x00456e30 on the registry and destroying the record: 0x00726980 passes
   `"-- Cutscene Script Group --"`, 0x007e6ea0 passes `"homies"`, 0x00a010b0 passes
   `"shopkeepers"` (the shop set-up routine that also looks up `"Cash Register"`). So these three
   names are registered by engine code from literal strings — a producer that is neither zone data
   nor script.
5. **0x00a35fe0(record, nameOrNull)**: bare record (0x008cc120), then set-name if a name is given.
   **Six callers, all in the Lua binding module**: 0x00a2c100, 0x00a2c4a0, 0x00a2c8e0, 0x00a30bf0,
   0x00a30c70, 0x00a31070 — and 0x00a2c4a0, 0x00a30c70, 0x00a31070 also call the name generator
   0x00a20820. These are the candidates for "a script names an object at runtime"; §2.7 reads them.

The name generator 0x00a206e0(out, base, numbered), read in full: it formats `"%s%s%s_%s%03d"` from
(when `numbered`) the local player's index rendered as digits and a global counter at 0x026e9518
that wraps at 10,000 — otherwise two empty strings — then an empty string (0x0129a0e3), the base
string copied to at most 31 characters and passed through 0x00da77e0 (case-folding, HIGH
CONFIDENCE from the string-helper family), and an index 0..999; it bumps the index while the result
already resolves in the name map (0x004588f0 on 0x02444db0, count at 0x02444dac) to a live object
of a resolvable class (class row +6 bit 1, liveness 0x00853b10). So generated names are unique among
live objects, and the only bases seen are `"group"` and `"navpoint"`. The `ctg_object_name` string
itself occurs once (0x0129ea5c) and has two uses: the descriptor 0x01352568 and a static
initialiser at 0x00fea3ac that registers the descriptor with 0x00d9e740 at start-up.

Negative check on the previous pass's eight Lua wrappers that create objects through 0x00456e30
(`nor/func12`, `nor/func13`): none of them calls set-name, the generator, or writes rec+0x1c (grep
over the eight dumps: only a stack word at 0x00a3b215) — they build records from `object_position`
/ `object_orientation` and class-specific properties and create **unnamed** objects. So most
script-driven creation is anonymous; naming at creation is confined to the 0x00a35fe0 family.

### 2.7 The Lua side, settled (dumps `nor2/func5`, `nor2/calls5`–`calls7`, `nor2/range1`, previous pass's `nor/func12`–`func13`) — CONFIRMED unless marked

**(a) The six Lua-module callers of 0x00a35fe0 are not Lua wrappers; five hang off one stream
deserialiser.** Raw scans: 0x00a2c4a0, 0x00a30c70 and 0x00a31070 have **no callers at all** in this
build (they generate a name from the bases `"interior"` / `""` and add `script_interior_peer`,
`script_mover_peer`, `script_mover_peer_handle` properties — dead code); 0x00a2c8e0 ← 0x00a2cc40,
0x00a30bf0 ← 0x00a31190, and 0x00a2c100 (a thin forwarder) ← twelve functions, three of which
(0x00a2e400, 0x00a354e0, 0x00a3b170) share one caller with 0x00a2cc40 and 0x00a31190:
**0x00a36a80(stream, outHandle)**. Read in full, it is a *deserialiser*: it reads a flag byte
(0x004d4750); if set, reads an 8-byte handle (0x0086ecb0), copies it to the out-pair and resolves it
in the handle table 0x024433a8 (0x00458230), returning the object only if initialised (+0x33 bit 0x10
clear), of a class whose row has +0xa bit 0x8, and alive (0x00853b10) — "reference an existing
object by handle". Otherwise it reads a kind code (0x00a29be0), takes a NUL-terminated string
**in place from the stream buffer** (base at stream+0x00, cursor at stream+0x14, advanced by
`strlen+1` through 0x00881000) — the name — and jumps through a six-entry table at 0x00a36bb4 on
`kind − 0x26`: 0x26 → 0x00a2e400(stream, name); 0x27 → 0x00a354e0; 0x28 → 0x00a3b170; 0x29 →
0x00a2cc40 (an "interior" object, class 0x26, with `script_interior_handle` /
`_notoriety_level` / `_notoriety_type` properties); 0x2a → 0x00a31190 (a "script mover", class
0x28, `script_mover_peer_handle`); 0x2b → nothing. Each builds a record carrying that name (through
0x00a2c8e0, 0x00a30bf0, or 0x00a2c100 → 0x00a35fe0 → set-name), creates the object with 0x00456e30
and destroys the record. 0x00a36a80's single caller 0x008a72a0(stream, target) is a virtual method
(vtable entry at 0x01167830) that forwards the created-or-resolved object's handle to
0x008adcc0(target, lo, hi) when 0x008add90(obj) is false, else — if the stream carried a handle —
to 0x00887400(target, lo, hi) after 0x00bc5610(). So this path **registers a name that arrives in a
serialised stream**, i.e. one that already existed on the writing side; it does not originate names.
The "peer" property vocabulary of its dead siblings makes co-op replication the plausible transport
— HYPOTHESIS; the writer side was not traced (§2.9).

**(b) Which Lua-registered functions reach the record family, and whether they name anything.**
Of the previous pass's eight Lua-module creators only two are registered Lua functions — both found
as dword operands inside the registration routine 0x00a20840 and read back with range mode:
0x00a46130 is **`commando_spawn`** (pair stored at 0x00a21a86/0x00a21a91, beside `commando_despawn`
0x00a42af0) and 0x00a5fd60 is **`spawn_wieldable_prop`** (0x00a24fb2/0x00a24fbd). 0x00a2e0b0 is a
vtable entry (0x0117ecb0); 0x00a271b0 is `character_add_prop`'s delegate (spec §17.13, which already
notes the 0x00456e30 call with type 0x1e); the remaining five are the deserialiser's targets above.

- `commando_spawn` → 0x00a33b80(record, def, positionOrNull): name = 0x00a20820(base) where base
  is the string at `def → +0x08 → +0x08` (a name field of the definition the Lua arguments select;
  which table that is was not read), set-name via 0x00a2c100, a 4-byte property at descriptor
  0x0130c6dc holding the def pointer, optionally the position (0x008cc650); then 0x00456e30. So
  `commando_spawn` registers a **generated, uniquified** name `<def-name>_NNN` (§2.6's generator):
  the script cannot choose it, but the object is thereafter resolvable by it. CONFIRMED.
- `spawn_wieldable_prop`, `character_add_prop` and `human_skydive_create_npc_backpack` (0x009ae340,
  spec §20.20) create through 0x00456e30 with **no** set-name → unnamed objects. CONFIRMED.
- Exhaustive negative: the five set-name callers (§2.6) and the twelve callers of 0x00a2c100 contain
  **no Lua-registered function and no function that takes a Lua string as the name**. Every name
  that reaches set-name is a literal (§2.6 item 4), generated (0x00a20820), deserialised from a
  stream (0x00a36a80), or passed in by an engine-module caller: 0x00a2d880 ← 0x00733bf0 / 0x00734050;
  0x00a2d900 ← 0x0064d780 / 0x006ea880; 0x00a33b00 ← 0x007357c0 / 0x00735940; 0x00a34820,
  0x00a393e0, 0x00a39480, 0x00a39f20 ← callers in 0x0063–0x007e and 0x00b2c240. None of those engine
  callers is cited anywhere in the spec set (grep over `spec-*.md`: no matches), and the 0x0073xxxx
  group's own callers (0x00733d30, 0x00733e40, 0x007341b0, 0x00735bf0) stay inside the same module
  that hosts the mission-kind resolvers 0x00734e90 / 0x00739690. Where *those* name strings come from
  is the one thing left untraced (§2.9 item 1).

### 2.8 Direct answer

**Is the record stream `.czn_pc`-specific, or a generic mechanism?** Generic. The 40-byte record
with its `{u16 kind, u16 size, u32 id}` property tail is the object registry's universal
construction interface. It has four producers in this build: (1) the `.czn_pc` top-level record
0x2234 ("Game objects, as placed in the WE"), batch-loaded at zone load (§1.7); (2) a vehicle
definition's embedded cover-node batch, same `FBOW`/version-5 batch layout (§2.2–§2.3); (3) in-memory
records built by engine code and fed to 0x00456e30 at 173 call sites (§2.5); (4) in-memory records
built from a serialised stream by the 0x00a36a80 deserialiser (§2.7a). The name map itself is one
global, case-insensitive table of 2,999 slots, enabled once at game start (§2.5), so a name
registered by any producer is resolvable by every per-kind resolver.

**Where does a mission script's `'Killbane'` get registered?** Of the three candidates:

- (a) *Mission package data* — no. The packages carry no name table (§0), and no code path reads
  names out of a mission package; the only file-borne producer of named objects is zone object
  placement.
- (b) *A separate table* — yes, in this form: the authored name is the **`ctg_object_name`
  property of a placed object's record in the zone's 0x2234 record**, pointed at by the 0x1c-byte
  head's +0x18 offset (§1.7, §2.4), registered into the global map by slot 11 when the zone loads.
  That record lives inside the `.czn_pc` object-placement interior, which is parked — **this is
  where the trail stops**; no `.czn_pc` bytes were opened or requested in either pass. (HIGH
  CONFIDENCE, not CONFIRMED, that the on-stream tail is the same `{kind,size,id}` property list
  the in-memory builders write: 0x00456660 reads and relocates the very tail the stream head
  describes, but no class-specific slot-1 property reader was dumped.)
- (c) *Script-spawned* — only in a weak sense. No Lua-bound function can assign a chosen name:
  Lua-reachable creation is either unnamed (`spawn_wieldable_prop`, `character_add_prop`,
  `human_skydive_create_npc_backpack`) or engine-named with a generated unique suffix
  (`commando_spawn`). Engine code also registers literal names (`"homies"`, `"shopkeepers"`,
  `"-- Cutscene Script Group --"`, generated `"navpoint"`/`"group"` names) and replays names it
  receives in a serialised stream (§2.7a). A mission character addressed by a fixed authored name
  such as `'Killbane'` therefore has to be a **placed object whose zone record carries that name**
  — for `dlc1_mm_06`, the mission's own zone(s) — not something the script creates.

So: **mix of (b) and (c), with (b) carrying the authored names.** Mechanism generic; data for
authored names `.czn_pc`-resident. For Team B this means a reader that wants to resolve mission
names must parse the 0x2234 record's per-object heads and their `ctg_object_name` property, which
requires the parked interior to be opened — a decision for the user, not for this note.

### 2.9 What to dump next (all code-only; none requires opening `.czn_pc`)

1. **The 0x0073xxxx named creators** — 0x00733930, 0x00733bf0, 0x00734050, 0x007357c0, 0x00735940
   and their callers 0x00733d30, 0x00733e40, 0x007341b0, 0x00735bf0: where the name strings they pass
   to 0x00a2d880 / 0x00a33b00 / 0x00a39480 come from (literal, `.xtbl` row, or mission data). This
   is the last route by which a mission-specific authored name could enter the map at runtime
   without being zone data; the module also hosts the mission-kind resolvers.
2. **The writer side of the 0x00a36a80 stream** — the owner of vtable 0x01167830 (0x008a72a0's
   class) and the serialise-name method 0x00458c70's call sites (0x008cc8d0 in vtable 0x0116ac34;
   undefined-code sites 0x008ebf19, 0x00931089, 0x00a2e9e5): confirm whether this is co-op
   replication or save-game persistence. If save-game, named script objects can survive a reload.
3. **A class slot-1 property reader** (e.g. the character class's, via 0x02cc9900 row → vtable +0x04)
   to confirm the on-stream tail is the `{u16 kind, u16 size, u32 id}` list and to enumerate the
   properties a placed character carries — completing the 0x2234 per-object description from code.
4. **`commando_spawn`'s definition table** (what `def→+0x08→+0x08` names) for the generated-name base.
5. **0x00ab1080** — which vehicle file carries the cover-node batch (an extension string should sit
   in its 41 KB body), and whether cover-node records carry `ctg_object_name` (a vehicle file is not
   parked, so its batch could be read if wanted).
6. **The four undefined-code 0x00456e30 sites in the Lua module** (0x00a2c347, 0x00a2c71c,
   0x00a30977, 0x00a30f1c): define them as functions and check for a Lua-string → set-name path. The
   raw scan already shows no CALL to 0x004563f0 from any of them, so this is belt-and-braces.
7. **The eight `.data` property descriptors** seen this pass (0x01308538 `object_position`,
   0x01308540 `object_orientation`, 0x01352568 `ctg_object_name`, 0x0130c428/0x30/0x40/0x48
   `script_interior_*`, 0x0130c6a8/0xc0 `script_mover_*`, 0x01307fd4 `navpoint_type`, 0x01308050/58
   `navpoint_path_*`, 0x013092e4 `scripted_path_positions`, 0x0130c538, 0x0130c6dc): read their
   `{namePtr, id}` pairs with ptrs mode to tie each hash-keyed id to its name — a code-side key for
   whatever property ids a future (authorised) 0x2234 reader encounters.

Dumps for this continuation are in the session scratchpad under `nor2/` (`calls1`–`calls7`,
`func1`–`func6`, `str1`, `range1`); the first pass's are under `nor/`.
