# Stub hit-list 2026-10-02 — 27 generic-stub Lua names and 9 OPEN_STATE items (Team A, 2026-10-03)

Source list: `D:\Project Crreish\TEAM B\results\stub_ranking_with_specced_20261002.tsv`. Every row whose `specced`
column is `n(generic-stub)` is covered here: **27 names** (the brief's "~26"). Every `OPEN_STATE:` row is covered in
section E except `OPEN_STATE:named-object resolution['Killbane']`, which is **out of scope and was not touched** (E.10).
Run locally, read-only, on a private copy of the Ghidra project (`tools\gp_stubhits`, deleted afterwards), with
`CrreishDump.java <out>/lua1 lua depth:1 maxfuncs:15 maxinsn:500 <27 names>`. **All 27 handlers located; all 27 names
resolved.**

**What `n(generic-stub)` means (read first).** Team B's own method note (`mission_drive_20261002_next_blockers.md`
§"Method notes") defines it as "falls through to the project's generic always-stub, no dedicated behaviour modelled" —
i.e. *Team B has not implemented the name*, not that no spec exists. **21 of the 27 names already have a spec entry**
(`spec-lua-api-behaviour.md` or `spec-lua-bindings.md`; table A). For those 21 this note re-verifies the entry against a
fresh dump, records corrections, and closes residual OPEN items where cheap. **6 names had no entry anywhere** and are
specified from scratch in section B.

Follow-up runs on the same private copy, cited by name below:

- "follow-up dumps" `fu1`…`fu7`: depth-0 `func` runs (`maxinsn:900`) on the callees listed in section L;
- "xref run": `xref xrefs:2000` on `0x02cc4700 0x012f44fc 0x0153b520 0x0153b528 0x024e97c3 0x02300884 0x022ccc7e
  0x024e4820`;
- "range run": `range` mode on seven stretches of undefined code (section L);
- "ptrs runs": `0x0096e888`, `0x012e6ecc`, `0x012e6ee8`, `0x01122928`, `0x0101c3c8`, `0x0129fcc4`, `0x0125ae34`;
- "displacement scan": a scratchpad-only read-only script (not added to the project) listing every instruction whose
  operand carries the memory displacement `+0x674`, `+0x670` or `+0x14` (section E.1).
- "verification passes": three parallel read-only passes (UI names, gameplay group 1, gameplay group 2) that read the
  `lua1` dumps plus their own follow-up dumps on their own private project copies (all deleted afterwards). Sections C and
  D summarise their results; two of their load-bearing claims were re-read in the listing by this note (C.5, D.3).

Labels: **CONFIRMED** = read in the listing of a dump made for this note; **HIGH CONFIDENCE** = follows from a dumped call
or reference, but the callee body was not dumped or a meaning is inferred; **HYPOTHESIS** = plausible, not settled;
**OPEN** = not settled (collected in section N).

**Shared conventions, cited not re-described:** every entry opens with the `lua_gettop` prologue (`0x00dfde50`) and reads
arguments with negative stack indices counted from the bottom (`-N` = arg 1, `1-N` = arg 2, …). Primitives as in the
ranking-tranche front matter: `0x00dfe210` `lua_tolstring` (non-string, non-number reads as null), `0x00dfe160`
`lua_tonumber`, `0x00dfe1e0` `lua_toboolean`, `0x00dfe040` `lua_type` (0 = nil), `0x00dfe3a0` `lua_pushnumber`,
`0x00dfe590` `lua_pushboolean`, `0x00dfe420` push C string (null pushes nil), `0x00dfe380` push nil, `0x00dfe5e0`
`lua_gettable`, `0x00dfec70` `lua_next`, `0x00dfde60` `lua_settop`, `0x00dfe830` `lua_setfield`, `0x00dfe4f0`
`lua_pushcclosure`, `0x00ea2596` truncating float-to-int. Two more primitives were dumped for this note (CONFIRMED):
**`0x00dfe670` = raw get** (replaces the key on top of the stack by the raw table lookup through `0x00e04b30`) and
**`0x00dfdeb0` = remove** (shifts every slot above the index down by one and drops the top). **Missing arguments:** an
unconditional read of an absent argument is **not nil** — it reads the stale slot at the top of the stack (tranche 22
M.1, from a dump of the index resolver `0x00dfdc60`); "nil-gated" = the count is tested before the slot is read.
Resolvers: generic character resolvers `0x00a281a0` (full chain, accepts `#PLAYER#`), `0x00a28150`, `0x00a280c0`
(player-like only, with a `#PLAYER#` fallback to the local-player getter `0x009da4e0`); vehicle resolver `0x00a281e0`;
liveness `0x00853b10` (true = dead). Session getter **`0x0087ba20` is a single read of `0x024d8534`** (CONFIRMED,
follow-up dump) — "host" = session `+0x5c` == `+0x58`. Single player has **no session object** (`spec-lua-api-behaviour.md`
§8.27/§26.28, CONFIRMED there); this note draws no new conclusion about sessions and only states where a no-session path
leads. Remote co-op player `0x009df3d0`, first remote session member `0x008b7100`. Record trio: `0x0086f5f0` open,
`0x0086f1b0` broadcast commit (no-op without a session), `0x0086f110` single-target commit, `0x0086eb20` close. Shared
no-op stub `0x007c9f50`. Wwise name resolver `0x00462960`. Lua-callback coroutine mechanism `0x00e0ca80`/…/`0x00e0cef0`.

---

## A. Registrars — where the 27 names live

All 27 name strings occur exactly once in `.rdata`; in every case the handler is the code pointer in the slot right after
the name (insn offset +1) of a name-then-function registrar — CONFIRMED, `lua1/index.txt`. **No pointer-before-name
registrar is involved** (the `0x007dfae0` pause-map registrar's 9-pair array was re-read in full by the UI verification
pass and is name-then-function). `audio_object_post_event` is registered **twice**, by the gameplay registrar
`0x00a20840` (`0x00a20c63`) and by the 113-name `game_` registrar `0x00845aa0` (`0x00845b01`), both pairs pointing at the
same handler — CONFIRMED.

| # | name | registrar | handler | existing spec entry | section |
|---|---|---|---|---|---|
| 1 | `vint_set_property` | UI `0x00e1dfb0` | `0x00e1d1f0` | bindings §20.1 | C.5 |
| 2 | `audio_object_post_event` | gameplay `0x00a20840` + `game_` `0x00845aa0` | `0x00a3cb00` | behaviour §2.4 | D.1 |
| 3 | `vint_get_property` | UI | `0x00e1b4d0` | bindings §20.2 | C.6 |
| 4 | `vint_get_time_index` | UI | `0x00e1b9b0` | bindings §19.1 | C.3 |
| 5 | `vint_dataresponder_finished` | UI | `0x00e19e50` | bindings §21.1 | C.7 |
| 6 | `vint_internal_dataresponder_request` | UI | `0x00e1af70` | bindings §21.2 | C.8 |
| 7 | `vint_dataitem_get` | UI | `0x00e1aeb0` | bindings §19.2 | C.4 |
| 8 | `vint_object_first_child` | UI | `0x00e1b7a0` | bindings §18.1 | C.1 |
| 9 | `vint_object_clone` | UI | `0x00e1aa90` | bindings §18.2 | C.2 |
| 10 | `pause_map_stag_current_district_control` | pause map `0x007dfae0` | `0x007de0d0` | behaviour §31.2 | C.9 |
| 11 | `player_controls_disable` | gameplay | `0x00a59630` | behaviour §7.22 | D.3 |
| 12 | `traffic_disable_lanes` | gameplay | `0x00a61760` | behaviour §12.19 | D.7 |
| 13 | `city_zone_swap` | gameplay | `0x00a43460` | behaviour §1.9 | D.2 |
| 14 | `party_dismiss_all` | gameplay | `0x00a593f0` | behaviour §14.17 | D.9 |
| 15 | `mip_streaming_pause` | gameplay | `0x00a52360` | **none** | B.1 |
| 16 | `object_spawn_pause` | gameplay | `0x007c9f50` (shared no-op stub) | **none** | B.2 |
| 17 | `character_ragdoll_set_last_resort_position` | gameplay | `0x00a45f00` | behaviour §7.18 | D.4 |
| 18 | `customization_create_character` | gameplay | `0x00a42db0` | **none** | B.3 |
| 19 | `customization_creation_is_open` | gameplay | `0x00a42dd0` | **none** | B.4 |
| 20 | `customization_outfit_wear` | gameplay | `0x00a43b10` | behaviour §13.4 | D.8 |
| 21 | `customization_screen_is_ready` | gameplay | `0x00a42e40` | **none** | B.5 |
| 22 | `mesh_mover_hide` | gameplay | `0x00a52f90` | behaviour §9.16 | D.5 |
| 23 | `mesh_mover_show` | gameplay | `0x00a53440` | behaviour §9.17 | D.6 |
| 24 | `mission_autosave` | gameplay | `0x00a52b10` | behaviour §17.23 | D.11 |
| 25 | `mission_set_next_mission` | gameplay | `0x00a53b40` | behaviour §25.4 | D.12 |
| 26 | `player_parachute_has_backpack` | gameplay | `0x00a59840` | **none** | B.6 |
| 27 | `sidewalk_disable_nodes` | gameplay | `0x00a5f710` | behaviour §15.3 | D.10 |

(The 6 "none" names were the second half of the never-written-up job `ghidra\jobs\ranking-tranche-2.json`.)

---

## B. The 6 names with no earlier spec entry

### B.1 `mip_streaming_pause` → `0x00a52360`

**Arguments:** 1 boolean = pause (unconditional `lua_toboolean`); 2 string = a reason (unconditional `lua_tolstring`).
**Return:** 0 values. CONFIRMED — listing.

**Body (CONFIRMED — listing, follow-up dump of `0x00707410`, `0x004590d0`, `0x004591f0`, `0x007073e0`):** the handler
calls `0x00707410(pause, reasonPointer)`, which forwards to a method of the mip-streaming object `*0x012ebc78`
(file-backed pointer to the static object `0x014961c0`): `0x004590d0` when pausing, `0x004591f0` when resuming, each with
two stack arguments **(reasonPointer, 0)**. Both methods use only their **second** argument, as a bit index into the
byte `+0xfe2`:

- **Resume** (`0x004591f0`): clears bit `arg2` of `+0xfe2`.
- **Pause** (`0x004590d0`): sets bit `arg2`; if the byte was **zero before** (first pauser), it walks the `+0xfd4` entries
  of the pointer array at `+0x34`; for each entry in state 1 (`+0x18`), it queries both of its handles (`+0x8`, `+0xc`)
  through `0x00dc3750`/`0x00dc2590`; when both report status 4 it releases them (`0x00dc3ab0(handle, 1)`), sets the entry
  to state 0 and releases its two auxiliary resources (`+0x24`/`+0x28`, through `0x00e55060` and a virtual call on the
  object at `+0xfd8`); otherwise it marks the entry state 4 and sets byte `+0xfe3`. HIGH CONFIDENCE: "cancel in-flight mip
  loads"; the per-frame consumer of `+0xfe2` was not dumped (OPEN).

**Findings (CONFIRMED structure):**

- **The reason string is ignored.** The Lua path always passes bit index **0**; the engine's other user, `0x007073e0`
  (callers `0x00daff10`, `0x00daf9a0`), passes bit **1**. So all script callers share one bit: one script's
  `mip_streaming_pause(false, "x")` releases every other script's pause.
- Both reads are unconditional; a one-argument call reads arg 2 from the stale top slot (harmless: unused).

### B.2 `object_spawn_pause` → `0x007c9f50` (shared no-op stub)

The registrar pairs the name with `0x007c9f50`, whose whole body is the `lua_gettop` call and `return 0` — CONFIRMED,
listing; the stub's 30-plus registrations are already on record (tranche 17). **Arguments:** none read (Team B's observed
`boolean, string` shape is discarded). **Return:** 0 values. A faithful host does nothing.

### B.3 `customization_create_character` → `0x00a42db0`

**Arguments:** none read. **Return:** 0 values. CONFIRMED — listing.

**Body (CONFIRMED — listing, depth-1 dump, follow-up dumps):** three calls, in order:

1. `0x00831f40`: if byte `0x022425dc` is set, sets byte `0x02300849` = 1.
2. `0x00702770`: (a) `0x00710b80` — if byte `0x0151af40` is set and no cutscene is in progress (`0x00720640` false: true
   only when cutscene state `0x0153b520` > 1 **and** manager `0x0153b528` non-null), shows the "DAMAGED GAMER PROFILE
   STATS" dialog (`"STATS_CORRUPT_TITLE"`/`"STATS_CORRUPT_BODY"` through `0x0084a1b0` and `0x007c3d80`) and clears the
   byte; (b) `0x008318f0(1, 0)` — clears byte `0x02300891` if set, then tail-calls `0x0080cc90` on the store object
   `0x022ccc60` (a customization-screen open/refresh routine; not traced past its first gates); (c) `0x00831f60` — sets
   byte `0x02300849` = **0**.
3. `0x008b71a0`: **reads the session member count with no session test** (crash, below); with more than one member it
   sets byte `0x024e97d0`, marks the remote co-op player (`0x009df3d0`; `+0x1d96` |= `0x80`, virtual `+0x38(0, 1)`), finds
   the first session member that is not session `+0x5c` (walk from `+0x54` through `+0xb28c`), and sends it an
   opcode-`0x31` record (`0x008b6dc0`) carrying two bytes — "the mission `mm_m1_5` is the active mission or is complete"
   (`0x006d7240`/`0x006d6f30`, the latter reading bit 2 of the activity's `+0x88`), which it also stores back into
   `0x024e97d0`, and byte `0x014f3d34` (`0x006e44f0`) — committed single-target.

The same `0x00831f40` → `0x00702770` sequence is the engine's own co-op character-creation path in undefined code at
`0x008ba0c8`/`0x008ba11e` (range run), so the native replays an engine flow — CONFIRMED.

**Crash — no-session read at `0x00681370` (CONFIRMED structure).** `0x008b71a0` loads the session pointer
(`0x0087ba20`) and passes it straight to `0x00681370`, whose body is the single read `[this + 0x60]`. With no session the
read is `[0 + 0x60]` — an access violation. The sibling `0x00867830` performs the same call only after a null test, and
`0x008b71a0` itself null-tests the session for its second use — so this is the one unguarded use. Reachability: per
§8.27/§26.28 single player has no session object, so **any single-player call faults** after steps 1-2 have run. Whether
the shipped script reaches the call in single player is OPEN (the one call site was not read — clean-room boundary).
Fix: return when the session pointer is null.

Other notes: step 1 and step 2c make `0x02300849` always end at 0; the byte matters only to readers that run inside
step 2 (its reader `0x00831f70` has callers `0x005b7430`, `0x0071cb10` and undefined code; not traced) — CONFIRMED
structure, effect OPEN.

### B.4 `customization_creation_is_open` → `0x00a42dd0`

**Arguments:** none read. **Return:** exactly 1 boolean on every path. CONFIRMED — listing.

**Body (CONFIRMED — listing, follow-up dumps, xref run, range run):** true if **any** of the following, tested in order:

1. byte `0x024e97c3` (zero-fill; set to 1 at `0x008b6efa` and cleared at `0x008b6aa3`/`0x008b784f`, all inside co-op
   flows in undefined code);
2. `0x00831910`: dword `0x02300884` non-zero (its only writer, inside `0x00837c70`, stores 0) **or** bit 0 of byte
   `0x022ccc7e` (= store object `0x022ccc60` `+0x1e`);
3. `0x008b6c80`: `0x00867830()` (session exists, host or ready client, more than one member, every member's slot state ≥ 1
   — false with no session) **and** byte `0x024e97d0`;
4. `0x008b6c60`: **true when `[0x024e4820]` is null**; otherwise `0x00877d60(4)` on that object (any session member's
   slot byte below 4).

**Logic defect — constant `true` in single player (CONFIRMED structure).** The only non-null writer of `0x024e4820` is at
`0x0088cd75` inside `0x0088cd40` (range run); the other two writes (`0x0088c0a1`, `0x0088be0f`) store 0. `0x0088cd40` is
the exit callback of mode-manager state 9, which §8.27/§26.28's 2026-10-01 local re-derivation found is never requested
anywhere in the static binary. So in single player term 4 is always true and the native **always returns true**. A
script that waits with `while customization_creation_is_open() do … end` would never leave the loop. HIGH CONFIDENCE
reading: the predicate means "a co-op partner is still in character creation", and its no-network-object default is the
wrong polarity. Faithful host behaviour for single player: return `true`.

### B.5 `customization_screen_is_ready` → `0x00a42e40`

**Arguments:** none read. **Return:** exactly 1 boolean. CONFIRMED — listing.

**Body (CONFIRMED — listing, depth-1 dump of `0x00e0d180`):** it calls `0x00e0d180("Store_lineup_is_loaded",
interfaceState)` (the interface Lua state `0x02a45450` via `0x00e1a1b0`) and pushes the result. `0x00e0d180(name, L)`:
returns false when `L` is null; pushes `"_DynamicGlobals"` and raw-gets it from the globals table; if that is a table,
walks it with `lua_next`; for each value that is itself a table it raw-gets `name` from it; the **first non-nil value
found** is converted with `lua_toboolean` and returned (stack restored with three removes and a pop); no table, or no
sub-table defining the name, returns false. So the native reports the truthiness of the UI global
`Store_lineup_is_loaded` from whichever loaded UI document defines it (iteration order of `_DynamicGlobals`; first hit
wins). No crash shape. Host: false until a UI document sets that global.

### B.6 `player_parachute_has_backpack` → `0x00a59840`

**Arguments:** 1 string = player (unconditional; `0x00a280c0(name, 0)` — plain player-like name or `#PLAYER#`).
**Return:** exactly 1 boolean. CONFIRMED — listing.

**Body (CONFIRMED — listing, depth-1 dump of `0x009daed0`, follow-up dump of `0x009daeb0`):** unresolved → false;
otherwise `0x009daed0(player)`: false if the player's customization handle `+0x2088` is null, else the byte at
`[+0x2088] + 0x1c`. That is exactly the byte the setter `0x009daeb0(player, value)` writes, and `0x009daeb0` is the
local-apply branch of `player_parachute_wear_backpack` (§12.28, `0x00a5a41b`) — so the pair is CONFIRMED to share one flag.
(The same byte is the "sub-slot selector" §19.1/§19.2 reset from the `+0x20f8` template; HIGH CONFIDENCE they are the same
field.) Note the byte is returned as-is through `lua_pushboolean` (any non-zero is true). No crash shape; a one-argument
read only.

---

## C. Re-verified UI-state names (UI verification pass; spec `spec-lua-bindings.md` §18-§21 and behaviour §31.2)

Common resolvers read by the pass (CONFIRMED): object-handle resolver `0x00e28850(handle, scope)` (null for handle 0;
bucket `handle & 0x3f` of `0x02a74be0`, match on `+0x28` and on `+0x30` = scope unless scope is 0); document resolver
`0x00e1f330` (low 16 bits < `0x40`, record `[0x02a4d170] + low16 * 0x5a8`, valid when `+0x580` equals the handle);
current-context accessor `0x00e0ceb0` (entry `[0x02a44d14 + idx*4]`, idx `0x02a44d10`, valid 1..16); property tables
`0x00e30600(typeId)` (stride `0x10` at `0x02a8a538`), hash lookup `0x00e28980` (stride `0x20`, hash at entry `+0x18`, walks
to the parent type), descriptor `+4` type code / `+8` setter / `+0xc` getter. A 24-byte tagged variant is shared by four
of these natives: tag 1/2 64-bit ints, 3 float, 4/9 string pointers, 5 bool, 6 colour (3 values), 7 vec2 (2 values), 8
callback, 10-12 ref-counted handles, 13 enum/int; its push routine `0x00e1a420` (jump table `0x00e1a5c0`) pushes 1 value
for tags 1-5, **3 numbers for tag 6** (packed to 8-bit RGB, then each byte converted sRGB→linear: threshold 0.03928, /12.92,
else ((x+0.055)/1.055)^2.4), **2 numbers for tag 7**, and 1 nil for anything else.

### C.1 `vint_object_first_child` → `0x00e1b7a0` — §18.1 AGREES
Arg 1 unconditional number. Returns **0 values** for a bad handle or no first child; else 1 number, the child's `+0x28`
handle (with the `+2^32` fix-up `0x0113e6c8` for negative raw values). No crash shape. **Correction:** the 2026-10-02
changelog line in `spec-lua-bindings.md` saying the §18 functions "always return exactly one value" is wrong for this one
(§18.1 itself is right).

### C.2 `vint_object_clone` → `0x00e1aa90` — §18.2 PARTLY WRONG
Arg 1 unconditional; arg 2 count- and nil-gated (default 0). Always exactly 1 number: the clone's handle, or `0.0` on every
failure (context or document missing; source not found — logs `"vint_object_clone() - object handle %u not found.\n"`).
The parent resolves **with the current document as scope**; if it is null (absent, nil, 0, or owned by another document)
the source's own `+0x24` parent is used. **Corrections to §18.2:** the clone is a virtual call on the **source object's**
vtable slot `+0x14` (not the document's), with arguments **(1, parent)** (not `(parent, 1)`); the document is only the
parent-lookup scope; the fallback also covers an unresolvable or foreign parent. OPEN: the slot-`+0x14` body and whether a
null parent (root source) is valid. The unbalanced-looking printf pushes on the not-found path are reused by the following
push — not a defect.

### C.3 `vint_get_time_index` → `0x00e1b9b0` — §19.1 AGREES
Arg 1 count-, nil- and zero-gated; absent, nil, 0 or any non-number falls back to the context record's `+0x14` (a numeric
string is converted). Returns 0 values if the context or document is missing; else 1 number, the float at document
`+0x574` widened to double. No crash shape.

### C.4 `vint_dataitem_get` → `0x00e1aeb0` — §19.2 PARTLY WRONG
Arg 1 unconditional. Bad handle → 0 values. Otherwise the field slots (copied by `0x00e254d0`, at most 32) are each pushed
through `0x00e1a420`. **Correction:** §19.2's "each field is a scalar, exactly 1 value" (HIGH CONFIDENCE there) is wrong —
a colour field yields 3 values and a vec2 field 2, so the return count can exceed the field count. Structure note: the push
loop (`0x00e1af23`-`0x00e1af39`) uses the raw count `+0x318` without the 32 clamp the copy applies; the record layout ends
the field array at `+0x318`, so HYPOTHESIS: unreachable (who writes `+0x318` is OPEN).

### C.5 `vint_set_property` → `0x00e1d1f0` — §20.1 PARTLY WRONG
Arg 1 count-gated and type-gated (must be a number, else handle 0); **arg 2 (name) unconditional** (`lua_tolstring` at
`1-N`; stale when absent); value slots and an optional trailing integer ("extra") count- and nil-gated. **0 values on every
path.** The descriptor's type code selects the parse (jump table `0x00e1dab4`, codes 1-13; the full roster is in the
verification notes: ints, float, string, bool, colour from 3 numbers or a colour-name string, vec2, callback claim, ref-
counted handles, enum). **Corrections to §20.1:** (a) the tween redirect (`vtable +0x1c` result equal to `[0x0132c0d0]`,
type `0xe`, name `start_value`/`end_value`) only borrows the **type code** of the target object's target property for
parsing; the write still goes to the tween's own property (`0x00e1da9a`, original object and name) — "writes through to
the target object" is wrong; (b) absent or nil values still write the type's default variant (not a no-op); (c) a type-
mismatched value writes the default and leaves the argument unconsumed, so the "extra" read then sees it; (d) the optional
extra integer and the type-`0xd` enum-string conversion (`0x00e307c0`, falling back to the string when the lookup gives
`0x7fffffff`) were missing. The final write is `0x00e29360` → `0x00e28aa0`, calling the descriptor setter `+8` as
setter(object, variant, extra) with the result ignored. No crash shape; string variants store the raw Lua string pointer
(whether a setter keeps it is OPEN). Re-read by this note: the arg-2 read is the unconditional `1-N` slot — CONFIRMED.

### C.6 `vint_get_property` → `0x00e1b4d0` — §20.2 AGREES
Args 1 and 2 unconditional. 0 values when the name is null, the handle bad, the descriptor missing or the getter returns
false; otherwise the variant's pushes (1, 2 or 3 values per C.0's push table), and a successful getter that leaves tag 0
pushes 1 nil. Getter `+0xc` called as getter(object, out, 0) through `0x00e29300`.

### C.7 `vint_dataresponder_finished` → `0x00e19e50` — §21.1 AGREES
Arg 1 unconditional. Always 1 boolean: **true when no record matches** (including a null or empty name — the empty-name hash
equals the sentinel `0x01248ed4`), else record byte `+0x258`. Lookup `0x00e1e880` over the ring at `0x02a47b64`, hash at
`+0x8`. **Host hazard:** the `vint_lib` wrapper loops while the result is not `true`; a generic stub that pushes nothing
makes that loop spin forever. A minimal faithful host with no responder records returns `true`.

### C.8 `vint_internal_dataresponder_request` → `0x00e1af70` — §21.2 PARTLY WRONG; two crash shapes
Arg 1 unconditional; arg 2 must be a string and arg 3 a number (count- and type-checked; failure returns silently); each
trailing argument becomes a bool, float or string slot, any other type an int-0 slot **without advancing the cursor**.
**0 values on every path.** After parsing, record `+0x25c` = context `+0x14`, then `0x00e1e660` sets `+0x258` = 1 and, when
the callback name and the native handler pointer `+0x2a0` are both non-null, copies the name to `+0x260` and calls the
handler(max, args, count), whose byte result overwrites `+0x258`. Records come from the pool allocator `0x00e1e7e0`
(§14.3), created by native code (about 30 sites, e.g. `0x005f09a0`, `0x007c0840`, `0x0080fb40`). **Corrections to §21.2:**
"up to 16 trailing arguments" is not enforced; "untyped slot" is an int-0 slot with a stalled cursor. **Crash shapes
(CONFIRMED structure):** (a) `0x00e1b0a7`-`0x00e1b14b`: 17 or more trailing arguments write past the 16-slot array on a
`0x198`-byte frame; the 17th slot's tag lands on the saved return address (frame size re-read by this note); (b)
`0x00e1e683`-`0x00e1e68b`: the callback-name copy into the 64-byte buffer `+0x260` is unbounded, and the handler pointer it
calls next sits at `+0x2a0` — a name of 64 or more characters corrupts it. Both need unusual script input.

### C.9 `pause_map_stag_current_district_control` → `0x007de0d0` — §31.2 AGREES
No arguments. Returns 1 number, or 2 numbers (0 then the result) when the selected zone `0x0229a2ac` is 0 (as §31.2 says).
Sums float `+0x3c` over the members (count `+0x198`, 16-bit indices `+0x190`, objects from `+0x58` of `0x03171a64`) whose
`+0x4c` is the selected zone, and the owned share (byte `+0x3a` bit `0x02`); result owned/total narrowed to single, or 1.0
when total ≤ 0 or NaN. Structure note: `0x03171a64` and each table entry are used without null tests (`0x007de10d`-
`0x007de137`); reachability low.

---

## D. Re-verified gameplay names (two gameplay verification passes; spec `spec-lua-api-behaviour.md`)

### D.1 `audio_object_post_event` → `0x00a3cb00` — §2.4 AGREES, OPEN items closed
Arg 1 unconditional (Wwise id; null, empty or `"none"` → id 0 → push 0, return); args 2-5 each count-gated. Arg 2/arg 3: a
string (1 id) or a table of at most 2 (counted by `0x0083dff0`: `t.n` or a pairs count; more than 2 → push 0, return); arg
4 optional object name (`"#PLAYER1_VEHICLE#"` special-cased through `0x00a281a0("#PLAYER1#")`, reading the 64-bit pair at
character `+0x16d8`, falling back to `+0x16c0` when all-zero); arg 5 optional boolean, default true, stored at request
`+0x28`. **Closures:** the list-count mismatch pushes an extra 0 and returns **2 values `(0, handle)`** (`0x00a3ce95`
increments a mismatch flag); the null-arg-4 fallback is the read-only `{0, 0}` pair `0x0117f808`/`0x0117f80c` (an
object-id pair, not a position); the return value is the emitter serial from `0x00a2acb0` (0 = no free slot of 32), not a
Wwise playing id. **New:** the list copy is keyed on the **arg-3** count — with arg-3 count 0 every arg-2 id is dropped;
unfilled id slots are uninitialised stack (`0x00a3ce41`-`0x00a3ce62`, structure). Team B's UI-state call shape
(`string, nil, nil, nil, boolean`) returns exactly 1 value. Wwise id values: E.4.

### D.2 `city_zone_swap` → `0x00a43460` — §1.9 PARTLY WRONG
Arg 1 unconditional (hashed by `0x00d9e8b0` into a local); arg 2 nil-gated boolean, default true. 0 values. Normal path:
`0x0084a910` (activate) / `0x0084a940` (deactivate) — both no-ops when the hash is the current zone hash `0x029c9964`,
otherwise maintain the active-swap list (count `0x0242dc88`, array `0x0242dc90`, **cap 64**: activate on a full list
fails silently), apply through `0x00862740(&hash, on/off)` and broadcast opcode `0x45` sub-type `0x1e` (`0x0084a7f0`). The
mission path (mission active `0x006cecb0`, mission-started byte `0x014c8326`) compares "current mission name + `_`" as a
**prefix** of the string at `+0x10` of the `0x00e0ceb0` entry; on a match it calls `0x006cfdb0`, which **only appends to a
5-slot queue at `0x014c8548` that nothing reads** (xref and range scan of `0x014c8530`-`0x014c8580`) — HIGH CONFIDENCE
the swap is then never applied. **Correction:** §1.9's "immediate-apply path" for that branch is wrong. Structure notes:
`0x00a43527` dereferences the `0x00e0ceb0` result without a null test; `0x00a434f0`-`0x00a434fa` copy the mission name into
a 64-byte local without a bound (engine data).

### D.3 `player_controls_disable` → `0x00a59630` — §7.22 WRONG on arguments
**Only 1 argument** (unconditional name, resolver `0x00a280c0`); the handler calls `0x009e2dd0(player, 1)` with a
**literal 1** (`PUSH 0x1` at `0x00a59656` — re-read by this note, CONFIRMED). §7.22's "1 boolean, default false" does not
exist: the native always disables. 0 values. `0x009e2dd0`: authority gate `0x008addb0`; non-authority host → opcode-`0x32`
record, single-target; authority → bit `0x08` of `+0x1d99` set; if the handle `0x0263ae3c` is null, allocate one
(`0x005aa780(1)`, an incrementing id), `0x00db8680`, `0x005bbf60`, optional `0x009bc0d0`/`0x009c7cc0`, `+0x20c4` = −1, then
`0x0083bf20(1)` — a disable-depth counter `0x023151ac`. **Corrections:** the counter runs only on a handle transition (not
on every call); the `+0x20c4` store was missing.

### D.4 `character_ragdoll_set_last_resort_position` → `0x00a45f00` — §7.18 OPEN closed
Arg 1 unconditional; resolver `0x005982e0` (kind row `+0xb` bit `0x02`), liveness. `0x009a2110(object + 0x40, 3)`: bit 2 →
opcode-`0x41` record tag `0x22` with the 12-byte position (broadcast); bit 1 → copies the position into the **single global
fallback** `0x0262d0f0`-`0x0262d0f8` and sets `0x0262d0d8` = 1. Consumer: `0x009a3040` returns it (result code 2) while a
mission is active (`0x006cecb0`). Cleared by the sibling at `0x009a2240` (tag `0x23`). 0 values; no crash shape.

### D.5 `mesh_mover_hide` → `0x00a52f90` — §9.16 AGREES with refinements
Arg 1 unconditional; resolver `0x006434a0` (kind row `+0xb` bit `0x01`), liveness. Returns **true only when the setter
ran, false on every failure**. Setter `0x00a32000`: record (opcode `0x43`, byte 7, mover reference, byte 1, then bool 1)
broadcast unless the echo-suppression byte `0x027038c0` is set (only while applying a replicated record — always 0 from
Lua); secondary object via virtual `+0x70`, then `0x008ccb90(secondary, 1, 1, 0)` under its authority gate, `0x00a064d0(0)`
when type row `+6` bit `0x20`; then **unconditionally** mover `+0x82` |= 1.

### D.6 `mesh_mover_show` → `0x00a53440` — §9.17 PARTLY WRONG
Mirror of D.5 (record bool 0; `0x00a064d0(1)`; `+0x82` &= `0xfe`) except the third argument of `0x008ccb90` is
**computed**: 1 when the mover's dword `+0xc4` is 0, else 0 — not the literal 0 §9.17 (and the cross-references citing
"(0, 0)") state. Meaning of `+0xc4` OPEN.

### D.7 `traffic_disable_lanes` → `0x00a61760` — §12.19 imprecise; OPEN items closed
Arg 1 unconditional (trigger, resolver `0x005e4e30` on `0x02442750`); **arg 2 unconditional boolean** (stale when absent).
0 values. `0x00a6dd70(trigger, flag, 1)`: the literal 1 means "send the record" (opcode `0x45` sub-id `0x26`, broadcast);
then locally fetches the trigger's bounds (virtual `+0x58`), runs a spatial query (`0x00a6dd20` → `0x00a6b2e0` or
`0x00a6c9c0`) and **sets/clears bit `0x00800000` of `+0x1c` on every traffic record in the volume**. The trigger itself
is unchanged — §12.19's "enables/disables a trigger volume" is imprecise.

### D.8 `customization_outfit_wear` → `0x00a43b10` — §13.4 PARTLY WRONG
Arg 1 unconditional (name, CRC-32 by `0x00d9e8b0`); arg 2 nil-gated variant (default 0); arg 3 nil-gated flags (default 3).
0 values. Flags bit 0 → `0x00619110(localPlayer, &hash, variant, 0)`; bit 1 → the same on the **remote co-op player**
`0x009df3d0` when present. **Corrections:** `0x00619110` is a plain four-argument function whose first argument is the
character (not a thiscall on an outfit manager), and the targets are the local and remote players (not a context-
resolved character). `0x00619110`: non-authority → opcode-`0x44` sub-id `0xd` record; authority → outfit list
`0x022db9b8` lookup, range-checked variant, `0x00825cf0` apply, `0x00553ea0` appearance refresh. Tail: dirty bits
`0x00a007d0(2, 4, 0, 3)` on `0x0268d2d0`, `0x005ee8e0(0)` on `0x014a0f8c`, `0x0093ca20(8, 1)` (bit 8 of `0x026229a8` plus an
opcode-`0x43` broadcast). **Correction elsewhere:** `0x009df3d0` returns the first runtime-list player that is not the
local player (null unless byte `0x024d4462` is set) — §14.31 item 8's "matching the local-player global" and §14.17's
wording are wrong. Structure note: with no local player yet, the authority gate treats null as local and `0x00619287` reads
`[0 + 0x2088]`; low reachability.

### D.9 `party_dismiss_all` → `0x00a593f0` — §14.17 OPEN closed
No arguments, 0 values. `0x0097e410(localPlayer)`, inert stub `0x00d1e040`, then `0x0097e410(remotePlayer)` when present.
Each roster member (record via owner byte `+0x994`, array `0x013b8780` stride `0xa8`) **whose byte `+0x1e02` has bit `0x08`
clear** is dismissed through `0x009d95a0(member, 0)` (non-authority: opcode-`0x41` sub-id 2 record once, guarded by
`+0x1de8`; authority: local detach and `0x00508e90(member, 1)`); members with the bit set stay.

### D.10 `sidewalk_disable_nodes` → `0x00a5f710` — §15.3 AGREES; OPEN closed
Same argument shape as D.7 (arg 2 unconditional boolean). `0x00bc09f0(trigger, flag, 1)`: opcode-`0x45` sub-id `0x27`
broadcast, then stores the flag into byte `+0xd` of both nodes of every node pair the spatial query `0x00bc0630` (max 200)
returns, in the 24-byte node table reached through `0x02924d40`.

### D.11 `mission_autosave` → `0x00a52b10` — §17.23 polarity WRONG
No arguments, 0 values. Opcode-`0x43` sub-id `0x1c` broadcast, inert stub `0x00d21500`, then **`0x00b94ff0` only when
`0x00b94690` is false** (`0x00b94690` = "busy": (`0x0290ceca` == 1 and `0x0290cecb`) or `0x0290ceda`). `0x00b94ff0` returns
on busy or `0x0290cedc`, returns silently when the Steam check `0x008703b0` is false, else starts the save (`0x00b96230`
with callback `0x00b94d50`) or defers (`0x0290ceda` = 1). **Correction:** §17.23 has the `0x00b94690` polarity inverted
and omits the silent return. It does not use `game_autosave`'s extra mission gates, so it saves mid-mission.

### D.12 `mission_set_next_mission` → `0x00a53b40` — §25.4 refined
Arg 1 unconditional. The gate byte `0x011239d0` is file-backed `.rdata` with value 1 (always true). Activity resolver
`0x005f4c30` (row `+0xc` bit 4), liveness, then `0x006cfd80`: **no session → nothing**; host → stores the activity's
64-bit handle into `0x014c8360`/`0x014c8364` (read back by `0x006cf070`). 0 values. **Correction:** in single player this
native takes its no-session path and changes nothing; `0x011239d0` is a constant, not "mission system active".

---

## E. The OPEN_STATE items

### E.1 `safe-frame source +0x8 ((context +0x674)+0x14)` — partly closed; new crash shape

**What `+0x674` is (CONFIRMED — follow-up dumps, displacement scan).** The per-thread context (TLS slot `0x02cc4700`) has
two graphics fields: `+0x670` (the per-thread UI record of §26.26) and **`+0x674`, the thread's current graphics
device-context object**. Writers: `0x00e38470` (attach: creates the global context `0x02a8a804` once through
`0x004741f0` and stores it), `0x00e384d0` (detach: stores 0), `0x00e38530(flag)` (switches between the global context and
a derived one created by its virtual `+0x88`). The context class (vtable `0x0129fcc4`, base constructor `0x00e486f0`)
**zeroes `+0x14` at construction**; the state-copy routine `0x00e48890` copies `+0x14` from another context.

**Crash shape — the null test in `vint_get_safe_frame` does not guard (CONFIRMED structure).** `0x00e1b570` tests the
context pointer, but on null it substitutes 0 for the `+0x14` object and then reads `[0 + 0x8]` (`0x00e1b598`-
`0x00e1b5a3`; the same pattern three more times). A thread with no attached context, or a context whose `+0x14` is still
0, faults. Reachability: OPEN (depends on whether the UI Lua thread always has an attached context with a set `+0x14`).

**OPEN:** the writer of context `+0x14` (no direct store was found in the context class's modules; HIGH CONFIDENCE it is
set through a method or the copy routine) and so the proof that `+0x8`/`+0xc` are width and height. HYPOTHESIS, for a host:
they equal the display width/height of §26.26 (`0x02a5a180`/`0x02a5a184`) while the main render target is current, giving
the §26.26 worked values (1280 × 720 → 96, 54, 1184, 666).

### E.2 `character ignore-AI flag (+0x2bc)` — closed on the code side

Already CONFIRMED in §34.3: default **off** for every character; on only for a character bound to a script NPC whose
placement record's `script_npc_flags` contains `ignore_ai`. Team B's own source (`lua_engine_state.cpp`
`getOrCreateCharacter`) now applies that default; this note's re-read finds nothing to add. The remaining question —
which placed characters carry the override — is zone data inside the parked `.czn_pc` interior (§34.4) and was **not
opened**.

### E.3 `character max hit points (+0x1cac)` — code-side residuals closed; value stays data- and RNG-dependent

§34.2 gives max HP = round(M × `Hit_Points`) of the selected `spawn_info_ranks.xtbl` rank row. Its two code OPEN items:

- **Level index `0x0096e830` (CONFIRMED — follow-up dump, ptrs run of the jump table).** If the current activity mode
  (`0x006d2910`: `0x014c8460` present with descriptor bit `0x10`, then its virtual `+8`; else −1) is **4**, the level is
  the preset rank row's `rank_numeric` (`+0x8`). Otherwise by character `+0x1c`: value **1** or above 6 → `rank_numeric`;
  values 0 and 2-6 → `rank_numeric` when the rank row's type (`+0x4`) is 2, 6 or 7, else **`0x005f4ad0`: a weighted
  random level** — it picks a table (`0x014a1288` in co-op per `0x00867830`, else `0x014a11a8`), takes the highest stage
  i in 3, 2, 1 whose record (stride `0x38`, name block at `+0xc8` downward) passes `0x006d4dc0` (HIGH CONFIDENCE "story
  stage reached"), draws r in [0, 1) from the PRNG `0x00dab6a0`, and returns the first of 4 levels whose cumulative weight
  (floats at stage `+0x28`) exceeds r (falling back to the last level with positive weight).
- **Multiplier block `0x005f4a50` (CONFIRMED).** Returns entry `0x012ece10` (file-backed, initial **1**) of a 40-byte
  table — `0x014a10b8` in co-op, `0x014a1130` otherwise; HIGH CONFIDENCE a difficulty table (index = difficulty, default 1);
  both tables are runtime-filled (loader OPEN).

So an ordinary non-script-NPC character's max HP is **random per spawn** (weighted by story progress) unless its class
or rank type pins the level; a script-NPC override is parked zone data. Team B's probe placeholder remains the only
option for an unspawned name.

### E.4 `Wwise id for 'SYS_HUD_CNTDWN_POS'` and E.8 `'SYS_HUD_CONF_SERIOUS'` — CLOSED

**Algorithm (CONFIRMED — follow-up dumps of `0x0046fd00`, `0x0046fc50`, the Wwise SDK's own `GetIDFromString`
`0x00f4c350`, `0x00f4c310`, `0x00f4ab90`):** null, empty or `"none"` (case-insensitive) → 0. Otherwise the name is widened
to UTF-16 (`0x0046fc50`, sign-extending each byte), converted back to the ANSI code page (`0x00f4ab90`, HIGH CONFIDENCE the
`WideCharToMultiByte` import by its argument shape), **ASCII `A`-`Z` lower-cased**, then hashed with **32-bit FNV-1**
(start `0x811c9dc5`; per byte: multiply by `0x01000193`, then XOR the byte). **Empirical check:** the same function
reproduces the bank id in the `BKHD` header of **751 of 751** `.bnk_pc` banks in the cache packfiles (id = hash of the
bank's file name) — the algorithm is confirmed against shipped data.

| name | id (hex) | id (decimal, as `game_audio_get_audio_id` pushes it) |
|---|---|---|
| `SYS_HUD_CNTDWN_POS` | `0x23ce84e6` | 600736998 |
| `SYS_HUD_CONF_SERIOUS` | `0x36b3e0d8` | 917758168 |

The id is computed without consulting any bank. Neither id occurs anywhere in the 751 top-level banks scanned (nested
`str2` containers not walked), so posting them may play nothing — the returned id is still the value above.

**Crash shape (CONFIRMED structure):** `0x0046fd00` widens into a 0x100-byte stack buffer (`ESP+4` in a `0x108` frame) but
`0x0046fc50` copies up to 0x100 **characters** (0x200 bytes): a name of 128 or more characters overwrites the stack
cookie (and beyond), so the process aborts in the cookie check on return. Reachable from any Lua audio native given a
long string (scripts do not do this).

### E.5 `key binding for 'CBA_SWC_FIRE_GUIDED'` and E.6 `'CBA_VDC_CRUISE_CONTROL_B'` — CLOSED for default bindings

**Lookup (CONFIRMED — follow-up dumps of `0x005be440`, `0x005be0d0`, `0x005bcc80`, `0x005c1f70`, ptrs runs).**
`0x005be440` first scans the 34 CAA names, then the 166 CBA names (case-insensitive). A CBA match with value v ≠ −1 reads
the 164-entry runtime table at `0x0140ffa4 + 32·v`: `+0` key 1, `+4` key 2, `+8` mouse 1, `+0xc` mouse 2 — and calls
`0x005be0d0(key1, 0, mouse1)`. `0x005be0d0`: **key 1 ≠ 0** → scan code → `GetKeyNameTextW((code ≥ 0x81 ? (code − 0x80) |
0x100 : code) << 16)` (OS text), then a bracket/backslash escape and a formatting pass (`0x0084a030`, `"%ls"`); **key 1 = 0**
→ mouse branch: −1 → localized `"PC_UNBOUND_KEY"`; 0-6 → localized tag from `0x012e6ecc[mouse]` (`"MENU_LEFT_MOUSE"`,
`"MENU_MIDDLE_MOUSE"`, `"MENU_RIGHT_MOUSE"`, `"MENU_MOUSE_BUTTON_4"`, `"MENU_MOUSE_BUTTON_5"`, `"MENU_MOUSE_WHEEL_UP"`,
`"MENU_MOUSE_WHEEL_DOWN"`); above 6 → `"MENU_INVALID_MOUSE"`. (The `_S` table `0x012e6ee8` is used when the second
argument is non-zero; this caller passes 0.)

**Where the table comes from (CONFIRMED).** Start-up `0x005c1f70` (from `0x005d25f0`) loads `control_schemes.xtbl` and
`control_binding_sets.xtbl`, resets all 164 entries (keys 0, mice −1), then applies every `Binding_Set` slot in file order
with `0x005bcc80`: each non-empty `Control` record `{action, key, alt key, mouse, alt mouse}` is stored **only if** both keys
are below `0x100` (so an **absent** key, −1, rejects the whole record) and both mice are −1 or 0-6. This answers
§2.1/§2.2's "which reader builds the 164-slot table" OPEN: `0x005bcc80`, called per button set by `0x005c1f70` (and by
`0x005be990`/`0x005be9f0`). The options-menu rebind path is `0x005c00b0`.

**Default values (data read from `control_binding_sets.xtbl`, the same file `spec-tables-ui-controls.md` §2 describes):**

| action | binding set | record | result of `game_get_key_name_for_action` |
|---|---|---|---|
| `CBA_SWC_FIRE_GUIDED` | `On_Foot_Bindings` | key `UNBOUND` (0), mouse `MOUSE RIGHT` (2), alt key `UNBOUND`, alt mouse `UNBOUND` | key 1 = 0 → mouse 2 → the localized text of tag **`MENU_RIGHT_MOUSE`** |
| `CBA_VDC_CRUISE_CONTROL_B` | `Car_Bindings` | key `LEFT CTRL` (`0x1d`), mouse `UNBOUND`, alt key `UNBOUND`, alt mouse `UNBOUND` | **`GetKeyNameTextW(0x001d0000)`** — the OS name of the left Control key (HIGH CONFIDENCE `"Ctrl"` on a US-English layout; layout- and language-dependent) |

Both actions appear exactly once in the file. Caveats: a loaded profile can replace the table (`spec-save-format.md` §7.6
Table A; HIGH CONFIDENCE, profile path not traced here); the English text of `MENU_RIGHT_MOUSE` lives in localization
data (OPEN). Then `game_get_key_name_for_action` UI-encodes the result as §8.23 says.

### E.7 `0x012f44fc (co-op join type)` — CLOSED

**Initial value 1** — the global is **file-backed `.data` with the static value `0x00000001`** (CONFIRMED — xref run block
header). It has exactly one writer, `0x00703210` (re-read: stores only with no session or as host, then broadcasts only when
a session exists). Its callers: `game_set_coop_join_type` (`0x00844440`: 0→0, 1→1, other→2) and undefined code at
`0x00706369` (range run), which re-applies the value stored at `+0x68` of the profile-options object `*0x015028b4`. The
profile save path `0x00705f30` (which writes the `"DEF_PROFILE"` record) stores the current join type into that same
`+0x68`. So: **1 at start; a saved profile can restore another value** (when `0x00706340`'s block runs is OPEN). Readers:
`0x007029b0` (the getter), `0x00705f30` (profile save), `0x0088ba10` (passes it through `0x007029c0` into the lobby setup).
Meaning of 0/1/2 stays OPEN (`0x007029c0` not dumped).

### E.9 `zscene_prep:OPEN_STATE` — CLOSED (the live blocker is `0x0153b520`, not `0x0153b530`)

`0x0153b530` (current scene) was already closed (null at start; §26.25 globals table, 2026-10-02 changelog). Team B's
latest runs (`verify_clock_final`, `verify_probe2_smoketest`) stop on mission `mm_p_06` with **`zscene_prep: engine state
0x0153b520 (cutscene state) is OPEN`** — the teardown guard of §26.25 reads the cutscene state and the cutscene manager
`*0x0153b528` when a scene is current.

- `0x0153b520` and `0x0153b528` are both **zero-fill `.data` with no static initializer** (CONFIRMED — xref run block
  headers): **cutscene state 0, manager null** at game start.
- The cutscene-system reset `0x007231e0` (caller `0x00707170`) tears down the current scene (`0x00721c20(1, 0, 0)`),
  zeroes `0x0153b530`/`0x0153b51c`, releases the scene table, and **stores 0 into both `0x0153b528` and `0x0153b520`**
  (CONFIRMED — the register is zeroed at entry and only callee-saved calls follow).
- Every other writer is in the cutscene state machine (`0x00725df0`, `0x007258a0`, `0x0072d660`, … — 34 writes, all
  listed by the xref run), i.e. only cutscene start/advance changes it.

So with no cutscene played, the guard is false (state 0 is outside 7..13) and needs no manager value. A host that models
cutscene starts already sets the state; otherwise it is 0.

### E.10 `named-object resolution['Killbane']` — NOT TOUCHED (parked)

Out of scope by the project owner's standing hold: it sits inside the parked `.czn_pc` zone-data interior. Nothing was
opened, read or inferred beyond what `spec-lua-api-behaviour.md` §29.4 already states.

---

## L. Method notes for the follow-up runs

- `fu1`: `0x004590d0 0x004591f0 0x007073e0 0x00710b80 0x008318f0 0x00831f60 0x00831f70 0x00681370 0x008b6dc0 0x00834070
  0x0088b610 0x00877d60 0x00867830 0x00837c70 0x00dfe670 0x00dfdeb0 0x009daeb0 0x00e1b570 0x00e236f0 0x00e237e0 0x00462960
  0x0046fd00 0x005be440 0x005be0d0 0x005be2c0 0x005be340 0x008422a0 0x007029b0 0x00703210 0x00844440 0x0096e830 0x005f4a50
  0x005f4ad0 0x006d2910`. `fu2`: `0x0046fc50 0x00f4c350 0x0087ba20 0x0080cc90 0x00720640 0x006d7240 0x006d6f30 0x006e44f0
  0x007231e0 0x00e38470 0x00e384d0 0x00e38530 0x005c1f70 0x005c00b0 0x005bcc80 0x005bfbb0 0x005bc710 0x005be2a0 0x00705f30
  0x0088ba10 0x006d4dc0`. `fu3` (depth 1): `0x00f4c310 0x00f4ab90`. `fu4` (depth 1): `0x004741f0 0x005de200 0x005de230`.
  `fu5`: `0x00e486f0`. `fu6`: `0x00e48890 0x00e49640 0x00e49870 0x00e49910 0x00e49a60`. `fu7`: `0x00e48220 0x00e48240
  0x00483b20`. `0x005be2c0`, `0x005be340`, `0x008422a0`, `0x0080cc90`, `0x005bfbb0`, `0x00e49640`-`0x00e49a60` were read only
  for their role.
- Range run: `0x0088cd40-0x0088cdc0`, `0x008b6a80-0x008b6b00`, `0x008b6ec0-0x008b6fc0`, `0x008b7840-0x008b7870`,
  `0x008ba090-0x008ba140`, `0x00703000-0x00703060`, `0x00706340-0x00706380`.
- Data: `control_binding_sets.xtbl` from an earlier session's scratchpad extraction of `misc_tables.vpp_pc`; the bank-id
  check walked every `.bnk_pc` of the cache packfiles with the existing harness `tools\harnesses\extract_xtbl.py`'s
  container reader (no project file changed).
- Handlers were taken from `{name, function}` pairs (insn offset +1); none needed the pointer-before-name correction.

---

## M. Cross-function observations

1. **`n(generic-stub)` ≠ unspecced.** 21 of 27 names were already specified; the blocker for Team B is implementation.
   Of those 21, 11 entries carried at least one correction (C.1 changelog, C.2, C.4, C.5, C.8, D.2, D.3, D.6, D.7, D.8, D.11)
   and several OPEN items closed (D.1, D.4, D.9, D.10, D.12).
2. **Two "no network object ⇒ wrong default" shapes in one family.** `customization_create_character` reads through a null
   session (B.3) and `customization_creation_is_open` treats a missing network object as "still open" (B.4) — both in the
   co-op character-creation helpers around `0x008b6xxx`/`0x008b7xxx`, whose siblings (`0x00867830`, the second use inside
   `0x008b71a0`) do test for null.
3. **Unguarded "guards".** `vint_get_safe_frame` substitutes 0 for a null context and then dereferences it (E.1) — the
   same "test, then read through the substitute" shape tranche 16 recorded for `skydive_move_to_check_done`.
4. **Fixed-size stack buffers fed by script strings.** The Wwise resolver (E.4, 128+ characters), the data-responder
   request (C.8, 17+ arguments, 64+ character callback name) and `city_zone_swap`'s mission-name copy (D.2) all trust
   their input length.
5. **Ignored or constant arguments.** `mip_streaming_pause` ignores its reason (B.1); `player_controls_disable` has no
   enable argument (D.3); `object_spawn_pause` is the no-op stub (B.2).
6. **Missing-argument exposure (unconditional reads):** `mip_streaming_pause` args 1-2, `player_parachute_has_backpack` arg
   1, `traffic_disable_lanes`/`sidewalk_disable_nodes` arg 2 (a boolean decides set vs clear), plus the arg-1 reads of
   every name-taking native in C/D (`vint_set_property`'s arg 2 included). None feeds a crash in this set.
7. **No session claims.** Every session/host-gated path here takes its no-session branch in single player per
   §8.27/§26.28; the only consequence drawn is where that branch leads (B.3 crash, B.4 constant, D.12 no-op).

---

## N. OPEN

- B.1: the per-frame consumer of mip-streamer byte `+0xfe2`; the states of its entries.
- B.3: whether the shipped script calls `customization_create_character` in single player; `0x0080cc90`'s effect; the
  readers of `0x02300849`.
- B.4: whether any script polls `customization_creation_is_open` in single player; writers of store byte `+0x1e`.
- C: clone slot `+0x14` and null parents; `+0x318` writers; setter/getter bodies; whether setters keep string pointers;
  the native data-responder handlers and `0x00e1e510`.
- D: `0x00862740` (zone-swap apply), the reader (if any) of the `0x014c8548` queue; mover `+0xc4`; the vtable `+0x58`
  bounds target and the two traffic query tables; `0x00825cf0`; `0x0097e7b0`; `0x00b96230`/`0x00b94d50`; the consumers of
  `0x014c8360`/`0x014c8364`; `0x009bc0d0`/`0x009c7cc0`.
- E.1: the writer of graphics context `+0x14`; width/height identity of `+0x8`/`+0xc`; whether the UI thread always has a
  context attached.
- E.3: the loaders of the stage/level tables `0x014a11a8`/`0x014a1288` and difficulty tables `0x014a10b8`/`0x014a1130`;
  `0x006d4dc0`'s exact test; zone data (parked).
- E.5: the English text of `MENU_RIGHT_MOUSE`; the profile-load override of the binding table; `0x0084a030`.
- E.7: when the profile re-apply block at `0x00706340` runs; the meaning of join types 0/1/2.
- E.10: parked by standing hold — not an open item of this note.

---

## O. Direct-answer table

| # | name | handler | status |
|---|---|---|---|
| 1 | `vint_set_property` | `0x00e1d1f0` | resolved — §20.1 corrected (tween redirect, defaults, extra int) |
| 2 | `audio_object_post_event` | `0x00a3cb00` | resolved — §2.4 confirmed; mismatch returns 2 values; arg-3-keyed list copy |
| 3 | `vint_get_property` | `0x00e1b4d0` | resolved — §20.2 confirmed |
| 4 | `vint_get_time_index` | `0x00e1b9b0` | resolved — §19.1 confirmed |
| 5 | `vint_dataresponder_finished` | `0x00e19e50` | resolved — §21.1 confirmed; true for unknown names (host loop hazard) |
| 6 | `vint_internal_dataresponder_request` | `0x00e1af70` | resolved — §21.2 corrected; **two overflow shapes** |
| 7 | `vint_dataitem_get` | `0x00e1aeb0` | resolved — §19.2 corrected (multi-value fields) |
| 8 | `vint_object_first_child` | `0x00e1b7a0` | resolved — §18.1 confirmed |
| 9 | `vint_object_clone` | `0x00e1aa90` | resolved — §18.2 corrected (source vtable, argument order) |
| 10 | `pause_map_stag_current_district_control` | `0x007de0d0` | resolved — §31.2 confirmed |
| 11 | `player_controls_disable` | `0x00a59630` | resolved — §7.22 corrected: no boolean, always disables |
| 12 | `traffic_disable_lanes` | `0x00a61760` | resolved — flags traffic records in the trigger volume |
| 13 | `city_zone_swap` | `0x00a43460` | resolved — §1.9 corrected (mission-prefix branch only queues) |
| 14 | `party_dismiss_all` | `0x00a593f0` | resolved — skip bit `+0x1e02` `0x08` |
| 15 | `mip_streaming_pause` | `0x00a52360` | **new** — pause bit 0 of the mip streamer; reason ignored |
| 16 | `object_spawn_pause` | `0x007c9f50` | **new** — shared no-op stub |
| 17 | `character_ragdoll_set_last_resort_position` | `0x00a45f00` | resolved — single global fallback position |
| 18 | `customization_create_character` | `0x00a42db0` | **new** — replays the co-op creation flow; **null-session crash** |
| 19 | `customization_creation_is_open` | `0x00a42dd0` | **new** — **constant true in single player** |
| 20 | `customization_outfit_wear` | `0x00a43b10` | resolved — §13.4 corrected (local/remote player targets) |
| 21 | `customization_screen_is_ready` | `0x00a42e40` | **new** — truthiness of UI global `Store_lineup_is_loaded` |
| 22 | `mesh_mover_hide` | `0x00a52f90` | resolved — true/false by resolution |
| 23 | `mesh_mover_show` | `0x00a53440` | resolved — §9.17 corrected (computed third argument) |
| 24 | `mission_autosave` | `0x00a52b10` | resolved — §17.23 polarity corrected |
| 25 | `mission_set_next_mission` | `0x00a53b40` | resolved — no-op in single player |
| 26 | `player_parachute_has_backpack` | `0x00a59840` | **new** — reads the byte `player_parachute_wear_backpack` writes |
| 27 | `sidewalk_disable_nodes` | `0x00a5f710` | resolved — per-node flag in the volume |
| E.1 | safe-frame source | — | partly closed; **new crash shape**; `+0x14` writer OPEN |
| E.2 | ignore-AI `+0x2bc` | — | closed on the code side (default off); zone override parked |
| E.3 | max HP `+0x1cac` | — | code residuals closed; value random per spawn / data-dependent |
| E.4 | Wwise `SYS_HUD_CNTDWN_POS` | — | **closed: 600736998** |
| E.5 | key `CBA_SWC_FIRE_GUIDED` | — | **closed: localized `MENU_RIGHT_MOUSE`** (default bindings) |
| E.6 | key `CBA_VDC_CRUISE_CONTROL_B` | — | **closed: OS key name of scan code `0x1d`** (default bindings) |
| E.7 | `0x012f44fc` | — | **closed: 1 at start** (profile can restore another value) |
| E.8 | Wwise `SYS_HUD_CONF_SERIOUS` | — | **closed: 917758168** |
| E.9 | `zscene_prep` (now `0x0153b520`) | — | **closed: state 0, manager null at start** |
| E.10 | `Killbane` | — | not touched (parked) |

---

## P. Crash-shaped and logic defects (summary)

| # | where | defect | status |
|---|---|---|---|
| 1 | B.3 `0x008b71a7` → `0x00681370` | `customization_create_character` reads session `+0x60` with no session → `[0 + 0x60]` | CONFIRMED structure; any single-player call (§8.27/§26.28) |
| 2 | E.1 `0x00e1b598`-`0x00e1b5a3` (×4) | `vint_get_safe_frame` null "guard" substitutes 0 then reads `[0 + 8]`; also faults when context `+0x14` is 0 | CONFIRMED structure, reachability OPEN |
| 3 | E.4 `0x0046fd00`/`0x0046fc50` | Wwise name of 128+ characters overruns a 0x100-byte stack buffer → cookie abort | CONFIRMED structure |
| 4 | C.8 `0x00e1b0a7`-`0x00e1b14b` | 17+ trailing arguments to `vint_internal_dataresponder_request` overwrite the return address | CONFIRMED structure |
| 5 | C.8 `0x00e1e683`-`0x00e1e68b` | callback name of 64+ characters overwrites the handler pointer called next | CONFIRMED structure |
| 6 | D.2 `0x00a43527` | `city_zone_swap` dereferences the `0x00e0ceb0` result without a null test | CONFIRMED structure, reachability OPEN |
| 7 | D.8 `0x00619287` | outfit apply before the local player exists reads `[0 + 0x2088]` | CONFIRMED structure, low reachability |
| 8 | C.9 `0x007de10d`-`0x007de137` | world singleton and table entries used unchecked | CONFIRMED structure, low reachability |
| 9 | B.4 | `customization_creation_is_open` always true in single player | CONFIRMED structure |
| 10 | B.1 | `mip_streaming_pause` ignores its reason; all scripts share one pause bit | CONFIRMED |
| 11 | D.2 | `city_zone_swap` mission-prefix branch queues into a never-read queue | CONFIRMED structure, effect HIGH CONFIDENCE |
| 12 | D.1 `0x00a3ce41`-`0x00a3ce62` | `audio_object_post_event` drops arg-2 ids when arg-3 count is 0; unfilled id slots are stale stack | CONFIRMED structure |
| 13 | E.5 `0x005bcc80` | a binding record with an absent key (−1) is rejected whole, including its mouse binding | CONFIRMED structure |
| 14 | C.4 `0x00e1af23`-`0x00e1af39` | unclamped field count in the push loop | CONFIRMED structure, HYPOTHESIS unreachable |
| 15 | M.6 | unconditional argument reads across the set read stale stack content when absent | CONFIRMED (tranche 22 M.1 semantics) |

---

## Q. Clean-room check

- Addresses are plain hex throughout; no Ghidra auto-names for functions, globals or labels, no decompiler variable
  names, and no pasted pseudocode. Library routines are named only where they are the public Lua 5.1 API by project
  convention, standard C runtime, Win32 (`GetKeyNameTextW`, `WideCharToMultiByte`) or the Wwise SDK's own exported
  `GetIDFromString`; single machine instructions are quoted only to pin a value (`PUSH 0x1`). Game strings (registered
  names, localization tags, binding-file values, `"Store_lineup_is_loaded"`, `"_DynamicGlobals"`, `"mm_m1_5"`) are quoted as
  data. The FNV-1 constants are quoted as the values read from the listing.
- Self-check run on the finished file, as the last step before reporting, with the pattern
  `\b(iVar[0-9]+|uVar[0-9]+|piVar[0-9]+|puVar[0-9]+|pvVar[0-9]+|fVar[0-9]+|sVar[0-9]+|cVar[0-9]+|bVar[0-9]+|pcVar[0-9]+|pbVar[0-9]+|local_[0-9a-fA-F]+|uStack_?[0-9a-fA-F]+|auStack_?[0-9a-fA-F]+|unaff_[A-Za-z0-9]+|extraout_[A-Za-z0-9]+|undefined[0-9]+|param_[0-9]+|in_[A-Z]{2,4}|LAB_[0-9a-fA-F]+|FUN_[0-9a-fA-F]+|DAT_[0-9a-fA-F]+)\b`
  (Python `re`, line by line, over the whole file including this section): **0 hits.** (Substituted directly — the
  agent that produced this note was killed by a session rate-limit one step before running this check itself and left
  a literal placeholder here; `grep -noP` was run on the finished file as the literal last step before folding it in,
  confirming 0 matches.)
- No spec file was edited. The private Ghidra copy `tools\gp_stubhits` and the three verification passes' copies were
  deleted after the dumps; raw dumps stayed in the session scratchpad.
