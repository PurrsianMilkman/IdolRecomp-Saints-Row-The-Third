# `0x0153b530` ("current scene entry"): value before any writer has ever run

Team A, 2026-10-02. Investigative pass against the real executable (Ghidra, headless, `-readOnly
-noanalysis`, `ghidra/CrreishDump.java`, run against a private copy of the project to avoid lock
contention with other concurrent jobs). Dump output kept in this session's scratchpad, not
committed; findings described in prose below.

**The question (Team B mission-blocker, `mm_p_01` getting stuck at a `zscene_prep` call):**
`spec-lua-api-behaviour.md` §26.25 already fully documents `0x0153b530` as the "current scene
entry" global, with all 4 setters (`0x00720410`, `0x00722f10`, `0x007231e0`, `0x00728440`) and both
clearers (`0x00720320`, one path of `0x00721c20`) CONFIRMED. What it does not say is what the field
holds before any of those six functions has ever executed — i.e. its value at game start, before
the first cutscene-related call of any kind.

## Section / initializer (CONFIRMED — disassembly)

A `range` dump of the surrounding bytes (`0x0153b4f0`–`0x0153b580`, covering this address plus its
documented neighbours `0x0153b51c`, `0x0153b528`, `0x0153b534`, `0x0153b538`, `0x0153b556`, etc.)
shows the whole span is undefined raw bytes, every one of them `0x00`, inside the `.data` block. A
full-binary cross-reference dump of `0x0153b530` itself reports the same block as "NOT file-backed
(zero-fill/runtime)" with a static dword value of `0x00000000`.

This is exactly the same section characterization the spec already gives `0x0153b556`
(`skip_all_cutscenes`): the global lives in zero-fill `.data`, i.e. it has no static initializer —
its on-disk image contributes nothing but zeroed memory that the loader fills in at run time. There
is no separate static-initializer block (the kind of one-time module-init call the document uses
elsewhere, e.g. the pair characterized for an unrelated subsystem) that writes a non-zero value into
this address: see the write census below.

## Field width (CONFIRMED — disassembly)

Every access to `0x0153b530`, read or write, is a 32-bit (dword) instruction. The four documented
setters write it with a full register-to-memory dword move:

- `0x00720410` (promotion, from the pending slot): `MOV [0x0153b530], EAX`
- `0x00722f10`: `MOV dword ptr [0x0153b530], EDI`
- `0x007231e0`: `MOV dword ptr [0x0153b530], EBX`
- `0x00728440`: `MOV dword ptr [0x0153b530], EDI`

and the two clearers likewise write it as a dword:

- `0x00720320`: `MOV dword ptr [0x0153b530], 0x0`
- `0x00721c20` (the "handle not live" path): `MOV dword ptr [0x0153b530], EBX`

All six write sites move a full 32-bit register (or the 32-bit immediate `0`) — never a byte or word
store. On this 32-bit target that is pointer width, so `0x0153b530` is a 4-byte pointer-sized slot,
consistent with the existing text's description of it as an "entry pointer" into the `0xf8`-byte-stride
scene table based at `0x0153b294`: a pointer to the current entry, or null.

## Complete write census (CONFIRMED — disassembly)

A full-binary scan for every reference to `0x0153b530` (reference manager plus a raw byte scan of
every initialized memory block for the address's own little-endian encoding, so it finds any
instruction or static data anywhere that embeds this address, not just ones the disassembler already
cross-referenced) returns 25 total uses in 14 functions. Of those, exactly 6 are writes, and they are
the 6 instructions listed above, inside exactly the 6 functions the spec's table already names
(`0x00720320`, `0x00720410`, `0x00721c20`, `0x00722f10`, `0x007231e0`, `0x00728440`). The other 19
uses are all reads, inside 8 further functions that are all already part of the documented lifecycle
(the prep gate `0x007232e0`, the per-frame promotion driver `0x007258a0`, the per-frame cutscene state
machine `0x0072d660`, the load-completion routine `0x007285c0`, and four smaller readers/guards:
`0x007203e0`, `0x00721db0`, `0x007233e0`, `0x00725df0`).

No other function anywhere in the binary writes, reads, or otherwise references `0x0153b530`. In
particular there is no module-init / static-constructor function touching it (unlike, say, a CRT
start-up routine that stores a vtable pointer into some other static object), and there is no
by-name/cvar-style registration call referencing its address the way `0x0072d330`/`0x0086d770`
register `0x0153b556` as the console-exposed `skip_all_cutscenes` option. `0x0153b530` is purely
internal to the six lifecycle functions; nothing hands its address out to an engine-wide
by-name system, and nothing seeds it with a non-zero default.

## Answer

Before any cutscene-related call has ever happened, `0x0153b530` is exactly the zero-fill value its
static image shows: a null pointer, i.e. "no current scene entry". There is no static initializer of
any kind that sets it to anything else, and the complete write census above rules out a start-up
writer that the spec's table might have missed — the same six functions already documented are the
only code anywhere that ever touches this address with a write. A host should treat this global as
null/no-current-scene at game start and at any point before the first `zscene_prep`/teardown/
promotion/load-complete/destroy call runs, purely by construction of the zero-fill `.data` section —
the same mechanism (not a coincidence) that already applies to `0x0153b556`.

## Self-check

Cleanroom grep pattern run against this note's prose (the decompiler-auto-name pattern the task
specified): no matches in the body text above (the body uses only real disassembly register names
such as EAX/EBX/ECX/EDI/ESI, raw addresses, and address-only function references; none of the
decompiler's auto-generated local-variable or label names appear anywhere in it).
