# Scoping note — `.czn_pc` interior: object placement/property stream, record interiors, navmesh

**Date:** 2026-10-07 · **Type:** scoping note only — no spec content, nothing here is cleared for
implementation. This is the first dispatch into the interior since the user's hold lifted
(`HANDOFF.md` §27.3/§27.H-5); it builds directly on `spec-zone-data-format.md` §7–§10 and does
not repeat or re-open anything that document already closed (§7's Mesh-mechanism geometry
finding, §9.1/§9.2's registration-table dead end, §9.5's CONFIRMED top-level-walker correction).

**User's explicit instruction on scope (quoted verbatim):**
> "I authorize full work on the .czn_pc interior: the object placement and property stream,
> record interiors and navmesh. Reverse-engineer it like everything else and sync specs to Team B
> as usual."

**Question.** (1) Where does the object-placement/property stream actually live, and what is the
real consuming code (not a generic data-driven reader — `spec-zone-data-format.md` §9.1/§9.2
already closed that route by construction)? (2) Once an object's own bytes are located, what is
the per-record field layout? (3) Does `.czn_pc` embed navmesh data directly, or reference it from
elsewhere, and does this project have any navmesh-adjacent prior work to build from?

**Method.** Read-only `pefile` + `capstone` static reads against the shipped
`SaintsRowTheThird.exe` only, via `tools/harnesses/exe_va.py`'s loader extended with a small
scratch script (function-size-by-heuristic, `.text`-wide `E8`-relative-call xref scan, and a raw
little-endian 4-byte needle search for data-table addresses — same idea as the project's existing
immediate-value searches, just targeted at this investigation's own addresses). **No Ghidra was
needed or used this pass.** No decompiled pseudocode is reproduced below; findings are reported as
addresses, instruction mnemonics/operands, and shipped string data, in the same style the rest of
this project already uses. **No leaked Volition/engine source of any kind was read, consulted, or
used.** `.czn_pc`/`.czh_pc` file *content* was not opened this pass either — everything below comes
from the executable's own code and static data, which is sufficient to scope the three areas and
identify root functions; reading real shipped records is explicitly left to the units this note
proposes, where it belongs (population counts, controls, discriminators — not a single scoping
pass).

**Confidence key** (this project's usual one): **CONFIRMED — disassembly** (control flow/operands
read directly this pass), **CONFIRMED — empirical** (shipped data read directly), **HIGH
CONFIDENCE — inferred**, **HYPOTHESIS — unconfirmed**, **OPEN / UNKNOWN**. Everything below is a
single rapid scoping pass, not the project's full population-validated rigor (`spec-zone-data-format.md`
§10's "five discriminators, each with a control that fires" standard) — that validation is
explicitly the next units' job, flagged throughout rather than skipped.

---

## 0. Verdict up front

1. **The real top-level reader is live, shipped-build runtime code, not a dead/authoring-only
   path — traced end to end from an already-documented entry point.** `spec-world-streaming.md`
   §10.2 already names `FUN_00866580` as the "zone-instantiate on load" callback the container
   registry fires when a `Zone` (kind `0x1D`) container finishes loading. This pass read
   `FUN_00866580`'s own body far enough to confirm it calls a new function, **`FUN_008660e0`**
   (~1.7 KB, only partly read this pass), which in turn unconditionally calls **two** sibling
   section-dispatchers in sequence: the already-known 9-handler one from
   `spec-zone-data-format.md` §9.5 (**`FUN_008652d0`**) and a second, larger one not previously
   found (**`FUN_00865f90`**, ~325 bytes, 13 more call sites). **[CONFIRMED — disassembly, the
   whole chain `FUN_00866580` → `FUN_008660e0` → {`FUN_008652d0`, `FUN_00865f90`} read directly,
   call targets verified by address.]**
2. **The engine's own zone-content-kind table was found, with real string names and
   descriptions for 22 of the ~790 ids `spec-zone-data-format.md` §1 counted** — a 22-entry array
   at `0x013026bc` (12 bytes/entry: two string pointers + a `u32` id, ids `0x2233`…`0x2254`,
   count `22` stored immediately after the array at `0x013027c8`). Reading the strings directly
   gives the engine's own names for each kind (§2). **This resolves part of
   `spec-zone-data-format.md` §9.5's open half directly: id `0x2234` is named `"Objects"`,
   described `"Game objects, as placed in the WE [World Editor]"` — exactly the object-placement
   section the task asked about — and id `0x2235` is named `"Navmesh"`, described `"The navmesh
   data for the zone"`.** **[CONFIRMED — disassembly + empirical: 22/22 entries read, trailing
   count field matches exactly.]**
3. **Navmesh is embedded directly in `.czn_pc`, not referenced from elsewhere — answered at the
   "where" level.** Section id `0x2235` has its own handler, **`FUN_008642a0`**, called from
   `FUN_008652d0` (the already-known dispatcher). This is a **different system** from the
   already-partly-specced runtime "navmesh cell" API (`FUN_00446c60`/`FUN_00445980`,
   `spec-ai-runtime.md` §8/§9; the human navigator `FUN_00993e90`→`FUN_00993630`,
   `spec-player-runtime.md` §10.6/OPEN 76) — whether the file-embedded section feeds that runtime
   system or is a separate/legacy representation is the key open discriminator for unit U3 (§6).
4. **The Objects section's handler, and its two real per-record-array readers, are now named and
   sized — the strongest concrete lead for both "object placement/property stream" and "record
   interiors."** `FUN_00863e90` (the `0x2234` handler) branches on a per-section field against the
   value `0x1c` (28) and a flags bit to call one of two thin forwarders (`FUN_008538f0`/
   `FUN_00853910`, ~26 bytes each) onto two real methods of a **global singleton object at
   `0x2442750`**: `FUN_00457c60` (~1.0 KB) and `FUN_00457a50` (~0.5 KB). Reading `FUN_00457c60`
   directly shows a genuine per-object deserialization loop: seek calls through the **same generic
   serialization stream primitives already named in `spec-zone-data-format.md` §9.3**
   (`FUN_00da7f30` appears directly), a name-hash → byte-ordinal lookup (same *shape* as §9.3's
   `FUN_00a33730`, a different table at `0x2cc9900`), and a vtable-dispatched create+deserialize
   call per object, appending to an output array and advancing a sequential index. **This directly
   advances `spec-zone-data-format.md` §9.5's own "concrete next step"** ("find who holds the
   table that resolves `FUN_00a33e00`'s indirect call sites") — `0x2cc9900` is ~~very plausibly that
   table, or its sibling for a different class family~~ **CORRECTED, see §4 below: a 3,757-site
   whole-binary xref census found this is the engine's one universal object-class table, used
   everywhere, not a zone/placeable-type-specific sibling family.** It was reached this time with the stream cursor
   **confirmed** inside a live `.czn_pc`-sourced buffer. **[CONFIRMED — disassembly for the control
   flow actually read; semantic field interpretation below is HIGH/HYPOTHESIS, not yet
   population-tested.]**
5. **A concrete record-size fact fell out of the read, not asserted a priori:** the per-object
   working struct `FUN_00457c60` builds is **40 bytes**, and the on-disk record's seek length is
   ~~computed as `(u16 field at header+0x1a) + 0x1c`~~ **CORRECTED (U0+U1 pass,
   `scratchpad/czn_u0u1_findings.md` §4): computed as `(u16 field at header+0x16) + 0x1c`.** The
   `+0x1a` offset is real, but it names where `0x00456760` *copies* that same word to inside the
   in-memory record (duplicated at both `+0x18` and `+0x1a` of the copy) — not the on-disk source
   offset, which is `+0x16`. The rest of the finding stands: **a fixed 28-byte base plus a variable
   tail whose length is itself stored in the header**. This is the first concrete structural fact
   about a `.czn_pc` object record's own shape. **[CONFIRMED — disassembly for the arithmetic,
   now against the correct offset; "28-byte base + variable tail" is itself promoted to CONFIRMED
   by the U0+U1 pass's own read of `0x00456760`'s full copy — see the findings doc §4.]**
6. **A plausible, testable closure for the `triggers.xtbl` loose thread
   (`spec-tables-vehicle-world.md` §21.1/§14) was found, not yet confirmed.** Section id `0x2243`
   ("Interior triggers", described "Interior trigger volumes") has its own handler,
   `FUN_008634e0`, reached from a single call site, `FUN_0085f26e` — **not** from the same
   dispatcher chain as §0.1, a separately-reached path, OPEN which. It reads a `u16` count, then
   loops calling `FUN_00dcd3d0` once per entry into a 144-byte-stride (`0x90`) output array. If
   `FUN_00dcd3d0` is shown to insert into the same hash table `triggers.xtbl`'s loader
   (`FUN_0093e0c0`, `spec-tables-vehicle-world.md` §14) later patches default properties onto, this
   closes §21.1's "noted, not acted on" structural link outright. **Not confirmed this pass** —
   `FUN_00dcd3d0` has 6 call sites total, only one of them this one, so it is a shared/generic
   primitive, not trigger-specific by itself; the test is whether its own writes land in the
   `FUN_00DAB330`-hashed (mod-20) bucket array, which was not traced this pass.
7. **19 of the 22 named section kinds are now pinned to a real handler address**; the 3 that
   are not are genuinely informative, not just gaps: `0x2239` ("Trailer") has **zero** references
   anywhere in `.text` — ~~a defined-but-dead section kind, confirmed negative, not an oversight.~~
   **Corrected 2026-10-07 (unit U2, real-data pass, `scratchpad/czn_u2_record_interiors_2026-10-07.md`
   §9): "dead" overstates it — `0x2239` is a universal, always-zero-length, always-last-in-chain
   END-OF-CHAIN MARKER, confirmed in 2,971/2,971 real shipped `.czn_pc` files (every file carries
   exactly one, it is always the final top-level record, and its length field is always 0). It has
   no handler because there is nothing to read, not because it is unused — it is structurally
   load-bearing as the chain's own terminator, the real-data counterpart to this project's
   already-documented `0xFFFB`-family flat-walk stop condition (`spec-zone-data-format.md` §2/§6
   item 3).**
   `0x2233` ("Crunched ref geom") and `0x2243` ("Interior triggers") each have exactly one
   reference, both resolved to real handlers this pass (`FUN_00865ba0` and `FUN_008634e0`
   respectively) — so in fact **all 19 non-dead kinds now have a named handler**; what remains open
   is depth (full field layout), population validation, and the two reachability questions in
   points 3 and 6 above. See §2 for the full table.

---

## 1. What this builds on, and does not repeat

`spec-zone-data-format.md` §7 already closed zone **geometry** (the shared Mesh sub-block,
population-confirmed) — not touched here. §9.1/§9.2 already closed the generic
registration-table/vtable-constructor route **by construction** (`.czn_pc`/`.gzn_pc` have a
literal NULL constructor) — not re-attempted. §9.5 already found the real top-level walker
(`FUN_00864c60`/`FUN_007512f0` via the dispatcher this note calls `FUN_008652d0`) and flagged "only
2 of ~9 tag-handlers opened so far ... genuinely possible, not yet shown, that one of [the other
6] is the interior object/property stream's own real reader" as the open half — **this note
answers exactly that question** (§2–§4 below): none of `FUN_008652d0`'s own 9 handlers is the
Objects section; it lives in a sibling dispatcher this note found, `FUN_00865f90`, called from the
same parent function.

`spec-tables-vehicle-world.md` §21.1 ("the `.czn_pc` connection — noted, not acted on") flagged that
`triggers.xtbl` patches a **pre-existing** runtime trigger-instance hash table rather than
constructing instances itself, and speculated that construction happens elsewhere — plausibly in
the parked `.czn_pc` interior. §6 above and §5 below give this a concrete address to chase.

`spec-ai-runtime.md` §8/§9 and `spec-player-runtime.md` §10.6/OPEN 76 already document a runtime
"navmesh cell" API (`FUN_00446c60` cell-identity, `FUN_00445980` cell-connectivity,
`FUN_00a12180`×2 then `FUN_00a11e10` "does a navmesh path exist between two cells", the human
navigator `FUN_00993e90`→path-planner `FUN_00993630`) and confirm (via `spec-physics-format.md`
§?, the middleware census) that **this is not Havok AI** — zero occurrences of `hkai`/`hkAi`/
`hkaiNavMesh` anywhere in the binary, and zero occurrences of Recast/Detour/Kynapse either. So
whatever this engine's navmesh is, it is proprietary. §6 below is the first point of contact
between that already-specced runtime API and the newly-found file-embedded `0x2235` section.

---

## 2. The zone-content-kind table (new this pass) — 22 named kinds, ids `0x2233`–`0x2254`

**Table location:** `0x013026bc`, stride 12 bytes (`{char* name_of_this_entry is actually one
slot ahead — see layout note, char* description_of_previous_entry, u32 id}`), 22 entries, count
field `22` at `0x013027c8` immediately following the array. Read directly with `cstr()` against
each pointer; every string below is shipped executable data (`.rdata`), quoted the same way this
project already quotes the Mesh/Morph block self-identification strings and the `SR3Z` error
text.

**Layout note, so a re-reader does not have to re-derive the indexing:** entry *i*'s own **name**
string is its own second pointer field; its own **description** string sits one entry further
along, as that next entry's *first* pointer field. (Equivalently: reading `(name[i], description
that-immediately-precedes-it)` pairs this way reproduces exactly the known facts —
`0x013026f4` holding `0x2237` named `"WE geom"` matches `spec-zone-data-format.md` §9.5's own
citation of that address/value exactly, and `0x0130279c` holding `0x2251` named `"Rain killer
meshes"` matches the community-doc cross-check's already-accepted finding, `HANDOFF.md` §27.H-2
item A8 — both independent confirmations that this reading of the table is right, not an
artefact.)

| id | name | description | handler (this pass) | handled by |
|---|---|---|---|---|
| `0x2233` | **Crunched ref geom** | *(not read this pass — the entry one position earlier was not dumped; see table note)* | `FUN_00865ba0` | `FUN_00865f90` |
| `0x2234` | **Objects** | "Game objects, as placed in the WE [World Editor]" | `FUN_00863e90` | `FUN_00865f90` |
| `0x2235` | **Navmesh** | "The navmesh data for the zone" | `FUN_008642a0` | `FUN_008652d0` |
| `0x2236` | **Traffic** | "Traffic data for the zone" | `FUN_00863c80` (shared w/ 2238, 2241) | `FUN_00865f90` |
| `0x2237` | **WE geom** | "Editor created geometry (patches, roads, path deform meshes)" | `FUN_00864c60` *(already known)* | `FUN_008652d0` |
| `0x2238` | **Sidewalks** | "Sidewalk data for the zone" | `FUN_00863c80` (shared) | `FUN_00865f90` |
| `0x2239` | **Trailer** | "???" (the engine's own literal placeholder string) | **none — zero references anywhere in `.text`** | — (dead) |
| `0x2240` | **Light clip meshes** | "Light clip mesh data" | `FUN_00863f40` | `FUN_008652d0` |
| `0x2241` | **Traffic signals** | "Traffic signal data" | `FUN_00863c80` (shared) | `FUN_00865f90` |
| `0x2242` | **Mover constraints** | "Constraints for movers in the zone" | `FUN_00863df0` | `FUN_00865f90` |
| `0x2243` | **Interior triggers** | "Interior trigger volumes" | `FUN_008634e0` | **separate path, `FUN_0085f26e`** |
| `0x2244` | **Heightmap** | "Heightmap data for the zone (stitched into the larger world)" | `FUN_00864200` | `FUN_008652d0` |
| `0x2245` | **c_object rbb tree** | "rbb tree for collecting c_objects at runtime" | `FUN_00864330` | both dispatchers |
| `0x2246` | **Undergrowth** | "Undergrowth data" | `FUN_00864150` | `FUN_008652d0` |
| `0x2247` | **Water volumes** | "Water volume data" | `FUN_008644b0` | `FUN_00865f90` |
| `0x2248` | **Wave killers** | "Wave killer data" | `FUN_008646f0` | `FUN_008652d0` |
| `0x2249` | **Water surfaces** | "Water surface data" | `FUN_00864770` | `FUN_00865f90` |
| `0x2250` | **Parking** | "Parking data" | `FUN_00864840` | `FUN_00865f90` |
| `0x2251` | **Rain killer meshes** | "Rain killer mesh data" | `FUN_00864000`→`FUN_007512f0` *(already known)* | `FUN_008652d0` |
| `0x2252` | **Supplemental LOD** | "Autogenerated 4th LOD data" | `FUN_008640a0` | `FUN_00865f90` |
| `0x2253` | **Fading grid data** | "City fading grid data. Also used for mip streamer calculations" | `FUN_008648c0` | both dispatchers |
| `0x2254` | **Audio Emitter rbb tree** | "rbb tree for collecting audio emitters at runtime" | `FUN_008643f0` | `FUN_00865f90` |

**[CONFIRMED — disassembly for every "handler" column entry: each address was independently
verified to load the matching id's table slot via an absolute reference, except `0x2245`/`0x2253`
whose handler bodies are shared/called from both dispatchers (self-consistent — a section can
legitimately appear in more than one content group). The two "shared `0x2236`/`2238`/`2241`
function" rows reflect one function (`FUN_00863c80`, ~356 bytes) that checks all three ids in
sequence internally, not three separate functions — read directly, not inferred.]**

**Review status for this table:** read this pass only, not cross-checked by a second reader or a
population run. The "name vs. description" pairing (layout note above) is inferred from content
sense plus two independent prior-spec matches (`0x2237`, `0x2251`) — HIGH CONFIDENCE, not itself
re-derived by a second method yet. Recommended first action of unit U0 (§8): re-verify this table
against the two already-independently-known entries one more time and spot-check two or three more
(e.g. confirm `0x2235`'s own description string really does read as given, which this note already
did, but a second pass should re-confirm independently before it is cited elsewhere as settled).

---

## 3. The confirmed runtime call chain

```
FUN_00866580  "zone-instantiate on load" — already named in spec-world-streaming.md §10.2,
              the container-kind-0x1D (Zone) registry callback; classifies a loaded container's
              entries into Zone fast/slow, Zone header fast, Buffer (jump table at 0x8666bc)
   -> FUN_008660e0  (~1.7 KB; only ~250 bytes read this pass — dispatches on which of the
                     classified buffers are present)
        -> FUN_008652d0  (9 calls, the dispatcher spec-zone-data-format.md §9.5 already found:
                           0x2237, 0x2240, 0x2251, 0x2246, 0x2235, 0x2244, 0x2245, 0x2248, 0x2253)
        -> FUN_00865f90  (NEW this pass; 11 calls: 0x2242, 0x2233, {0x2236,0x2238,0x2241},
                           0x2234, 0x2245, 0x2247, 0x2249, 0x2250, 0x2252, 0x2253, 0x2254)
```

~~Both dispatcher calls are unconditional in sequence inside `FUN_008660e0`~~ **CORRECTED (U0+U1
pass, `scratchpad/czn_u0u1_findings.md` §1): each call is individually gated on its own
buffer-presence test earlier in `0x008660e0` (`test edi,edi; je <skip>` on the resolved "Zone
fast"/"Zone header fast" buffer pointer), not literally unconditional — the two calls
(`0x0086630d`→`0x8652d0`, `0x008663b3`→`0x865f90`, both re-read in full this pass) only both fire
if both corresponding buffers are present, which is true for every normal shipped zone but is not
the same claim as "always runs."** i.e. **a real `.czn_pc` load
walks essentially all 19 live section kinds in one pass**, not a lazy/on-demand read of only the
kinds a given zone happens to need. `0x2243` (Interior triggers) is the one live kind **not** in
this chain — its single call site, `FUN_0085f26e`, was not itself traced to a parent this pass;
finding that parent is a concrete, bounded first step for whichever unit covers it (§6, §8).

**What was not done this pass, flagged rather than assumed:** `FUN_008660e0`'s full ~1.7 KB body
was not read end to end (only the ~250 bytes around the two dispatcher calls); it may do more
(e.g. handle the "slow" zone-header variant differently, or contain the `0x2243` reachability
answer itself). This is the natural first few hundred instructions of unit U0.

---

## 4. Object placement and property stream (user's area 1)

**Root: `FUN_00863e90`** (the `0x2234` "Objects" handler, ~165 bytes/60 instructions). After
locating the section via the shared tag-find helper `FUN_00863430` (the same helper the Interior
Triggers handler also calls — likely this engine's one generic "find a tagged record within a
record chain" primitive, a good unification target for unit U0), it:

1. Reads a per-section field and compares it against `0x1c` (28) — **a format-version-style
   discriminator**, not yet proven which concrete meaning (schema version? reserved
   header-size field? something else) — HYPOTHESIS.
2. Below the threshold, calls `FUN_008538f0` → forwards (fixed `this = 0x2442750`) to
   `FUN_00457c60`.
3. At/above the threshold, additionally tests a flags bit (same bit-0-style test this project
   has already established for g-backed/inline Mesh blocks, `spec-zone-data-format.md` §10.1 —
   HIGH CONFIDENCE that this is the same "inline vs. externally-backed" idiom reused here, not
   confirmed for this specific field) and calls `FUN_00853910` → forwards to `FUN_00457a50`.

**`0x2442750` is a global singleton object** (static image bytes zero — a classic
constructed-at-startup C++ singleton, not file data). Both `FUN_00457c60` (~1.0 KB/321
instructions) and `FUN_00457a50` (~0.5 KB/166 instructions) begin by testing the same flag byte,
`[this + 0x26a4]`, before doing anything — i.e. **the whole Objects path is gated by one runtime
on/off flag on this singleton**, a concrete, checkable discriminator for the next unit (does a
normal game session have this flag set? under what condition is it clear?).

**`FUN_00457c60`'s loop (read in full, §0 point 4), restated as plain facts, not pseudocode:**

- Allocates an output array sized `40 × count` bytes through the singleton's own factory
  interface (`[this+0x54]`, vtable slot `+0x38`, called twice — once for a working buffer, once
  for the array itself).
- Per object, seeks the generic stream (`call 0xda7f30` — this is `FUN_00da7f30`, **the exact seek
  primitive `spec-zone-data-format.md` §9.3 already named** as part of the generic named-property
  mechanism's own stream abstraction) by ~~`(u16 field at header+0x1a) + 0x1c`~~ **CORRECTED:
  `(u16 field at header+0x16) + 0x1c`** bytes — i.e. **a
  28-byte fixed base plus a variable tail whose own length is carried in the header.**
- ~~Reads a name/type value, hashes and resolves it~~ **CORRECTED (U0+U1 pass): the header's
  `+0x10` field is already a precomputed 32-bit integer hash (not a string, and not re-hashed
  from any string at load time) — confirmed by disassembling `0xdab2b0` itself, which takes one
  integer and a modulus and runs a textbook integer-avalanche mix, with no byte/character loop
  anywhere in its body.** That value resolves to a small byte ordinal via a lookup through
  `[this + 0xc6c]` (ordinal `0xFF` = absent, exactly the sentinel convention
  `spec-zone-data-format.md` §9.3 already documented for a *different* ordinal table) — then uses
  that ordinal to index a **second** per-class table at `0x2cc9900` and calls its vtable slot
  `+0xc` (a count/capacity probe — `jle` skips the object if it returns ≤0).
- On success, calls that same per-class entry's slot `0` (a **Create**), writes two fields from
  the object's own stream-read pair plus the stream cursor into the new instance
  (`+0x8`/`+0xc`/`+0x10`/`+0x14`), registers it via `[this+0xc58]` (`FUN_00458110`), stamps a
  sequential `u16` index at `+0x30`, and appends it to the output array before advancing to the
  next entry.

**This is a complete, concrete object-spawn loop**: per placed object, resolve a hashed class
name → class-specific create+deserialize, feed it the stream (which, per §9.3, bottoms out in
exactly the kind of `HasProperty`/`SetProperty`/bulk-deserialize mechanism already found there).
**[CONFIRMED — disassembly for the control flow described; HYPOTHESIS for which concrete field is
"class name", "position", etc. — not yet labelled against real bytes.]**

**How this advances `spec-zone-data-format.md` §9.5's own open half directly:** that section's
"concrete next step" asked for *"the table that resolves `FUN_00a33e00`'s (and siblings')
indirect call sites."* `0x2cc9900` is a real, found, per-ordinal table of exactly that shape
(vtable-dispatched, reached from a name-hash ordinal), reached this time from a confirmed
`.czn_pc`-sourced stream. ~~Whether `0x2cc9900`'s registered classes are the *same* class family as
`FUN_00a33ff0`'s ~30 AI/spawn-behaviour flag names, or a sibling family for placeable-object
*types* specifically, is not yet determined — the next unit's first concrete task (dump
`0x2cc9900`'s entries the same way `0x00700780`'s rows were read in `spec-zone-data-format.md`
§9.1, and compare).~~ **CORRECTED (U0+U1 pass, `scratchpad/czn_u0u1_findings.md` §5): leans
negative, with real evidence, not just unresolved. A raw whole-`.text` scan for the literal
`0x2cc9900` found 3,757 reference sites spread across the entire binary (AI, vehicle, and other
unrelated modules), confirmed by sampling several far-away hits to be the same generic
`object+0x34 → class table` idiom used everywhere for ordinary per-object class dispatch —
`0x2cc9900` is the engine's one universal object-class descriptor table, not a zone-Objects- or
placeable-type-private one. It is also a non-file-backed, runtime-populated global (confirmed via
`pefile` section metadata — it reads as all-zero in the shipped executable), so "dump its entries"
as originally proposed is not possible by a static read; it would need emulation of whatever
populates it. The relationship to `0x00a33730` specifically (not `0x00a33ff0`, which is the setup
routine, not the table) remains formally untested either way — see
`scratchpad/czn_u0u1_open_items.md` item 5.**

---

## 5. Record interiors (user's area 2)

Directly downstream of §4: the on-disk object record has, at minimum, these established
boundaries —

- a **28-byte (`0x1c`) fixed base** (exact field layout not decoded this pass — flagged
  HYPOTHESIS, not asserted),
- a **variable tail** whose length is a `u16` at base-relative offset `0x1a` (i.e. inside the
  fixed base, near its end — consistent with a small, fixed "record header" that itself declares
  how much variable data follows, the same general shape as other formats this project has
  already resolved, e.g. the Mesh sub-block's own length-prefixed design),
- an **in-memory working struct of 40 bytes** (`FUN_00457c60`'s own allocation stride) that is
  *not* the on-disk layout — it is what the loop builds *from* the stream, mixing read-back fields
  (`+0x8`/`+0xc` from the object's own data at `[ebp+0xc]` when present, else inherited from the
  section/caller) with runtime-only bookkeeping (`+0x10` handle, `+0x30` sequential index).

**What is NOT yet known, honestly:** which bytes of the 28-byte base are position, orientation,
class-name hash, flags, etc. — none of the field-level semantic work `spec-ctorless-types.md`
§5.1/`spec-world-streaming.md` §10.7 already did for the **unrelated** `.czh_pc` 14-byte
level-mesh-placement record (position `s16`×3 at `1/64 m`, a `u16` name index, an unresolved `s16`
at `+6`) has been attempted here. **That `.czh_pc` record is a different, already-mostly-resolved
structure for a different purpose (level-mesh LOD-tile packing, not general object placement) —
do not conflate the two when scoping unit U2; they are siblings in the same file family, not the
same record.**

**Concrete next step for unit U2:** with `FUN_00457c60` and the class-create path from `0x2cc9900`
both now named, the natural move is to dump a real shipped `.czn_pc`'s `0x2234` section bytes next
to this control flow and label the 28-byte base field-by-field by replay — exactly the "hypothesis
tested against the whole population with controls" method `spec-zone-data-format.md` already
models, now that there is a real consumer to replay against instead of guessing at a generic
chunk format (which is exactly what made the earlier, now-superseded attempts fail, §2 of that
document).

**Partial real-data answer landed (2026-10-07, `scratchpad/czn_player_spawn_2026-10-07.md`,
dispatched for a different question — player spawn position — but it is the first pass to
actually open real `0x2234` section bytes, which is this note's own proposed U2):** ~~which bytes
of the 28-byte base are position, orientation... has been attempted here~~ **position/orientation
are NOT in the 28-byte fixed head at all — they live in the variable tail, as an ordinary
`{kind,size,id}` property** (id `3367956165`, either `kind=2`/12 bytes = plain X/Y/Z, or
`kind=3`/28 bytes = X/Y/Z + a 4-float quaternion), the exact same generic property mechanism
§11.7 already describes for the object's name (id `895416646`, empirically pinned by that same
pass). Confirmed against real bytes in `sr3_city~fawld.czn_pc` (the main open-world zone,
2,172 objects, walked exactly to the declared count with zero overruns — the first population-
shaped confirmation that §11's whole chain parses real data end to end) and several safehouse/
mission/minigame zones (`sh01`, `sh03`, `mm_a_01`, `f_a_tm_dt_02`, `m18`, and 25 more). The 28-byte
head itself is now fully field-mapped by that same real-data pass plus `spec-zone-data-format.md`
§11.4's own code-level read: handle, owner handle, class-name hash, tail length, and name offset —
no position/orientation field anywhere in it. **U2 is not fully closed by that pass** (it answered
one specific question — player-start markers — not a full field-by-field census of every class;
see that write-up's own open items for what remains, notably the tail-property sequential walker's
own `ok=False` result on every multi-property tail actually inspected, item 4 there), but the
"dump real bytes next to the control flow" step this note proposed has now genuinely started.

---

## 6. Navmesh (user's area 3)

**Where it lives:** id `0x2235`, name `"Navmesh"`, description `"The navmesh data for the zone"` —
read directly from the shipped executable's own table (§2). Its handler, `FUN_008642a0` (~131
bytes/52 instructions — not fully read past the initial tag-match this pass), is called from the
already-known `FUN_008652d0` dispatcher, itself confirmed on the live runtime zone-load path
(§3). **This answers the task's "embedded directly, or referenced elsewhere" question at the
location level: embedded directly, as its own tagged section, with a real, live, called
handler.** **[CONFIRMED — disassembly for the table entry, the tag match, and the call site;
OPEN for the payload's own layout — the handler's body past the tag match was not read this
pass.]**

**What this project already has, separately, and how the two may or may not relate:**
`spec-ai-runtime.md` §8/§9 and `spec-player-runtime.md` §10.6 already document a *runtime* navmesh
API used for AI/human pathing: `FUN_00446c60` (resolve a position to a navmesh cell),
`FUN_00445980` (two cells connected?), `FUN_00a12180`(×2)/`FUN_00a11e10` ("does a path exist
between two humans' cells"), and the human navigator `FUN_00993e90` → path-planner `FUN_00993630`
(the latter two, at ~1.7 KB/435 instructions and ~1.3 KB/365 instructions respectively, are
substantial enough to be real pathfinding logic, not thin wrappers). **Confirmed separately: this
is not Havok AI, Recast/Detour, or Kynapse** (zero binary occurrences of any of their identifying
strings — already established, `spec-physics-format.md`'s middleware census) — so this is a
proprietary system, and the "cells" terminology plus a "256 grid units" constant
(`spec-ai-runtime.md` §9) suggest a coarse spatial grid rather than a classic polygon navmesh.

**The open discriminator unit U3 must settle, stated as a falsifiable question:** does
`FUN_008642a0`'s payload populate the same cell/grid structure `FUN_00446c60` resolves positions
against, or is the file-embedded `0x2235` section a different (older? editor-only? per-zone
local) representation that the shipped game's actual pathing never reads? A clean test: trace
whether `FUN_008642a0` (or whatever it calls past the tag match) ever calls into `FUN_00446c60`'s
own module, or writes into any global `FUN_00446c60` reads from — a direct call-graph/data-xref
check, the same style of discriminator this note used throughout §3–§5, not yet run for this pair.
**If they are the same system, this is a major, clean closure — the navmesh "where" and "how it's
used at runtime" questions answered together. If they are not, that is still a real, useful
negative** (e.g. it would mean the shipped game computes pathing data at runtime from collision
geometry rather than from this file section, and `0x2235`'s payload is editor/tool-only data that
ships but is never consulted — a parallel to the already-documented "authoring `.zonex` inputs
don't exist, so the game takes the lookup branch instead" pattern `spec-world-streaming.md` §10.5
found for a different function).

---

## 7. Bonus context: the other 17 named section kinds (not core scope — informational only)

The table in §2 incidentally names and locates handlers for 17 further section kinds the user did
not ask about (Traffic, Sidewalks, Mover constraints, Heightmap, Water volumes/surfaces, Parking,
Wave killers, Light clip meshes, Undergrowth, Supplemental LOD, Fading grid data, Audio Emitter
rbb tree, c_object rbb tree, Crunched ref geom, and the dead Trailer kind). **These are out of this
note's requested scope** (object placement/property stream, record interiors, navmesh) and are
recorded here only because finding the table made them free to name. They would make a sensible,
low-priority catch-all unit (U4, §8) if and when the project wants full `.czn_pc` coverage, but
nothing here should be read as a recommendation to work on them before the three areas the user
actually authorized.

---

## 8. Proposed RE units

Sized on this project's usual scale (U0–U4, ~0.6–5k instructions each), with real root candidates
and size estimates from this pass's own heuristic function-size reads (byte range to a plausible
`ret`+padding/next-prologue boundary — an estimate, not a Ghidra function-boundary read; flagged
as such in each row).

| Unit | Covers | Root candidates | Size estimate | Confidence this pass |
|---|---|---|---|---|
| **U0 — the section-kind spine** | Formalize §2's table (all 22 ids, names, descriptions) and §3's call chain (`FUN_00866580`→`FUN_008660e0`→`{FUN_008652d0, FUN_00865f90}`) as a real spec section; finish reading `FUN_008660e0`'s remaining ~1.4 KB; resolve `0x2243`'s separate reachability (`FUN_0085f26e`'s own parent) | `FUN_00866580` (511 B/178 insn, partly already specced), `FUN_008660e0` (~1,695 B/558 insn), `FUN_008652d0` (223 B/98 insn), `FUN_00865f90` (325 B/110 insn), plus ~9 small per-id handler stubs averaging ~150–350 B each | **~1.5–2.0k instructions** | CONFIRMED for the chain itself; the table needs one independent re-check (§2) |
| **U1 — object placement & property stream** | §4 in full: `FUN_00863e90`'s branch, both forwarders, `FUN_00457c60`/`FUN_00457a50` read end to end, the `0x2cc9900` table dumped and compared against `FUN_00a33ff0`'s family, the `0x26a4` gate flag's real condition | `FUN_00863e90` (165 B/60 insn), `FUN_008538f0`/`FUN_00853910` (26 B each), `FUN_00457c60` (1,036 B/321 insn), `FUN_00457a50` (519 B/166 insn), shared callees already partly known (`FUN_00da7f30` seek, `FUN_00456760`, `FUN_004586a0`, `FUN_00458110`, `FUN_00458230`) | **~1.5–3.0k instructions** (core chain ~900 insn; shared-callee depth is the main unknown) | CONFIRMED control flow; this is the single highest-value unit for the user's area 1 |
| **U2 — record interiors** | §5: label the 28-byte base + variable-tail record field by field, by replaying real shipped `.gzn_pc`/`.czn_pc` `0x2234`-section bytes against U1's confirmed consumer, with the population+control rigor `spec-zone-data-format.md` §10 models | Depends on U1's output; no fresh root beyond what U1 names | **Depends on U1 — not independently sizeable yet** (HYPOTHESIS) | Hard dependency on U1 — do not dispatch standalone |
| **U3 — navmesh** | §6: read `FUN_008642a0`'s payload past the tag match; settle the file-section-vs-runtime-cell-API discriminator against `FUN_00446c60`/`FUN_00445980`; if connected, extend into the human navigator | `FUN_008642a0` (131 B/52 insn, payload unread), `FUN_00446c60` (70 B/29 insn), `FUN_00445980` (215 B/89 insn), `FUN_00993e90` (1,736 B/435 insn), `FUN_00993630` (1,293 B/365 insn), `FUN_00a12180` (229 B/82 insn), `FUN_00a11e10` (220 B/68 insn) | **~1.0–2.5k instructions** (small if the discriminator resolves "different/dead" quickly; larger if it resolves "same system" and pulls in the navigator) | CONFIRMED location; OPEN discriminator — the unit's first task decides its own final size |
| **U4 — remaining section kinds (low priority, not core scope)** | §7's 17 bonus kinds, if ever wanted; **`0x2243` Interior triggers should be pulled OUT of this bucket and treated as its own small priority item** given the `triggers.xtbl` closure opportunity (§0 point 6, §6/§1) | Per-kind handlers already named in §2's table (each ~50–360 B); `FUN_008634e0` (Interior triggers, separately) + `FUN_00dcd3d0` (shared per-instance callee, 6 call sites) + `FUN_00DAB330` reachability check | **~2–4k instructions total if done wholesale; the Interior-triggers sub-item alone is ~0.3–0.6k** | Mostly OPEN; explicitly deferred by this note, not refused |

**Suggested dispatch order:** U0 first (cheap, formalizes what this note already found, and its
one open question — `FUN_008660e0`'s remaining body — may itself answer §3's `0x2243`
reachability question for free). U1 next (the user's clearest-named priority, and the unit most of
this note's budget already de-risked). U3 can run in parallel with U1 — it is independent. U2
waits on U1. The Interior-triggers sub-item of U4 is a cheap, high-value side quest (closes a
named loose thread in a different spec file) and could be slotted in alongside U0 if an agent is
free; the rest of U4 should wait.

---

## 9. Open items / risks, stated plainly

1. `FUN_008660e0`'s own ~1.4 KB unread remainder (§3) — may contain the `0x2243` answer, may
   contain more than the two dispatcher calls found, not yet known either way.
2. The §2 table's name/description pairing convention (one-ahead layout, see table note) is HIGH
   CONFIDENCE from internal content sense plus two independent prior matches, but has not been
   re-derived by a second reader or tool this pass — recommended first action of U0.
3. The `0x1c`(28)-threshold field in `FUN_00863e90` and the `0x26a4` flag byte on the `0x2442750`
   singleton are both load-bearing gates whose actual real-world value in a normal shipped session
   was not checked (static code reading only — no process was run, consistent with this project's
   current static-only method for this item).
4. `0x2cc9900` vs. `FUN_00a33ff0`'s `0x0117f2a4`-rooted class family: same table or a sibling? Not
   determined — flagged explicitly rather than assumed either way, per `HANDOFF.md`'s standing
   "when your count/finding could plausibly be two different things, say so" discipline.
5. The navmesh discriminator (§6) is the one area where this pass could not even get a tentative
   lean — it is a genuine open fork, not a hedge.
6. Nothing in this note was checked against real `.czn_pc`/`.czh_pc`/`.gzn_pc` file bytes. Every
   "CONFIRMED — disassembly" label above is honest about covering control flow only; no
   population count, no control, and no replay-to-EOF check (this project's own standard of
   rigor, `spec-zone-data-format.md` §10.3) has been run on anything in this note. That is
   deliberate — this is a scoping pass — but it means **nothing here should be cited as a closed
   finding until a real unit does that work.**

---

## 10. Cleanroom compliance

No decompiled code, pseudocode, or original identifiers from any source are reproduced above.
Function addresses are evidence anchors (this project's standard convention). Quoted strings
(`"Objects"`, `"Game objects, as placed in the WE"`, `"Navmesh"`, `"The navmesh data for the
zone"`, `"???"`, and the other kind names/descriptions in §2) are shipped engine string data, read
directly from the executable's own `.rdata`, cited the same way this project already cites the
Mesh/Morph block self-identification strings and `SR3Z`'s error text. No leaked Saints Row
IV/Re-Elected source, leaked Havok source, or any other FORBIDDEN material (`HANDOFF.md` §27.H-1)
was read, consulted, or used at any point in this investigation.

## Changelog

- 2026-10-07: initial scoping note, written immediately on the user's hold-lift authorization.
- 2026-10-07: U0+U1 pass (`scratchpad/czn_u0u1_findings.md`,
  `scratchpad/czn_u0u1_open_items.md`) re-disassembled every function this note cites for those
  two units and corrected four points in place, light-touch strikethrough style, rather than
  leaving them to drift: (1) the per-object seek length's on-disk source offset is `+0x16`, not
  `+0x1a` (`+0x1a` is only where the field gets copied to in memory); (2) the two top-level
  dispatcher calls inside `0x008660e0` are each individually gated on a buffer-presence test, not
  literally unconditional; (3) the record head's `+0x10` field is a precomputed 32-bit integer
  hash, confirmed by disassembling the mixing function itself, not a string being hashed at load
  time; (4) `0x2cc9900` is confirmed, by a 3,757-site whole-binary xref census, to be the engine's
  one universal object-class table, not a zone/placeable-type-specific sibling of
  `spec-zone-data-format.md` §9.3's `0x00a33730` — the relationship to that specific table remains
  open, but the original "very plausibly" framing is corrected to a real, evidenced lean away from
  it. No other claim in this note was found wrong; everything else was independently re-confirmed.
- 2026-10-07 (prop-tiles follow-up, `scratchpad/czn_prop_tiles_2026-10-07.md`): resolves
  `HANDOFF.md` §27.H-0c dispatch item 3 ("prop placement within tiles"), which this note did not
  itself address. **Verdict: not a separate mechanism.** A "prop" is an ordinary Objects (0x2234)
  record, confirmed against 7 real shipped `.czn_pc` Objects sections (155 objects, 0 bytes left
  over in any file, no tile/cell field anywhere). "Tile," in this project's own specs, is a term
  this note never used but that turns out to carry two unrelated senses elsewhere:
  `spec-zone-data-format.md` §10's `.gzn_pc`-segment-packing sense (irrelevant to object
  placement) and `spec-world-streaming.md` §2/§3/§10's spatial `CCRR`-grid sense (where one tile
  *is* one `Zone` container, i.e. one `.czn_pc` zone — the identity that makes "props within a
  tile" collapse to "the Objects section of that tile's own zone," already fully covered by §11).
  A genuinely separate, already-documented, tile/fine-cell-scoped mechanism does exist, but for
  static `.clmesh_pc` level-mesh geometry via `.czh_pc` records (`spec-world-streaming.md` §10.5),
  not for anything in `.czn_pc`'s Objects section.
