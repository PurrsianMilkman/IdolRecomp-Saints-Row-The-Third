# Saints Row: The Third — The Six Extension-less Registered Types

**Prepared by:** SPEC TEAM (cleanroom reverse-engineering process)
**Phase:** 1, the last block of registered-type work. These six were never on `spec-format-inventory.md` §6's target list *because* they declare no file extension — which is precisely the question this pass answers.
**Scope:** Types **25 `Lightset`**, **26 `VINT doc`**, **33 `Mission override table`**, **34 `Activity table file`**, **38 `Light Curve`**, **43 `Vehicle customization camera`** — how they receive data, what each does with it, and two corrections to previously-published claims that fell out of the work.
**Method:** All six constructors decompiled; a **recursive** census of every archive entry in the game (405,694, including inside `.str2_pc` bundles) to test whether extension-less files exist at all; and a call-site census of the engine's hash functions. Evidence: `tools/extless.txt`, `hash_sibling.txt`; harness `scratchpad/extless_files.py`.
**Cleanroom compliance:** No decompiled code or original identifiers. Quoted strings are shipped data; offsets and bucket counts are load-bearing.

**Confidence key**: **CONFIRMED — empirical**, **CONFIRMED — disassembly**, **HIGH CONFIDENCE — inferred**, **HYPOTHESIS — unconfirmed**, **OPEN / UNKNOWN**.

---

## 1. Headline results

- **No extension-less file exists anywhere in the game.** A recursive scan of **405,694 entries** across all 38 archives *and* inside every `.str2_pc` bundle finds **zero** entries whose name has no extension. **[CONFIRMED — empirical, exhaustive.]**
- **So these six types are never resolved by filename.** They still take the normal `(name, buffer, size, …)` constructor signature and really do receive buffers — so they are **constructed programmatically**, by whatever subsystem already holds the data, rather than by the resource system resolving a path. The registration entry exists to give that data a type, a name and a lifetime, not to find it on disk. **[CONFIRMED — disassembly + the exhaustive negative.]**
- **`FUN_00754410`, listed in the inventory as the destructor for nine types, is a no-op** — its entire body is a return. It is also *called* as a labelled hook (e.g. with the literal `"Lightset"` and a size), so it is a **stripped instrumentation stub**, not a destructor. Those nine types have no teardown at all. **[CONFIRMED — disassembly.]**
- **All four load-time patterns this project has catalogued reappear here**, which is a good sign the catalogue is complete: parse-in-constructor (25, 33, 38), stash-only (26, 43), and register-into-a-hash-table (34, 38).
- **A breadth correction on the engine's hashes** (§4): the rotate-6/XOR hash this project has repeatedly called "the engine-wide string hash" has **18 call sites**. The table-driven CRC-32 has **835**. Calling the former engine-wide, relative to the latter, was backwards.

## 2. The six types

| ID | Name | Pattern | What it does |
|---|---|---|---|
| 25 | Lightset | parse-in-ctor | Requires a buffer and size and **no `g`-side** (rejects if either paired argument is non-zero), then hands `(name, buffer, size, context)` to a real consumer. Calls the no-op hook with the literal label `"Lightset"`. **[CONFIRMED — disassembly.]** |
| 26 | VINT doc | **stash-only** | Looks up an existing slot by name (case-insensitively), and on a match stores **buffer, size, `g`-buffer and `g`-size** into the slot at `+0x10`/`+0x18`/`+0x14`/`+0x1C`. Parses nothing. Takes the 6-argument paired form, so a VINT doc *can* have a `g`-side. **[CONFIRMED — disassembly.]** |
| 33 | Mission override table | parse-in-ctor | Creates a named document context (`"mission_override_table"`), wraps the buffer in it, navigates to a node named `"Table"`, and parses it — the field names its parser reads are `"mission_override"`, `"external_light_override"`, `"mission_name"`, `"weather_stage"`. **This is an XML/`.xtbl` document handed over as a buffer**, not a bespoke binary format. **[CONFIRMED — disassembly.]** |
| 34 | Activity table file | register | Hashes the name with the **CRC-32** routine and stores it into a table that holds **at most two** entries (the constructor refuses beyond that). **[CONFIRMED — disassembly.]** |
| 38 | Light Curve | register + parse | Hashes the name with **CRC-32**, mixes that integer to a **512-bucket** index (§4.1), and looks it up: if already present, returns success immediately; otherwise allocates and parses. **[CONFIRMED — disassembly.]** |
| 43 | Vehicle customization camera | **stash-only** | Stores buffer and size into two globals and returns success — byte-for-byte the `.csc_pc` pattern (`spec-cutscene-camera-format.md` §1), including the consequence that **only one can be live at a time**. **[CONFIRMED — disassembly.]** |

Types 33 and 34 are notable for a different reason: they confirm that the resource system is used to carry **`.xtbl` table data** as well as binary assets, which is why they need no extension of their own — the `.xtbl` file already has one and is loaded by its own type.  **[HIGH CONFIDENCE — inferred from type 33's document-node parse.]**

## 3. What this means structurally

The registration table's five load-time shapes are now fully enumerated:

1. **Parse in the constructor** — most binary asset types.
2. **Stash-only** — morph 11/12, csc 24, ctdg 42, VINT doc 26, vehicle-customization camera 43. The constructor records the buffer somewhere and a later subsystem parses it.
3. **Flag-variant thin constructor** — the PEG family, types 3/17/18 (`spec-texture-format.md` §7–§8).
4. **No constructor at all** — types 29/30/39/44/45 (`spec-ctorless-types.md`): the dispatcher skips the call and reports success.
5. **No extension at all** — the six here: never filename-resolved, constructed programmatically.

Shapes 4 and 5 are independent: a type can have one, the other, or both (39 `Buffer` has both, and ships nothing at all).

## 4. Correction: which hash is actually the engine-wide one

A call-site census of the three string hashes gives:

| Hash | Call sites | Where this project had seen it |
|---|---|---|
| Table-driven **CRC-32**, lowercased (`FUN_00d9e8b0`) | **≈835** | documented as the `.ctdg_pc` conversation-registry key |
| **Multiply-33 / XOR**, lowercased, `% buckets` (`FUN_00dab330`) | **≈174** | documented as the always-loaded texture registry's bucket function |
| **Rotate-6 / XOR**, lowercased (`FUN_00da7890`) | **≈18** | documented as *"the engine-wide string hash"* |

The rotate-6/XOR hash is the **narrowest of the three**, not the broadest. Its documented uses — archive filenames, `.rig_pc` bone names, the foliage registry, `.ctdg_pc` speaker ids — are apparently close to its complete set of uses, which is consistent with an 18-call-site count. Meanwhile the CRC-32, which this project first met as one format's registry key, is the engine's general-purpose name hash by a wide margin. **[CONFIRMED — empirical call-site census.]**

This is the **third** revision of this claim (`spec-vpp-container.md` §2.2 has gone "exactly one" → "two" → "at least three"), and it is a different *kind* of revision from the first two: those added hashes, this one corrects the **relative breadth** of the ones already known. The phrase "engine-wide" should be dropped in favour of naming the actual uses.

*(Caveat on the numbers: these are Ghidra reference counts and include some call sites Ghidra could not attribute to a named function. The counts are reliable as orders of magnitude — 18 vs 174 vs 835 — not as exact totals.)*

### 4.1 Not a fourth hash

Light Curve's 512-bucket index comes from `FUN_00dab2b0`, which looked like another string hash and is not one: it takes an **integer**, not a string, and applies a shift/XOR/multiply avalanche (`~(x<<15)+x`, `(x>>10 ^ x)*9`, `x ^ x>>6`, `x + ~(x<<11)`, `x>>16 ^ x`) before the modulo. It is a well-known **integer mixer** applied to an already-computed CRC-32, not a name hash. Checked precisely because its address sits `0x80` from the multiply-33 function and the assumption would have been cheap to make. **[CONFIRMED — disassembly.]**

## 5. Open Items

1. Which subsystems construct each of these six, and from what buffer — the constructors were read, their callers were not.
2. Type 25 `Lightset`'s actual payload format (its consumer was not opened).
3. Type 38 `Light Curve`'s parse routine.
4. Why type 34 `Activity table file` is capped at two entries.
5. Whether any of the nine types whose "destructor" is the no-op stub leak, or whether their teardown lives elsewhere.
