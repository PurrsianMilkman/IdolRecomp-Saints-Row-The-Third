# Ranking tranche 04 — 25 previously-unspecced Lua-bound names (exe-derived 2026-10-02)

Investigation notes only; nothing here has been transcribed into `spec-lua-api-behaviour.md`. Each entry is drafted in that file's per-function shape so it can be transcribed later.

**Source.** Job definition `D:\Crreish-sync\for-team-a\team-a\ghidra\jobs\ranking\tranche-04.json` (Team B call-count order, names 26-50 of the 554 unspecced). Dump run locally, read-only, with `CrreishDump.java` (`lua depth:1 maxfuncs:15 maxinsn:500`, all 25 names in one call) against a job-private copy of the shared project (`tools\gp_t04`, deleted afterwards). All 25 names resolved, each to exactly one registration.

**Registration.** Every one of the 25 names is stored by the gameplay registrar 0x00a20840 as a (name string, function pointer) pair in consecutive stack slots — name at `[ESP+N]`, function at `[ESP+N+4]` — so every address below is CONFIRMED — disassembly (the store instruction pair is cited per entry as "pair at 0xAAAAAAAA/0xBBBBBBBB").

**Shared conventions (cited, not re-described per entry).** Lua primitives as already pinned in the spec: `lua_gettop` 0x00dfde50, `lua_type` 0x00dfe040, `lua_tonumber` 0x00dfe160 (non-number and non-convertible → 0.0), `lua_toboolean` 0x00dfe1e0 (false only for nil and `false`), `lua_tolstring` 0x00dfe210, `lua_pushnil` 0x00dfe380, `lua_pushnumber` 0x00dfe3a0, `lua_pushstring` 0x00dfe420, `lua_pushboolean` 0x00dfe590; 0x00ea2596 is the float→int64 truncation helper (§4.1). Arguments are read by the usual negative-index idiom (`-gettop` = argument 1, `-gettop+1` = argument 2, …). "Returns 0" below means the C function returns 0 (no Lua values); "returns 1" means exactly one value was pushed.

---

## A. Bare global setters / queries (no object argument)

### A.1 `store_interface_is_active` (0x00a5def0) — pair at 0x00a2500a/0x00a25015

**Arguments:** none read (the argument count is fetched but unused; any arguments are ignored).

**Return:** 1 boolean, always.

**Body:** pushes bit 0x1 of the byte at global 0x022cce86 as a boolean and returns 1. The byte is runtime-only (`.data`, not file-backed). The image has 7 direct references to it and all 7 are reads (0x00525e5a, 0x00584b8c, 0x00586fc8, 0x00587260, 0x008ff915, 0x009df163, and this function) — no direct writer, so it is written through a pointer or as a field of a larger structure (not traced). **[CONFIRMED — disassembly for the body and the reader census; OPEN — who sets the byte; HYPOTHESIS — "store" is the in-game shop/store UI (name only).]**

**Side effects/subsystem:** none (pure query of one global flag bit).

### A.2 `spawn_region_max_spawn_dist` (0x00a5dbf0) — pair at 0x00a24ec0/0x00a24ecb

**Arguments:** 1 number (arg 1), no nil-gate; a value Lua cannot convert reads as 0.0.

**Return:** none (returns 0).

**Body:** reads arg 1 with `lua_tonumber`, narrows it to single precision and calls 0x00931640(value), which squares it (in double precision, result narrowed to float) and stores the square in global 0x013092f0 (file-backed initial value 0x7f7fffff = FLT_MAX). The only reader of that global is 0x00931980 (two reads, 0x00931bc4 and 0x00931d1a, each widening it to double for a comparison), so the value is a squared-distance cap compared against squared distances. **[CONFIRMED — disassembly for the setter and the global census; HIGH CONFIDENCE that it is a squared maximum spawn distance (the squaring plus the name); OPEN — 0x00931980's body (what is spawned and how the cap is applied).]**

**Side effects/subsystem:** sets one global spawn-distance limit. No range check: a negative argument squares to a positive cap; 0 makes the cap 0.

### A.3 `spawn_region_max_spawn_dist_reset` (0x00a5dc20) — pair at 0x00a24ed6/0x00a24ee1

**Arguments:** none read.

**Return:** none (returns 0).

**Body:** calls 0x00931660(), which copies FLT_MAX (from the `.rdata` constant 0x012a2ea0) back into 0x013092f0 — the global's own static initial value, so "reset" means "no limit". The image has only two writers of 0x013092f0: A.2's 0x00931640 and this 0x00931660. **[CONFIRMED — disassembly.]**

**Side effects/subsystem:** restores the A.2 cap to its unlimited default.

### A.4 `set_ped_override_density` (0x00a5d1f0) — pair at 0x00a24b0e/0x00a24b19

**Arguments:** 1 number (arg 1), no nil-gate (non-convertible → 0.0).

**Return:** none (returns 0).

**Body:** reads arg 1 with `lua_tonumber`, narrowed to float, then branches on the sign:
- value > 0 → 0x009108b0(value), which stores it in global 0x01308af0 **only if value ≤ 1.0**; a value above 1.0 is silently ignored and the previous setting stays.
- value ≤ 0 → 0x009108d0(), which stores −1.0 (constant 0x012a2d54) in 0x01308af0.

0x01308af0's file-backed initial value is also −1.0 (0xbf800000), so −1.0 is the "no override" state and any value ≤ 0 clears the override. The global has exactly three references: the two writers above and one reader, 0x009106c0 (at 0x0091079d). A NaN argument takes the first branch and is then rejected by the ≤ 1.0 test (unordered compare), so it is a no-op. **[CONFIRMED — disassembly, including the compare directions; HIGH CONFIDENCE that the value is a 0..1 ambient-pedestrian density fraction overriding the normal density (name plus range); OPEN — how 0x009106c0 uses it.]**

**Side effects/subsystem:** ambient population density override (one global).

### A.5 `pause_map_tutorial_mode` (0x00a594e0) — pair at 0x00a240ea/0x00a240f5

**Arguments:** 1 boolean (arg 1), read with `lua_toboolean`, no nil-gate — absent or nil means false.

**Return:** none (returns 0).

**Body:** true → 0x007d9ab0() writes 1 to the byte 0x0229a318; false → 0x007d9ac0() writes 0 to it. The byte is runtime-only. It is also written by 0x007dab20 (at 0x007dab2c, from a register) and read by 0x007d9280, 0x007d98e0, 0x007d9a70 (a getter), 0x007db5b0, 0x007dea30, 0x007df420 and 0x007df8a0 — all in the 0x007d9xxx-0x007dfxxx range, i.e. one UI module. **[CONFIRMED — disassembly for the setter and the census; HIGH CONFIDENCE that the module is the pause-menu map screen (name plus clustering); OPEN — what the readers change in tutorial mode.]**

**Side effects/subsystem:** pause-menu map UI flag.

### A.6 `set_time_of_day` (0x00a5e370) — pair at 0x00a24c9a/0x00a24ca5

**Arguments:** 2 numbers, no nil-gates: arg 1 hours, arg 2 minutes. Each is read with `lua_tonumber` and truncated toward zero by the §4.1 helper 0x00ea2596. A missing argument reads as 0.

**Return:** none (returns 0).

**Body:** the function never writes the clock directly — it computes how far to **advance** it:
1. Reads the current hour from byte 0x014ff33c and the current minute from byte 0x014ff33d (two bytes inside the game-clock object based at 0x014ff338; both runtime-only). The hour reading is supported by other readers comparing 0x014ff33c with 5 and 19 (0x0072cb75, 0x00a00e00, 0x00a037fa, 0x00a05041).
2. Hour delta = requested hour − current hour; minute delta = requested minute − current minute. If the hour delta is negative, or it is zero and the minute delta is negative, 24 is added to the hour delta — the target is always reached by going forward (0 up to just under 24 hours). Asking for the current time gives a delta of 0.
3. Seconds = 60 × (minute delta + 60 × hour delta), converted to float, passed to 0x006fe240(seconds, 1, 0, 1).
4. Then calls 0x005a2440().

No range check: an hour of 30 or a minute of 90 simply produces a larger delta (more than 24 h is possible that way).

0x006fe240(amount, flagA, flagB, flagC) as called here (1, 0, 1): flagA set → it skips adding the amount to the accumulator 0x014ff348 and skips 0x006fe0b0; flagC set → it skips the "time scale (0x014ff34c) approximately zero → amount becomes 0" test (0x00dad830, tolerance 0x3a83126f ≈ 0.001), so the amount is used at scale 1.0. A positive amount goes to 0x006ff1f0 (this = 0x014ff338), a non-positive one to 0x006ff360 with the amount negated. flagB clear → every function pointer in the list at 0x014ff330 (count at 0x014ff354) is then called with the amount, and the function tail-calls 0x006ff670.

0x005a2440(): unless byte 0x014032e6 or 0x014032e7 is set, it queries 0x005a1d10 on the clock (five outputs), picks one of two of those outputs depending on whether a third, fraction-like output is ≥ 0.5, and passes it to 0x005a22d0.

**[CONFIRMED — disassembly for the delta arithmetic, the forward-wrap rule, the argument order and the 0x006fe240 flag handling. HIGH CONFIDENCE — 0x014ff33c/0x014ff33d are hour/minute, and 0x006ff1f0/0x006ff360 advance/rewind the clock. HYPOTHESIS — the 0x014ff330 list holds time-change listeners, and 0x005a2440 re-selects the nearest time-of-day lighting/weather key. OPEN — 0x006ff1f0, 0x006ff360, 0x006ff670, 0x005a1d10, 0x005a22d0, and the two suppress bytes 0x014032e6/0x014032e7.]**

**Side effects/subsystem:** game clock / time of day. No network record is opened at depth 1.

### A.7 `satellite_weapon_mode_exit` (0x00a5df20) — pair at 0x00a248bc/0x00a248c7

**Arguments:** 1 optional number (arg 1), nil-gated (`lua_type`), default 3, truncated to an integer (0x00ea2596). It is a player-selector bitmask, not the usual local/replicate mask: bit 0x1 = the local player, bit 0x2 = the remote co-op player.

**Return:** none (returns 0).

**Body:**
- bit 0x1 set → 0x00b709d0(local player), the local player coming from 0x009da4e0 (= global 0x0262edfc).
- bit 0x2 set **and** 0x009df3d0() non-null → 0x00b709d0(that player). 0x009df3d0 returns 0 unless the byte 0x024d4462 is set; otherwise it walks the player list on the object at 0x03171a64 (count `+0x1f8`, 16-bit indices at `+0x1f0` into the pointer array at `+0x58`) and returns the first entry that is **not** the local player 0x0262edfc, or 0. This identifies the "second one-liner, not independently identified" of the spec's §8.17: it returns the other (remote) co-op player while co-op is active. It also **contradicts §14.31's wording** ("scans … for an entry matching the primary singleton"): the loop at 0x009df408-0x009df425 returns the first entry that does **not** equal 0x0262edfc (`CMP EAX,EDI` / `JNZ` to the return at 0x009df429) and returns 0 only when every entry is the local player. **[CONFIRMED — disassembly for the walk; HIGH CONFIDENCE that 0x024d4462 means "co-op active".]**

0x00b709d0 is a thiscall on the singleton 0x0130eee8 (HIGH CONFIDENCE: the satellite-weapon controller) taking the player (callee pops 4 bytes). Null player → nothing. It then applies the single-gate authority test 0x008addb0(player, 0):
- gate false → opens a record, opcode 0x41, writes an 8-bit sub-tag 0x38 and one zero byte, and commits it with 0x0086f110 addressed to 0x008ae020(player) — the request is forwarded to the machine that owns that player; nothing changes locally.
- gate true and the controller's active byte `+0xb0` set → local teardown: sets `+0x2c`, `+0x20`, `+0x4c` to −1; calls 0x00d9fc60 on `+0x34` and 0x00b6f6b0 on the controller; restores global 0x013c87c4 from the controller's saved `+0xa4`; resets `+0x18`/`+0x1c` from the constants 0x01187fb8/0x01187fbc; releases the HUD-slot handles at `+0xa8` and `+0xac` through 0x007efbd0(handle, 1) (the shared HUD-display-state slot release §15.6 `qte_cleanup` documents) and sets both to −1; calls 0x007ff270(2, 0), 0x00564d70(), 0x0056cc30(), 0x005dcd70(1.0); clears `+0xb0`; calls 0x00b6ff90(player), 0x00905f60(7), 0x00905fc0(7) (slot 7 of the same two slot arrays §6.13/§6.15 drive through 0x00905f60(1)/0x00905fc0(1)), 0x0058ebc0(), 0x0059eef0(4), and three times the no-op logging stub 0x00754410; finally, if 0x009a1dc0(player) is true, starts a sound on the player through 0x0094c9e0 → 0x0045f5b0/0x0045ea70 (the voice-associate/start pair §2.2 documents) with hash constants 0x50d893ff/0xd6adcb25 and 0x0d61bb32.
- gate true but `+0xb0` clear → nothing (exiting when not in the mode is a no-op).

**[CONFIRMED — disassembly for the selector bits, the default, the gate/record split and the teardown call list. OPEN — the meanings of 0x007ff270, 0x00564d70, 0x0056cc30, 0x005dcd70(1.0), 0x00b6ff90, 0x0058ebc0 and 0x0059eef0(4) (§15.5 describes 0x0059eef0 as zeroing slot 0 of the refraction-situation override array 0x013ef578; that it takes the slot as its argument, so that 4 here names another slot, is HYPOTHESIS — its body was not dumped) beyond their call shapes, and the controller's field meanings.]**

**Side effects/subsystem:** satellite (orbital) weapon targeting mode — leaves the mode for one or both players, restoring camera/HUD/audio state; co-op aware (the remote player's exit is sent as a message).

---

## B. Character force-flag setters (record-and-replicate idiom, §4.13)

All three take (character name, optional boolean) and share one shape:

**Arguments:** arg 1 string (character name; `lua_tolstring`, no nil-gate — a nil name simply fails to resolve). Arg 2 optional boolean, nil-gated (`lua_type`), **default true** when absent or nil.

**Return:** none (returns 0) on every path.

**Setter shape (0x009486f0, 0x00948f90, 0x004e2950 — CONFIRMED — disassembly, all three read):** thiscall, one byte argument, callee pops 4. Null receiver → nothing. If 0x008ae480(id low, id high, 0) or 0x008837a0() reports that the call is already inside a replicated-apply context, the bit is written directly (an XOR-mask conditional set: set to exactly the boolean, not toggled). Otherwise 0x008ae3a0(id pair) yields the target; if that is null **nothing happens at all**; else a record is opened (opcode 0x46), two literal debug-tag strings are attached through 0x00d9e7e0 → 0x004def50, then the id pair (0x0086f4b0) and the boolean byte (0x004d46e0) are written, and the record is committed with 0x0086f110(target, 0, 0, 0) and closed with 0x0086eb20 — the bit is **not** changed locally in that call. This is the idiom §3.7/§13.5 already document; these are three more members of the same force-flag setter family. Each tag string has exactly two users in the image — the setter and a network apply dispatcher (0x008ae650 for the `"human"` flags, 0x008b14c0 for the `"human_ai_data"` flag) — which is how the receiving side applies the change.

### B.1 `set_seatbelt_flag` (0x00a5d5d0) — pair at 0x00a24c16/0x00a24c21

**Body:** arg 1 resolves through 0x00a281a0 (generic name / `#PLAYER#` chain, falling back to 0x00a28150 — §1's name-resolution sentinels), then 0x00948f90(boolean) on the character. Direct path writes **bit 0x40 of the dword at character `+0x1c9c`**; record tags `"human"` / `"human_force_flagsalways_uses_seatbelt"` (0x01167f64; other user 0x008ae650 at 0x008aee7b, which also calls the setter at 0x008aeea1). **[CONFIRMED — disassembly. HYPOTHESIS — "always uses seatbelt" stops the character being thrown from a vehicle in crashes (tag string only).]**

### B.2 `set_trailing_aim_flag` (0x00a5d820) — pair at 0x00a24cc6/0x00a24cd1

**Body:** arg 1 resolves through 0x00a28150 only (`#FOLLOWER#` sentinels, then the character resolver 0x005e4dd0 on 0x02442750, liveness 0x00853b10, vtable `+0x70` cast) — **no `#PLAYER#` step**, unlike B.1, so the literal `"#PLAYER#"` does not resolve here (a by-name lookup of the player's own registered name still can). Then 0x009486f0(boolean). Direct path writes **bit 0x1 of the dword at character `+0x1c9c`**; record tags `"human"` / `"human_force_flagstrailing_aim"` (0x0116804c; other user 0x008ae650 at 0x008aed31, which calls the setter at 0x008aed57). **[CONFIRMED — disassembly; OPEN — what "trailing aim" changes in the aim/animation code.]**

### B.3 `set_never_turn_on_player` (0x00a5d140) — pair at 0x00a24ae2/0x00a24aed

**Body:** arg 1 resolves through 0x00a28150 (same as B.2, no `#PLAYER#`). The setter 0x004e2950 is called with **this = character + 0x2b0** (the per-character AI-data sub-object the `"human_ai_data"` setters use, §3.4) and reads its id pair from that sub-object's own `+0x610`/`+0x614` (character `+0x8c0`/`+0x8c4`) rather than `+8`/`+0xc`. Direct path writes **bit 0x40 of the byte at sub-object `+0x9`, i.e. character byte `+0x2b9`**; record tags `"human_ai_data"` / `"ai_force_flagsnever_turn_on_player"` (0x01116230; other user 0x008b14c0 at 0x008b189f). 0x004e2950 has five further callers (0x00652c30, 0x0066acb0, 0x00672bc0, 0x005380f0, 0x008b14c0), so the engine sets this flag itself too. **[CONFIRMED — disassembly. HYPOTHESIS — the flag stops a friendly NPC turning hostile to the player (name only).]**

**Side effects/subsystem (B.1-B.3):** per-character behaviour flags, co-op/network-replicated.

---

## C. Player-state functions

### C.1 `player_revive` (0x00a599b0) — pair at 0x00a244f4/0x00a244ff

**Arguments:** 1 string (player name; `"#PLAYER#"` accepted), no nil-gate.

**Return:** none (returns 0) on every path.

**Body:** arg 1 resolves through 0x00a280c0(name, 0) (per-kind by-name lookup 0x00a27e20 with liveness, then the `"#PLAYER#"` → local player fallback, §1/§28.1). Unresolved → nothing. Then 0x009715d0(character) — "state field `+0xcc8` == 6", the downed test §7.13 `human_is_downed` documents — and **only a downed character is revived**; any other state (alive, dead = 5) → nothing. On a downed character: 0x009a7c90(character, 0, 1, 0).

0x009a7c90(character, value, relocate, fromNetwork) — 23 callers (`value` is 0 here; it is only carried through to 0x009a78d0 in step 4); read at depth 1, summarized:
1. fromNetwork == 0 → broadcast first: record opcode 0x41, 8-bit sub-tag 4, the character's compressed 16-bit network id (0x008add70 then 0x00bc5610, the compression pair §13.20/§13.23 document), the `relocate` byte, one zero byte; committed with 0x0086f1b0 addressed to 0x0087ba20(0, 0). Always sent, before any local work.
2. Local cleanup on every machine: releases the handle at `+0x1900` (0x007fe050) and zeroes it; if bit 0x10 of byte `+0x1905` is set, calls 0x008f7870(16-bit `+0x18fc`, 1) and clears the bit; calls 0x0101b530(); releases `+0x15f8` through 0x007ff1f0(handle, 1) and zeroes it.
3. Picks a reference player — the local player, unless 0x009a1dc0(local player) is true and a remote co-op player exists (0x009df3d0, A.7) for which 0x009a1dc0 is false, in which case the remote player — and calls that player's vtable `+0x64` with two output buffers.
4. Authority gate 0x008addb0(character, 0):
   - not authoritative → clears bit 0x1 of `+0xe8`, then 0x0096e6e0(character, 0), 0x00704610(character), 0x0094f450(character, 1, 0).
   - authoritative → 0x00973700(character); if `relocate` (true from this Lua function), runs a spatial query (0x0075a480 with literals 0xf and 0x31) around the character's position `+0x40` and a path/placement query (0x0075c880) seeded 1.75 × 5.0 units from it, and if a spot is found moves the character there through its own vtable `+0x44`(position, 1); finally 0x0070dcf0(character, 0x612ab47e, 0) and 0x009a78d0(character, value, 1, 0, 1). The two outputs of step 3 feed the spatial-query block only.

**[CONFIRMED — disassembly for the Lua wrapper, the downed gate, the argument literals (0, 1, 0), the record layout and the authority split. HIGH CONFIDENCE — `relocate` moves the revived player to a nearby valid spot (the two spatial queries plus the vtable `+0x44` position write). HYPOTHESIS — 0x009a1dc0 is "is downed/incapacitated", so a revive is anchored on a standing co-op partner when there is one; 0x0070dcf0 with hash 0x612ab47e plays a get-up animation/event. OPEN — the meaning of the released handles `+0x1900`/`+0x15f8`/`+0x18fc`, and 0x00973700, 0x009a78d0, 0x0096e6e0, 0x00704610, 0x0094f450.]**

**Side effects/subsystem:** character state machine (downed → revived), co-op replicated (opcode 0x41 sub-tag 4).

### C.2 `player_warp_to_shore_disable` (0x00a59a90) — pair at 0x00a24536/0x00a24541

**Arguments:** 1 string (player name; `"#PLAYER#"` accepted), no nil-gate.

**Return:** none (returns 0).

**Body:** arg 1 → 0x00a280c0(name, 0); the result is passed **without a null check** as `this` to 0x009e0840(1) (literal true); 0x009e0840 itself returns at once on a null receiver, so an unresolved name is a harmless no-op. 0x009e0840 is the §B setter shape with record tags `"player"` / `"sync_flagswarp_to_shore_disabled"` (0x01169310; two users: this setter and 0x008b0f80 at 0x008b11c4, which also calls the setter at 0x008b11ec — the network apply side); the direct path writes **bit 0x2 of the byte at player `+0x28ad`**. The function registered immediately after it, 0x00a59ac0 (registrar slot `+0x1660`), also calls 0x009e0840 (at 0x00a59ae5) — HIGH CONFIDENCE it is the enable counterpart passing false (its name string was not read in this job). **[CONFIRMED — disassembly. HYPOTHESIS — the bit stops the engine teleporting a player who swims too far out back to shore.]** No kind check beyond 0x00a280c0's own: a non-player character name would have its `+0x28ad` byte written (OPEN whether that offset is valid on non-player objects).

**Side effects/subsystem:** per-player sync flag, co-op replicated.

### C.3 `skydive_setup_tank_bailout` (0x00a5e510) — pair at 0x00a24e10/0x00a24e1b

**Arguments:** 1 number (stage), no nil-gate, truncated to an integer (0x00ea2596).

**Return:** none (returns 0).

**Body:** a mission-specific hard-coded helper. Calls 0x009b0300(stage, 0); 0x009b0300's only other caller (0x009b072e, outside any defined function) is presumably the network apply path, passing a non-zero second argument.
1. Second argument 0 → broadcast first: record opcode 0x41, 8-bit sub-tag 0x1c, a 4-byte field holding the literal 4, and the low byte of `stage`; committed with 0x0086f1b0 addressed to 0x0087ba20(0, 0). Sent whatever the stage value and whether or not the vehicles below exist.
2. Looks up two vehicles by **literal name** through the vehicle resolver 0x0062a190 on 0x02442750 (liveness 0x00853b10, vtable `+0x70` instance cast): `"veh_stag_cargo_plane 001"` (0x01174d08) and `"veh_player_tank 001"` (0x01174cf4) — each string used nowhere else in the image. Either missing → stop.
3. stage 1 → 0x008538d0(plane, 1), then on the plane 0x00a7d350(1) and 0x00a7dbf0(1), and on the tank 0x00a7dbf0(1).
4. stage 2 → snapshots the plane's `+0x58`/`+0x5c`/`+0x60` and passes it to 0x00ac0c90(plane, &vector, 0); builds an orientation from the tank's `+0x64`/`+0x68`/`+0x6c` (0x00da0730, 0x00da57b0) and applies it through the tank's vtable `+0x48`(matrix, 1); calls 0x00abadc0(tank, 1) and 0x00abadc0(plane, 1); looks up the name `"m18 plane tank collide"` (0x01174cdc) through 0x004bf810 (the animation-state name → id table, §7.11) and starts it on both vehicles through 0x00a75f00(vehicle, id, 0x1d4c, 0x200000, 1.0) — the same "play by literal id" vehicle-animation dispatcher, with the same two literals, that §15.2 `vehicle_anim_start` documents.
5. Any other stage → nothing locally (the record is still sent).

**[CONFIRMED — disassembly. HIGH CONFIDENCE — this scripts the cargo-plane/tank bail-out set piece of mission 18 (the literal names). OPEN — 0x008538d0, 0x00a7d350, 0x00a7dbf0, 0x00ac0c90, 0x00abadc0; the role of the literal 4 in the record.]**

**Side effects/subsystem:** mission set-piece (vehicle animation/physics), co-op replicated.

### C.4 `qte_human_is_used` (0x00a5b180) — pair at 0x00a245ba/0x00a245c5

**Arguments:** 1 string (character name), no nil-gate.

**Return:** 1 boolean, always (false when the name does not resolve).

**Body:** arg 1 → 0x00a281a0 (generic / `#PLAYER#` chain). Resolved → 0x0060b530(character), which scans the two 0x70-byte QTE slot records at 0x014b01c0 and 0x014b0230. A slot counts only when its dword `+0` is non-zero and its dword `+4` is not −1 (active). For an active slot it derives the slot's owning player — the local player (0x009da4e0) when the slot is the one indexed by byte `+0x158` of the session object's `+0x5c` (session from 0x0087ba10), otherwise the remote co-op player (0x009df3d0, A.7) — and returns true if the character **is** that player, or if the character's id pair (`+8`/`+0xc`) equals the slot's id pair at `+0x28`/`+0x2c` or any of the four further id pairs at `+0x30`..`+0x48`. No match in either slot → false. 0x0060b530 has at least thirty callers (the dump's caller list is capped at 30). **[CONFIRMED — disassembly for the slot walk and the comparisons. HIGH CONFIDENCE — one slot per co-op player and the five id pairs are the QTE's participants. OPEN — the slot fields' other meanings; whether these are the same records §15.6 `qte_cleanup` tears down.]**

**Side effects/subsystem:** none (pure query) — "is this character taking part in an active QTE".

---

## D. NPC / party (follower) functions

Descriptor bits below use the §28.20 encoding: 0x00853b30(obj, n) tests bit (n & 7) of byte `+6 + (n >> 3)` of the object's `0x02cc9900` kind row, so n = 0x21 is bit 0x2@`+0xa` and n = 0x22 is bit 0x4@`+0xa`.

### D.1 `party_add_do` (0x00a5af70) — pair at 0x00a23fcc/0x00a23fd7

**Arguments:** (the positions are computed from the top of the stack, so they hold for 3 or 4 arguments)
- arg 1 string — leader name, no nil-gate.
- arg 2 table — follower names. Its length comes from the table-walk helper 0x0083dff0 (§2.4): `t.n` if that is a number (converted by 0x00ea2560), otherwise a `pairs` count; −1 when arg 2 is nil (then no follower is processed). Elements `t[1]..t[count]` are read with `lua_gettable` and `lua_tolstring`.
- arg 3 boolean, read with `lua_toboolean`, **no nil-gate** (nil → false).
- arg 4 optional boolean, nil-gated (`lua_type`), default false.

**Return:** none (returns 0) on every path.

**Body:**
1. Leader = 0x00a280c0(arg 1, 0) (per-kind lookup, then `"#PLAYER#"` → local player).
2. Each follower name resolves through 0x00a28150 (`#FOLLOWER#` sentinels, character resolver, liveness, `+0x70` cast) into a **five-slot local array** (0x14 bytes ending immediately below the return address). **The loop has no bound check against that array**: a table reporting 6 or more entries (a real 6-element table, or a `t.n` ≥ 6) writes resolved pointers over the saved return address and beyond — a stack overwrite that would crash on return. Scripts must pass at most 5 followers. **[CONFIRMED — disassembly: `SUB ESP,0x2c`, four register pushes, array base `[ESP+0x28]` = 0x14 below the entry stack pointer, index advanced with no compare other than against the table count.]**
3. For each non-null follower: if the leader failed to resolve, it is re-resolved **per follower** with 0x00a3bd20(arg 1, follower `+0x40`), which tries the per-kind lookup 0x00a27e20 again and otherwise, for `"#CLOSEST_PLAYER#"` (prefix `"#CLOSEST_"` via `_strncmp`, then `"PLAYER#"` at 0x0117f7f0) **or** `"#PLAYER#"`, returns the player from the 0x03171a64 player list nearest to that follower's position (0x00da1330 distance; FLT_MAX start). So `"#CLOSEST_PLAYER#"` as leader attaches each follower to whichever co-op player is nearest to it. (`"#PLAYER#"` never reaches this path, step 1 already resolved it.)
4. 0x0097e940(follower, leader) — "is the follower's current party leader exactly this leader": the follower's party record comes from 0x005083f0, its leader id pair at record `+8` is resolved with 0x004d7f30, a self-reference counts as none; with no party, true only when leader is null; a follower with descriptor bit 0x22 **and** bit 0x1 of byte `+0x1e01` is treated as party-less. Already in this party → skip.
5. Otherwise 0x0097ead0(leader, follower, **not** arg 3, 0, arg 4):
   - returns at once for a null leader/follower or leader == follower; calls 0x004e2dc0(1) (receiver not identified); returns 1 if step 4's test now passes;
   - admission gate: refuses (returns 0) only when 0x0097e780(leader) ≤ 0x0097e260(leader) **and** arg 4 is false **and** arg 3 is true **and** 0x0097e670(leader, follower) is false — with arg 3 false the gate is skipped entirely;
   - joins through 0x00508ef0(leader, follower, flag), flag = leader has descriptor bit 0x22 or 0x008add90(leader);
   - if the two characters' `+0x994` bytes differ, returns 0 here (after the join);
   - network ownership: if the leader is not authoritative (0x008addb0(leader, 1) false) but the follower is, the follower is handed to the leader's machine (0x008addf0(follower, 0x008ae020(leader))); if the leader is authoritative and the follower is not, 0x008addd0(follower, 0, 0);
   - if the leader has descriptor bit 0x21: follower set-up — snapshots follower fields (`+0x980` ← `+0xf8`, `+0x984` ← `+0x1cac`, `+0x988` ← `+0x1cc8`, `+0x98c` ← 0x0094cdf0(), `+0x990` ← `+0x1ce0`), 0x0097e480(follower), three typed setters with 2, 0x1a and 0x2c (0x0094adb0, 0x0094af10, 0x0094b750), registers the follower under the literal `"human_follow"` (0x01173118) through 0x00bbb4a0(follower `+0x1dc0`, …, 5), clears its action state through 0x004e4130(follower, 0) when its byte `+0x501` is 13, links it through 0x00653b00(follower, leader) and 0x00971cf0 → 0x009dd6d0, and — when the leader is the local player and bit 0x10 of follower byte `+0x1da5` is clear — calls 0x00bda610(follower).

**[CONFIRMED — disassembly for the argument positions, the five-slot overflow, the per-follower `#CLOSEST_PLAYER#` fallback, the "not arg 3" inversion and the 0x0097ead0 control flow. HYPOTHESIS — 0x0097e780/0x0097e260 are the leader's party capacity/size (so arg 4 overrides a full party and arg 3 = "don't force"); `+0x994` is a team byte. OPEN — what descriptor bit 0x21 means: D.2 answers false for it and only a bit-0x21 leader gets the `"human_follow"` set-up, which suggests "player", but F.1's 0x008ccb90 also uses it to exempt an object from replication, which does not fit a plain "player" reading. OPEN — 0x00508ef0, 0x0097e670, 0x004e2dc0, 0x00bda610 and the snapshot fields' meanings.]**

**Side effects/subsystem:** party/homie membership; co-op ownership hand-off; follower AI set-up.

### D.2 `npc_is_in_party` (0x00a562a0) — pair at 0x00a237ea/0x00a237f5

**Arguments:** 1 string (character name; `#FOLLOWER#`/`#FOLLOWER1#`-`#FOLLOWER3#` accepted), no nil-gate.

**Return:** 1 boolean, always.

**Body:** inlines 0x00a28150's chain — 0x00a26010 maps `#FOLLOWER1#` and `#FOLLOWER#` to 1, `#FOLLOWER2#` to 2, `#FOLLOWER3#` to 3 (else 0), resolved through 0x00a26110; otherwise character resolver 0x005e4dd0 on 0x02442750, liveness 0x00853b10, vtable `+0x70`. Then 0x0097e280(character): null → false; descriptor bit 0x21 set → false; else the result of 0x005088c0(character) (not dumped; a sibling of D.1's party-record getter 0x005083f0), non-zero → true. **[CONFIRMED — disassembly. OPEN — 0x005088c0's body; whether "in party" means "has a party leader" or "is in the local player's party" (HYPOTHESIS: the former).]**

**Side effects/subsystem:** none (pure query).

### D.3 `npc_go_idle` (0x00a554b0) — pair at 0x00a237a8/0x00a237b3

**Arguments:** 1 string (character name), no nil-gate.

**Return:** none (returns 0).

**Body:** arg 1 → 0x00a28150 (no `#PLAYER#`). Unresolved → nothing. Otherwise:
1. 0x004fc4a0 with **this = character + 0x510** — a reset of that sub-object: clears handles at `+0x210`, `+0x218`, `+0x21c`, `+0x220`, `+0x228`, `+0x230`, `+0x234`, `+0x23c` (0x004fa820/0x00d9e140 with 0), zeroes `+0x248`..`+0x254`, clears bits 0x21 of `+0x25f`, resets `+0x270` (0x004f69f0), clears `+0x260`, resets 22 eight-byte slots at `+0x2f0` and the one at `+0x3a0` (0x004f8c50), zeroes the float `+0x200`, sets `+0x208` to −1, clears `+0x25c`..`+0x25e`, and recomputes `+0x204` = (owner's B.3 never-turn-on-player bit 0x40@`+0x2b9` clear **and** 0x009b7100(owner byte `+0x1ca8`, 0) ≥ 3).
2. Sets the character's action state with 0x004e4130: **0x19 when the character's `+0x16d4` == 3** (0x009b9160, the in-vehicle state of §4.5/§18.31), **0 otherwise** — exactly the rule §3.4 `set_ignore_ai_flag` applies. 0x004e4130(character, state): rejects states ≥ 0x2c and states 0x004de110 refuses; also refuses while bit 0x8 of `+0x317` is set with current state `+0x504` == 0x22; state 0 additionally clears bit 0x10 of `+0x4a6` and zeroes the id pair `+0x458`/`+0x45c`; then 0x004e3830 on `+0x2b0` with a zero id pair, sets bit 0x8 of `+0x317`, clears bits 0x3 of `+0x44c`, stores the state byte at `+0x504`, and calls 0x008addb0(character, 0) (result unused).

**[CONFIRMED — disassembly. HIGH CONFIDENCE — the `+0x510` sub-object holds the current AI orders/targets and step 1 drops them; `+0x504` is the AI action-state id (0 = none, 0x19 = in-vehicle idle). OPEN — 0x004de110's refusal rule and the meaning of state 0x22.]**

**Side effects/subsystem:** character AI — cancels current orders and returns the NPC to idle (in a vehicle: the vehicle idle state).

---

## E. Named objects, markers and shops

The by-name resolvers here are all methods on the singleton 0x02442750 with one shape (CONFIRMED — disassembly, all four dumped): require the registry count `+0x265c` > 0, look the name up with 0x004588f0 in the registry at `+0x2660`, reject objects with bit 0x10 of `+0x33`, then require one kind-descriptor bit — 0x00734e90: bit 0x2@`+6` (§10.5's resolver); 0x00a3de90: bit 0x8@`+0xa`; 0x00729080: bit 0x80@`+0xa`; 0x005982e0: bit 0x2@`+0xb`. Each entry then applies the liveness test 0x00853b10 (true = dead/invalid: null, bit 0x4 of `+0x33`, or kind byte `+0x34` == 0xff).

### E.1 `object_destroy` (0x00a57630) — pair at 0x00a239a7/0x00a239b2

**Arguments:** 1 string (object name); a nil/non-string argument → nothing.

**Return:** none (returns 0).

**Body:** 0x00a3de90(name) → liveness → the object's kind must have bit 0xc@`+0xb` (either bit) **or** bit 0x80@`+0xa` (the item kind, see F) → vtable `+0x68` predicate must be true (the same capability test the vehicle resolver applies before its `+0x70` cast, §28.1) → vtable `+0x58` called with no arguments. No record is opened in this function; any replication would be inside the virtual. **[CONFIRMED — disassembly. HIGH CONFIDENCE — vtable `+0x58` is the object's destroy/remove method (the function's name; it is the only effect). OPEN — the virtual's body per kind, and the kinds behind the descriptor bits.]**

**Side effects/subsystem:** removes a placed object from the world.

### E.2 `object_indicator_remove_do` (0x00a59190) — pair at 0x00a239e9/0x00a239f4

**Arguments:** arg 1 string (object name). Arg 2 optional number, nil-gated, default 3, truncated (0x00ea2596) — the §1.1/§15.5 local/replicate mask (bit 0x1 = apply locally, bit 0x2 = send a record).

**Return:** none (returns 0).

**Body:** 0x00734e90(name) → liveness → 0x008f7a00(id low, id high, mask) (the indicator-remove helper; at least 30 callers). Inside: if 0x008ae410(id pair) is false **and** the low 16 bits of the id's high dword are 0, the mask is forced to 1 (local only). Bit 0x2 → record opcode 0x40, 8-bit sub-tag 4; the object is written either as a 16-bit network id from 0x008ae2b0 (preceded by a 0 byte) or, when that id is 0, as a 1 byte followed by the full id pair (0x0086f500); committed with 0x0086f1b0 to 0x0087ba20(0, 0). Bit 0x1 → repeatedly asks 0x008f4ef0(id pair) for an entry and hands each one found to 0x008f4f30, until 0x008f4ef0 returns 0 — **raw disassembly: the found entry is passed in EDI (`MOV EDI,EAX` just before the call); the decompile shows 0x008f4f30 with no input at all** — so every indicator attached to the object is removed, not just one. **[CONFIRMED — disassembly, including the EDI hand-off. OPEN — 0x008f4ef0/0x008f4f30's bodies (an indicator list keyed by the id pair, HIGH CONFIDENCE).]**

**Side effects/subsystem:** HUD world-space object indicators (the markers drawn over objects), co-op replicated.

### E.3 `minimap_icon_remove_do` (0x00a54430) — pair at 0x00a23296/0x00a232a1

**Arguments:** arg 1 string (object name). Arg 2 optional number, nil-gated, default 3, truncated — the local/replicate mask.

**Return:** none (returns 0).

**Body:** 0x00734e90(name) → liveness, then two routes by kind:
- kind with descriptor bit 0x2@`+7` → 0x0093d3a0 with **this = the object** and (−1, −1, −1, 0.0, mask): the object's own icon fields are set to "none". 0x0093d3a0 compares the new values with `+0xcc` (HYPOTHESIS: the icon id), `+0xc0`, `+0xc4` and the float `+0xd0`; bit 0x2 of the mask, when the object is authoritative (0x008addb0(obj, 0)), passes 0x008add90 and 0x00889d10() is false, sends record opcode 0x40 sub-tag 9 (a byte that is 1 unless the low 16 bits of the id high dword are 0, the compressed 16-bit id 0x008add70/0x00bc5610, then 8 bits of the icon id and the three 4-byte values), committed through 0x0086f1b0 with the extra pair (callback 0x00884650, object) instead of the usual zeros; bit 0x1 and something changed → releases the old handle `+0xc8` through 0x00806e60(handle, old icon id), stores the new values, and, when bit 0x1 of `+0xe4` is set, calls 0x0093b7a0(mask).
- any other kind → **an indirect call through the 4-entry function table 0x0130cb98 indexed by the mask**, with the object's id pair. The table's static (file-backed) contents, as an earlier stopped agent dumped them (`tools/scratchpad/luaB551B_tables.txt`): [0] = 0 (null), [1] = 0x008070b0, [2] = 0x008070d0, [3] = 0x00807140 (this job's dump confirms entry [3] and entry [0] directly). 0x00807140 = 0x00806f70(id pair) then 0x00806e60(0x008052f0(id pair, 5)) — release of the id's category-5 icon handle, i.e. [3] = [1]'s local removal + [2]'s replicated removal (HIGH CONFIDENCE from the 1/2/3 mask convention; [1]/[2] not dumped here).

**Hazard (CONFIRMED — disassembly):** the table index is the mask with **no range check**. For a non-bit-0x2@`+7` object, an explicit mask of 0 calls a null pointer, and a mask ≥ 4 calls whatever follows the table (entry [4] holds 0xfffffffe) — both crash. Only masks 1-3 are safe on that path.

**[CONFIRMED — disassembly for the routing, the table call and the 0x0093d3a0 field handling; table entries [1]/[2] HIGH CONFIDENCE (static data from an earlier dump, not re-read in this job). OPEN — what descriptor bit 0x2@`+7` marks (objects that carry their own icon fields), and 0x008070b0/0x008070d0/0x00806f70/0x008052f0's bodies.]**

**Side effects/subsystem:** minimap icons, co-op replicated per mask.

### E.4 `shop_enable_nearest` (0x00a5f650) — pair at 0x00a24d60/0x00a24d6b

**Arguments:** arg 1 optional string (location object name), nil-gated: absent or nil → the empty string `""` (0x0129a0e3); a present non-nil value that is not string-convertible → return at once. Arg 2 optional boolean, nil-gated, default true (enable).

**Return:** none (returns 0) on every path.

**Body:**
1. 0x005982e0(name) → liveness. With the empty-string default the registry lookup of `""` will normally fail, so the no-argument form does nothing (HYPOTHESIS that no object is registered under `""`).
2. 0x00a01200(object `+0x40` position) picks the shop: it walks the second typed index list of the global object table 0x03171a64 (count `+0x180`, 16-bit indices at `+0x178` into the shared pointer array `+0x58` — the same array A.7's player list indexes through `+0x1f0`), considers only entries with bit 0x10 of byte `+0x90`, and **returns the first entry within squared distance 225.0 (15 units) at once**, otherwise the closest entry within squared distance 5625.0 (75 units, constant 0x01115360), otherwise 0. So with two shops inside 15 units the first in list order wins, not the nearest.
3. Found → 0x00a04d90(this = shop, enable): if a session exists (0x0087ba20) **and this machine is the host** (session `+0x5c` == `+0x58`, the host test §2.10 documents), sends record opcode 0x45 sub-tag 7 with the shop's id (0x00a017c0), an 8-bit 0 and the enable byte, committed with 0x0086f1b0; then, on every machine, sets bit 0x20 of shop byte `+0x92` to **not** enable (a "disabled" bit); if that leaves it disabled and 0x00d9e4c0 on `+0x9c` is true, calls 0x00a041a0(1) on `+0x98`.

**[CONFIRMED — disassembly, including the 15/75-unit thresholds and the first-within-15 early return. HIGH CONFIDENCE — the bit-0x10@`+0x90` entries are shops and 0x00a041a0(1) closes a shop that is open/in use when disabled. OPEN — 0x00a017c0, 0x00d9e4c0, 0x00a041a0.]**

**Side effects/subsystem:** shop availability; host-replicated.

---

## F. Items

### F.1 `item_show` (0x00a51d50) — pair at 0x00a23018/0x00a23023

**Arguments:** 1 string (item name); nil → nothing.

**Return:** none (returns 0).

**Body:** 0x00729080(name) (item kind, bit 0x80@`+0xa`) → liveness → vtable `+0x68` true → vtable `+0x70` instance cast → 0x008ccb90(instance, 0, 1, 0). **Decompiler artefact (CONFIRMED — disassembly):** the three literals 0, 1, 0 are pushed before the `+0x70` virtual call but belong to 0x008ccb90 (the caller pops 0x10 bytes after it); the decompile wrongly shows them as arguments of the virtual and shows 0x008ccb90 with one argument.

0x008ccb90(object, hidden, extra, fromNetwork) — the item hide/show setter: if the object lacks descriptor bit 0x21, is not authoritative (0x008addb0(obj, 0) false) and fromNetwork is 0 → record opcode 0x45 sub-tag 0x13 (written by 0x00898cb0), the object id (0x0050f790), the hidden byte and the extra byte, committed with 0x0086f110 addressed to the owner 0x008ae020(object); nothing changes locally. Otherwise, if bit 0x1 of byte `+0x3b` differs from `hidden`: sets bit 0x2 of the word `+0x74`, writes `hidden` into bit 0x1 of `+0x3b`, calls 0x008cc4e0(object), and — unless bit 0x1 of `+0x3a` is set — calls the object's vtable `+0x38`(not hidden, extra). Already in the requested state → nothing.

**[CONFIRMED — disassembly. HIGH CONFIDENCE — bit 0x1 of `+0x3b` is the hidden flag and vtable `+0x38` applies visibility; the function registered just before it, 0x00a51ce0 (registrar slot `+0xea0`), is very likely `item_hide` (name not read). OPEN — the meaning of `extra` (1 here).]**

**Side effects/subsystem:** item visibility, co-op replicated.

### F.2 `item_anim_play` (0x00a51ad0) — pair at 0x00a22fec/0x00a22ff7

**Arguments:** arg 1 string (item name); arg 2 string (animation name), no nil-gate (nil → id −1); arg 3 optional boolean, nil-gated, default false; arg 4 optional string, nil-gated, default none.

**Return:** none (returns 0) on every path.

**Body:** item resolves as in F.1 (0x00729080, liveness, `+0x68`, `+0x70`). Args 2 and 4 are each mapped to an id by 0x004bf810 — the animation-state name table (16-byte entries at 0x03171c10, count 0x03171c08; hash then case-insensitive `_stricmp` compare; −1 if absent), §7.11. Nothing happens if the instance's `+0xe8` is null; the animation controller is `[+0xe8] + 0x10`. Then:
- arg 4 resolved → 0x004b21a0(controller, arg-4 id, −1, arg-2 id or −1, −1, 0.3 (0x3e99999a), 0) — a two-state set/transition with a 0.3 blend; arg 3 is ignored on this path.
- arg 4 absent/unresolved, arg 2 resolved → 0x004b1b90(controller, arg-2 id, −1, flags, 1.0, 1.0, −1.0) with flags **0x40 when arg 3 is true, 0x10 otherwise**.
- neither resolved → nothing.

Both helpers first recurse into the controller's child list (`+8`) and map the id to a slot through 0x004b1630; 0x004b1b90 then claims a free or matching layer slot (0x84-byte slots at `+0x4c`, count `+0x48`) and starts the animation (0x004bce30). **[CONFIRMED — disassembly for the argument order, the branch rule and every literal. HYPOTHESIS — flag 0x40 = hold/loop versus 0x10 = play once; arg 4 = a target state blended to. OPEN — 0x004b2280 and the slot-flag meanings.]**

**Side effects/subsystem:** item (prop) animation; no record at depth 1 (local only).

---

## G. Vehicle-targeted

### G.1 `radio_set_station` (0x00a5c1c0) — pair at 0x00a24704/0x00a2470f

**Arguments:** arg 1 string (vehicle name, or a character name — the vehicle resolver 0x00a281e0 first tries 0x00a280c0 and uses that character's cached vehicle id pair `+0x16c0`/`+0x16c4`, §28.1); arg 2 number (station), no nil-gate, truncated and narrowed to a signed byte.

**Return:** none (returns 0).

**Body:** unresolved → nothing. Otherwise two independent calls:
1. 0x005602e0(vehicle id low, id high, station) — local apply. Station index = station − 1; **station ≤ 0 or > the station count (global 0x013c58dc) → rejected**, so stations are 1-based and this function cannot switch a radio off. The vehicle is re-resolved by id (0x004dcf00); a vehicle whose `(+0xbf4)+0x870` dword lacks bit 0x4000000 is accepted but left unchanged (HYPOTHESIS: "has a radio"). Station records are 0x690 bytes at the global base 0x013c58cc; a station with bit 0x80 of byte `+0x689` is refused. If the vehicle's current station (0x0055ec10) is already the target → done. Otherwise 0x0055ed20 attaches the vehicle to the new station (failure → stop), the station byte is stored at vehicle `+0x1630`, the old station is detached (0x0055efa0), and when the local player's cached vehicle pair matches this vehicle (0x00456920 id-pair compare) the player-facing radio state is refreshed (0x00558a30, 0x0055e110, 0x00bda1c0).
2. 0x00a5bfe0(2, station) — **always sent when the vehicle resolved, even if step 1 rejected the station**: record opcode 0x45, 8-bit sub-tag 0x17, 8-bit value 2, the vehicle's compressed 16-bit id (0x008add70/0x00bc5610; 0 if none) and the station byte, committed with 0x0086f1b0 to 0x0087ba20(0, 0). **The vehicle reaches 0x00a5bfe0 in register ESI, not on the stack** (a non-standard register argument; the decompile shows it only as an unassigned register) — CONFIRMED — disassembly (the caller's ESI still holds the resolved vehicle at the call, and 0x00a5bfe0 tests ESI before compressing it).

**[CONFIRMED — disassembly for the argument handling, the 1-based range check, the always-sent record and the ESI hand-off. HIGH CONFIDENCE — `+0x1630` is the vehicle's current station. OPEN — 0x0055ec10/0x0055ed20/0x0055efa0 internals, the 2 in the record, and 0x00a5bfe0's other caller 0x00a5c170 (registered just before, name not read).]**

**Side effects/subsystem:** vehicle radio; co-op replicated.

### G.2 `helicopter_shoot_vehicle` (0x00a4d790) — pair at 0x00a228f6/0x00a22901

The vehicle-target sibling of §14.11 `helicopter_shoot_navpoint` and §14.18 `helicopter_shoot_human`, through the same fire dispatcher 0x00b37250.

**Arguments:** arg 1 string (helicopter; vehicle resolver 0x00a281e0). Arg 2 string (target vehicle; **also 0x00a281e0**, so a character name targets that character's vehicle). Arg 3 optional boolean, nil-gated, default true (apply spread). Arg 4 optional number, nil-gated, default 1.0 (0x01117a4c) (spread radius). Arg 5 optional boolean, nil-gated, default true (passed to the dispatcher).

**Return:** 1 boolean, always: false if either name fails to resolve or the helicopter fails 0x00ad31a0 (AI drive state 3 or 4 at `(+0xbf4)+0x2c`, §9.4/§28.2); otherwise the dispatcher's result.

**Body:** aim point = target `+0x40`, `+0x44` + 1.0 (double constant 0x012a2d70), `+0x48`. If arg 3: adds arg 4 × 0x00da0e90's vector. 0x00da0e90 fills a 3-float buffer with z drawn uniformly in [−1, 1] (0x00dab6a0(−1, 1)), an angle draw (0x00dab5e0), and x, y = cos/sin of it × √(1 − z²) — **a uniformly distributed point on the unit sphere** (the standard z-then-angle construction), which answers §14.18's OPEN "exact distribution" item (HIGH CONFIDENCE: 0x00ea3a70/0x00ea2470 read as cos/sin from their use). Like §14.11 and unlike §14.18, this function **does** test 0x00ad31a0 before firing. Then 0x00b37250(heli, x, y, z, arg 5).

**0x00b37250 (the shared dispatcher), as read in this job:** returns false at once if 0x00b73870(heli) is false. Not authoritative (0x008addb0(heli, 0) false) → record opcode 0x42, 8-bit sub-tag 0x14, 8-bit 3, the heli id (0x0050f790), the 0x004d2d40 field taken from the address of x, and the flag byte, committed with 0x0086f110 to the owner 0x008ae020(heli); returns **true** (sent, not fired). Authoritative → returns false if seat 0 is empty (0x00ab5070(heli, 0)); otherwise for each seat 0-7 with an occupant: the occupant's current weapon from 0x005ff220 on its inventory `+0x1b78`(1, 0, 1, 1, 0); if the weapon's info record (pointer at weapon `+0x19c`) has `+0x4c` == 0x16 (or the pointer is null — see below), and 0x00600720(inventory, info-record byte `+0x54`, 1, 0, 0) accepts it, the occupant is ordered to fire: bits 0x28 set in `+0x13c5`, target stored at `+0x1374`/`+0x1378`/`+0x137c`, bit 0x2 of `+0x13c5` = flag, bit 0x1 set. Returns true if at least one occupant was ordered.

Three oddities, CONFIRMED in the code (raw disassembly; reachability OPEN):
- **The dispatcher's return value is invisible in the decompile** (it renders 0x00b37250 as returning nothing); the AL/BL values above are read from the listing (0x00b372a3, 0x00b373b1, 0x00b37479).
- **Null read:** when the weapon's `+0x19c` pointer is 0 the branch at 0x00b37407 jumps straight to 0x00b3740f, which reads the byte at `[0 + 0x54]` — an access violation if that case ever occurs.
- **The record may drop z:** only the 0x004d2d40 field (an 8-byte writer per §14.31) and the flag follow the id; if that writer is really 8 bytes, the remote side receives x, y only. OPEN — 0x004d2d40's real width.

**[CONFIRMED — disassembly for the arguments, defaults, the +1.0 height, the drive-state gate, the dispatcher's control flow and return values. HIGH CONFIDENCE — the uniform-sphere reading of 0x00da0e90. OPEN — 0x00b73870, 0x005ff220/0x00600720 semantics, weapon class 0x16.]**

**Side effects/subsystem:** AI helicopter gunner targeting, co-op forwarded to the owner.

---

## H. Cross-function observations

1. **Three crash-capable paths reachable from script arguments (CONFIRMED in the code):** `party_add_do` with more than 5 followers overruns a 5-slot stack array into the return address (D.1); `minimap_icon_remove_do` with an explicit mask of 0 (or ≥ 4) on an ordinary object calls through a null/garbage table entry (E.3); and the shared helicopter-fire dispatcher reads `[0 + 0x54]` for an occupant weapon whose `+0x19c` info pointer is null (G.2 — not script-controlled, reachability OPEN). A reimplementation should bound-check the first two rather than reproduce them.
2. **Decompiler drops found in this batch** (all settled from the listing): a register-passed object (ESI into 0x00a5bfe0, G.1; EDI into 0x008f4f30, E.2), stack literals attributed to the wrong call (F.1), and a return value rendered as `void` (0x00b37250, G.2).
3. **0x009df3d0 is the remote co-op player getter** (A.7), not a "matching" lookup; its users here are A.7, C.1 and C.4.
4. **0x03171a64 is a typed object-list table**: one shared pointer array at `+0x58`, indexed through per-type 16-bit index lists — players through `+0x1f0` (count `+0x1f8`), shops through `+0x178` (count `+0x180`) (A.7, D.1, E.4).
5. **Record census additions:** opcode 0x40 sub-tags 4 (indicator remove) and 9 (object minimap icon); 0x41 sub-tags 4 (revive), 0x1c (tank bail-out), 0x38 (satellite-mode exit); 0x42 sub-tag 0x14 (helicopter fire); 0x45 sub-tags 7 (shop enable, host only), 0x13 (item hide/show), 0x17 (radio station); 0x46 flag records with tags `"human"`/`"human_force_flagsalways_uses_seatbelt"`, `"human"`/`"human_force_flagstrailing_aim"`, `"human_ai_data"`/`"ai_force_flagsnever_turn_on_player"`, `"player"`/`"sync_flagswarp_to_shore_disabled"`.
6. **Flag bits added to the character map:** `+0x1c9c` bit 0x1 = trailing aim, bit 0x40 = always uses seatbelt (B.1/B.2; §13.5 already has bit 0x4 there); byte `+0x2b9` bit 0x40 = never turn on player (B.3, also read by D.3's reset); player byte `+0x28ad` bit 0x2 = warp-to-shore disabled (C.2; §14.31 has bits 0 and 3 of the same byte).

---

## Direct answer — all 25 names

| # | Name | Address | Result |
|---|---|---|---|
| 1 | `store_interface_is_active` | 0x00a5def0 | resolved, written up (A.1) |
| 2 | `spawn_region_max_spawn_dist_reset` | 0x00a5dc20 | resolved, written up (A.3) |
| 3 | `spawn_region_max_spawn_dist` | 0x00a5dbf0 | resolved, written up (A.2) |
| 4 | `skydive_setup_tank_bailout` | 0x00a5e510 | resolved, written up (C.3) |
| 5 | `shop_enable_nearest` | 0x00a5f650 | resolved, written up (E.4) |
| 6 | `set_trailing_aim_flag` | 0x00a5d820 | resolved, written up (B.2) |
| 7 | `set_time_of_day` | 0x00a5e370 | resolved, written up (A.6) |
| 8 | `set_seatbelt_flag` | 0x00a5d5d0 | resolved, written up (B.1) |
| 9 | `set_ped_override_density` | 0x00a5d1f0 | resolved, written up (A.4) |
| 10 | `set_never_turn_on_player` | 0x00a5d140 | resolved, written up (B.3) |
| 11 | `satellite_weapon_mode_exit` | 0x00a5df20 | resolved, written up (A.7) |
| 12 | `radio_set_station` | 0x00a5c1c0 | resolved, written up (G.1) |
| 13 | `qte_human_is_used` | 0x00a5b180 | resolved, written up (C.4) |
| 14 | `player_warp_to_shore_disable` | 0x00a59a90 | resolved, written up (C.2) |
| 15 | `player_revive` | 0x00a599b0 | resolved, written up (C.1) |
| 16 | `pause_map_tutorial_mode` | 0x00a594e0 | resolved, written up (A.5) |
| 17 | `party_add_do` | 0x00a5af70 | resolved, written up (D.1) |
| 18 | `object_indicator_remove_do` | 0x00a59190 | resolved, written up (E.2) |
| 19 | `object_destroy` | 0x00a57630 | resolved, written up (E.1) |
| 20 | `npc_is_in_party` | 0x00a562a0 | resolved, written up (D.2) |
| 21 | `npc_go_idle` | 0x00a554b0 | resolved, written up (D.3) |
| 22 | `minimap_icon_remove_do` | 0x00a54430 | resolved, written up (E.3) |
| 23 | `item_show` | 0x00a51d50 | resolved, written up (F.1) |
| 24 | `item_anim_play` | 0x00a51ad0 | resolved, written up (F.2) |
| 25 | `helicopter_shoot_vehicle` | 0x00a4d790 | resolved, written up (G.2) |

25 of 25 resolved (each to exactly one registration in 0x00a20840) and written up; none failed. Depth-1 limits are marked OPEN per entry — chiefly 0x005088c0 (D.2), 0x00931980 (A.2) and the radio station internals (G.1).


