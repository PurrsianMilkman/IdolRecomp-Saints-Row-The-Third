# Scoping note — Steamworks call boundary (game → Steam, not Steam itself)

**Date:** 2026-10-07 · **Type:** scoping note only — no spec content, nothing here is cleared for implementation.
**Explicit scope limit (carried over from the task brief, repeated here so it survives on its own):** Steamworks is **not** to be reverse-engineered. The GOG build ships a 15 KB no-op stub `steam_api.dll` — Team B will link the real Steamworks SDK against this game later. This note documents only the **game's own side of the call boundary**: which of the 17 imported Steamworks functions the game calls, from where, with what arguments, when (startup/per-frame/shutdown/on-demand), and — the part that actually matters given the stub — **what the game does when `SteamAPI_Init` fails or an interface accessor comes back unusable.** Nothing about Steamworks' own internal behaviour is characterised beyond the few bytes needed to show the stub always "succeeds" trivially; the real SDK's behaviour is out of scope by the task's own statement.
**FORBIDDEN sources (unchanged, obeyed):** no leaked Saints Row IV/Re-Elected source, no leaked Volition/Havok/Audiokinetic material. `.czn_pc` not touched (not relevant to this topic anyway). Steamworks callback-ID naming below cites only the publicly-published SDK header enum values (`steamclientpublic.h`), the same way this project already cites Wwise's and Bink's public API names — never partner-gated documentation.
**Method:** read-only, against the project's existing analysed Ghidra project (`tools/ghidra_projects/SR3`, opened `-readOnly -noanalysis`, nothing saved, no decompiler output used — listing-mode only) and direct Capstone disassembly via the same pattern as `tools/harnesses/exe_va.py`. Two passes: (1) a reference-manager + raw-little-endian-immediate census (`ghidra/CrreishDump.java` `xref` mode) against each of the 17 import-address-table slots for `steam_api.dll`, same method as `scoping-audio.md` §1.2; (2) listing dumps (no decompile) of the handful of functions that matter for the init/shutdown/per-frame story, cross-checked with direct Capstone reads of the stub DLL and of the data pushed around the `SteamAPI_Init` call site.
**Confidence labels:** the project's usual ones — **CONFIRMED** (read directly in this pass), **HIGH** (inferred from structure), **HYPOTHESIS** (plausible, not checked).

---

## 0. Verdict up front

1. **The prior whole-binary census's numbers are exactly right, and this pass explains the two gaps it flagged.** A full reference census (CALL sites *and* "cache the interface pointer in a register for later" sites) finds **104 distinct reference sites to the 17 import slots, in roughly 45 calling functions** — a strict superset of the prior census's 60 direct-`CALL` sites (every one of the prior census's per-function counts that this pass could cross-check — SteamUser 14, SteamFriends 10, `UnregisterCallback`/`UnregisterCallResult` 7 each — reproduces exactly). The extra 44 sites are places that read the interface accessor's return value into a register and use it later rather than calling through it immediately; same functional boundary crossing, just not visible to a flat `CALL`-only scan.
2. **`SteamAPI_Init`'s failure path is real, and it is a silent skip, not a crash, a retry, or a hard exit.** Read directly off the init function (§2): if `SteamAPI_Init()` returns false, the function returns immediately — it never calls `SteamUser()`, never calls `SteamClient()`, never registers the one Steam callback this path sets up, and never registers the `SteamAPI_Shutdown` cleanup hook either. The game simply continues running with none of that having happened. There is no error dialog, no fallback account system, and no process exit tied to this check.
3. **A second, independent exit() gate sits immediately before `SteamAPI_Init`, and it is also dead on this build.** The helper function it calls always returns true (`mov eax,1; ret` — CONFIRMED, direct read), so the `exit(1)` branch next to it never executes here. Combined with the stub DLL's own `SteamAPI_Init` body (`mov al,1; ret` — always "succeeds", CONFIRMED by direct disassembly of `steam_api.dll` itself), **every gate on this exact build resolves to "proceed."** The failure branch in point 2 is real code that would run against a genuine Steamworks SDK on a machine without Steam running — it is simply never exercised by this stub.
4. **`SteamAPI_Shutdown` is not dead code — it is registered, not called.** The address of its import thunk is pushed as a plain function-pointer argument to a small generic "append to a LIFO cleanup list" helper, immediately after a successful `SteamAPI_Init`, and only then. A sibling helper walks that same list back-to-front at process exit and calls each entry before finally calling the CRT `__exit`. This fully explains why the earlier census could not find a direct `CALL dword ptr [SteamAPI_Shutdown]` anywhere: there isn't one. It also means shutdown is symmetric with the failure path in point 2 — if `SteamAPI_Init` fails, the shutdown hook is never registered either, so nothing Steam-shaped runs at process exit in that case.
5. **`SteamAPI_RestartAppIfNecessary` has no real call site at all.** The one raw byte-pattern hit the scan found for it sits inside a short run of bytes an unconditional jump in the init function jumps clean over — i.e. inside dead/unreached code, confirmed by reading the actual instruction stream (§2). This is a coincidental 4-byte collision, not a use.
6. **The on-demand surface (the other 14 imports) is not funnelled through one adapter the way Wwise is** (`scoping-audio.md` §1.1's "one contiguous block"). It is scattered across roughly a dozen small clusters spread through the whole binary — matchmaking/lobby handling, a cloud-storage existence check, friends/overlay calls (several of which are **already specified** at the Lua-binding level in `spec-lua-api-behaviour.md`), a few DLC/ownership checks, and scattered single-purpose getters. §3 maps the clusters; §4 cross-references what is already specified.
7. **At least one on-demand cluster explicitly null-checks the interface accessor before using it** (the cloud-storage cluster, §3.4) and returns a specific soft error code rather than dereferencing a null pointer. The stub's accessors never return null, so this check is dead on this build too, but it shows the game was written expecting the interface to sometimes be unavailable at the *individual-call* level, not only at startup.

---

## 1. Every game-to-Steamworks reference site (reference-manager + raw-scan census)

Census method: for each of the 17 import-address-table slots, every instruction anywhere in the executable that reads that slot, found by Ghidra's reference manager plus a raw little-endian 4-byte scan of all initialized memory (same combined method `CrreishDump.java`'s `xref` mode already uses project-wide; same idea as the audio census's reference-manager pass). CONFIRMED counts; the enclosing-function attribution is CONFIRMED where Ghidra has a defined function there, "(no function)" where the hit falls in code Ghidra never grouped into a function (noted per row).

| Import (IAT slot) | Direct `CALL`/`JMP [slot]` sites | Register-cached reads (`MOV reg,[slot]`, used later) | Total | Calling functions (hex only) |
|---|---|---|---|---|
| `SteamAPI_Init` (`0x0101c550`) | 1 | 0 | 1 | `0x00870cf0` |
| `SteamAPI_RunCallbacks` (`0x0101c544`) | 1 | 0 | 1 | `0x00870db0` |
| `SteamClient` (`0x0101c554`) | 1 | 0 | 1 | `0x00870cf0` |
| `SteamAPI_Shutdown` (`0x0101c558`) | **0 confirmed** | — | 0 | none directly — registered as a callback pointer instead, see §2.3 |
| `SteamAPI_RestartAppIfNecessary` (`0x0101c57c`) | **0 confirmed** | — | 0 | the one raw hit is inside unreached bytes, see §2.4 |
| `SteamUser` (`0x0101c54c`) | 14 | 11 | 25 | 24 distinct functions, incl. `0x00870cf0`, `0x00870450`, `0x008704d0`, `0x0087aef0` (×2), `0x0087b120`, `0x00b95ff0`, `0x00b96050`, `0x00b96170`, `0x00b96230`, `0x00b96640`, `0x00b966d0`, `0x00b96760` |
| `SteamFriends` (`0x0101c570`) | 10 | 7 | 17 | 15 distinct functions, incl. `0x008420c0` (already Lua-specced), `0x00870450`, `0x008704d0`, `0x00870db0`, `0x008707c0`, `0x00870810`, `0x0087ad80`, `0x0087adf0`, `0x0087aef0`, `0x0087b120`, `0x0087b260` |
| `SteamMatchmaking` (`0x0101c548`) | 7 | 9 | 16 | 11 distinct functions, incl. `0x0086e0d0`, `0x00870db0` (tail-dispatch), `0x008707c0`, `0x00870880`, `0x0087fd70`, `0x00880530`, `0x0088dd80` |
| `SteamRemoteStorage` (`0x0101c584`) | 2 | 7 | 9 | 8 distinct functions, incl. `0x00b95eb0`, `0x00b95f30`, `0x00b96050`, `0x00b96340`, `0x00b963f0`, `0x00b96540`, `0x00b96640` |
| `SteamAPI_UnregisterCallback` (`0x0101c564`) | 7 | 3 | 10 | 10 distinct functions, incl. `0x0086e1f0`, `0x00870a30`, `0x00870b20`, `0x00880290`, `0x008803d0`, `0x00880410`, `0x00880920`, `0x00880950`, `0x00880990`, `0x01018ad0` |
| `SteamAPI_UnregisterCallResult` (`0x0101c568`) | 7 | 0 | 7 | `0x0086e230`, `0x0086e5b0`, `0x0086e640`, `0x00870a30`, `0x0087d180`, `0x0087fad0`, `0x01018ad0` |
| `SteamApps` (`0x0101c580`) | 3 | 1 | 4 | `0x0045bd40`/`0x0045be39` (one caller, two sites), `0x00701da0`, `0x00849660` |
| `SteamAPI_RegisterCallback` (`0x0101c560`) | 2 | 1 | 3 | `0x0086e1f0`, `0x00870b20`, `0x008803d0` |
| `SteamUserStats` (`0x0101c574`) | 1 | 2 | 3 | `0x00711e40`, `0x00870410`, `0x00870450`(tail-call region) |
| `SteamNetworking` (`0x0101c55c`) | 1 | 3 | 4 | `0x00870151`-adjacent thunk region, `0x00ddea60`, `0x00ddec50`, `0x00ddee40` |
| `SteamUtils` (`0x0101c578`) | 2 | 0 | 2 | `0x00701da0`, and one site in an un-grouped code region near `0x0087067a` |
| `SteamAPI_RegisterCallResult` (`0x0101c56c`) | 1 | 0 | 1 | `0x0087fad0` |
| **Total** | **60** | **44** | **104** | ~45 distinct functions |

Notes on the table:
- The "direct `CALL`/`JMP`" column reproduces the prior whole-binary census's 60-site count exactly, function for function, everywhere this pass could cross-check it (SteamUser 14, SteamFriends 10, `UnregisterCallback` 7, `UnregisterCallResult` 7 — all exact matches). The "register-cached reads" column is new granularity from this pass, not a contradiction of the prior number.
- `SteamAPI_RegisterCallResult` (1 site) versus `SteamAPI_UnregisterCallResult` (7 sites) is a real asymmetry this pass did not resolve — plausibly because the register side is a single shared helper while unregister is inlined per call-result object, but that is HYPOTHESIS; flagged OPEN rather than guessed at further.
- Two imports (`SteamAPI_Shutdown`, `SteamAPI_RestartAppIfNecessary`) have no genuine call site at all by this method — both are explained in §2, not left as "scan limitation" hand-waves this time.

---

## 2. Startup, shutdown, and the init-failure branch (the decision-relevant part)

### 2.1 The stub DLL's own behaviour, just enough to show every gate resolves to "proceed"

Read directly off `steam_api.dll` (15 KB, 4 sections, 17 exports — matches the already-known `spec-output.md` §1.4 identification). Every export's first instructions (CONFIRMED, direct Capstone disassembly of the shipped DLL):

| Export | Body (first bytes) |
|---|---|
| `SteamAPI_Init` | `mov al,1 ; ret` — always returns true |
| `SteamAPI_Shutdown` | `ret` — pure no-op |
| `SteamAPI_RestartAppIfNecessary` | `xor al,al ; ret` — always returns false |
| `SteamUser`/`SteamClient`/`SteamFriends`/`SteamUtils`/`SteamApps`/`SteamMatchmaking`/`SteamNetworking`/`SteamRemoteStorage`/`SteamUserStats` | each is `mov eax,<constant address in the DLL's own .rdata> ; ret` — a distinct non-null pointer per interface |

This is as far as this note goes into the stub's own behaviour — it is Steamworks' (emulated) side, out of scope. The one fact that matters for the game-side story is: **every accessor returns a non-null pointer, and `SteamAPI_Init` always reports success**, so none of the game's own "Steam unavailable" branches below are ever actually taken on this build.

### 2.2 The init function (`0x00870cf0`) — full branch trace

Called from exactly one site (`0x0086fc30`, inside a function beginning at `0x0086fbe0`) — a single, unconditional, once-per-process boot call (CONFIRMED, from the function's own reference list).

Listing-level trace (CONFIRMED — direct read, no decompiler):

```
0x00870cf0  guard byte @0x024d6800 already set?  -> if yes, return now (ran once already)
0x00870d19  call 0x00d48390                        ; always returns true on this build (CONFIRMED: body is "mov eax,1; ret")
0x00870d20  if false: push 1; call __exit            ; dead on this build — a genuine process-exit gate, just never reached here
0x00870d29  call 0x00d449d0                          ; unexamined helper (OPEN — not Steam-specific by its call position)
0x00870d2e  push 0x00870320 ; call 0x00dc43f0         ; registers a cleanup callback — NOT Steam-related (see §2.3)
0x00870d3b  call [SteamAPI_Init]                      ; <<< the actual call >>>
0x00870d41  store return value into guard byte @0x024d6800
0x00870d46  test the return value
0x00870d48  if FALSE: skip straight to the epilogue and return   <<< the entire failure path >>>
0x00870d4a  push 0x00870330 ; call 0x00dc43f0         ; registers the SteamAPI_Shutdown thunk as a cleanup callback (§2.3) — ONLY reached on success
0x00870d57  call [SteamUser] ; get the local SteamID via a virtual call (vtable slot +0x8) ; store it
0x00870d7a  call 0x00870b20                           ; registers the one callback this path sets up (§2.5) — ONLY reached on success
0x00870d7f  call [SteamClient] ; virtual call through vtable slot +0x54 ; passes function pointer 0x00754410 as argument
0x00870d93  reset a small state dword to 0
0x00870d9d  call 0x00d1de80                           ; unexamined helper (OPEN)
0x00870da5  tail-jump to 0x0101b5b0                    ; generic continuation, not chased further (OPEN, not Steam-specific)
```

**On failure, nothing past `0x00870d48` runs: no `SteamUser`, no `SteamClient`, no callback registration, no shutdown-hook registration.** The function just returns. There is no retry loop, no user-facing message from this function, and no process termination tied to this particular check (CONFIRMED — read directly; this is the headline finding of the whole note).

The vtable-slot virtual call at `0x00870d68` (through `SteamUser`'s interface, slot `+0x8`) and the one at `0x00870d91` (through `SteamClient`'s interface, slot `+0x54`, passed a function pointer) are HIGH confidence `GetSteamID` and `SetWarningMessageHook` respectively, by vtable-slot position and argument shape matching the publicly documented flat-C++ interface layout — cited for naming only, not decompiled, not sourced from anything partner-gated.

### 2.3 `SteamAPI_Shutdown` is registered, not called — the mechanism, read directly

The helper both `0x00870320` and `0x00870330` get pushed into, `0x00dc43f0`, is a small generic list-append (CONFIRMED, direct read):

```
0x00dc43f0  eax = counter @0x02a07708
            table[eax] = argument (the pushed function pointer)   ; table @ 0x02a07218
            counter += 1 ; store ; ret
```

Its sibling, `0x00dc4410` (immediately adjacent in the binary), walks that same table **back to front** and calls each entry, then calls the CRT `__exit` — a LIFO cleanup-on-exit list, the same shape as a C-runtime `atexit` chain (CONFIRMED mechanism; "this is the engine's generic shutdown-hook registry" is HIGH, inferred from the shape — it is shared, general infrastructure, not something built only for Steam).

`0x00870330` disassembles to `jmp dword ptr [0x0101c558]` — i.e. it **is** the `SteamAPI_Shutdown` import thunk (CONFIRMED, direct Capstone read; this is the same address the prior census flagged as "an import-thunk stub... but no call site this static scan could confirm"). Its *address* is what gets pushed as the registered callback — it is never the direct target of a `CALL`, which is exactly why no flat scan finds one. Because this registration only happens after the `0x00870d48` success check, **the shutdown hook only ever gets registered if `SteamAPI_Init` succeeded** — symmetric with the failure path in §2.2.

`0x00870320` (registered unconditionally, just before `SteamAPI_Init` is even called) disassembles to a two-instruction stub (`call 0x00d44a10 ; jmp 0x00d48400`) that is unrelated to Steam — it is a different subsystem's cleanup entry registered through the same shared list at the same point in the boot sequence. Mentioned only to show the registration pattern is shared infrastructure, not Steam-specific plumbing.

### 2.4 `SteamAPI_RestartAppIfNecessary` — why the one hit is not a real use

The raw scan's single hit for this import sits at `0x00870d07`. Reading the actual instruction stream around it (§2.2's listing): `0x00870d00` is an unconditional `JMP 0x00870d19`, jumping clean over the byte range `0x00870d05`–`0x00870d18`. `0x00870d07` falls inside that unreached range. The 4-byte value matching the import's IAT address there is a coincidental collision inside dead/padding bytes, not an instruction operand (CONFIRMED by reading the bytes in context). **This import has no confirmed call site anywhere in the binary.** Whether it is genuinely unused, or reached through a calling convention this method cannot see (e.g. a function-pointer table this pass didn't enumerate), is OPEN — but unlike `SteamAPI_Shutdown`, no mechanism explaining it turned up.

### 2.5 The one callback registration on the startup path (`0x00870b20`)

Called from `0x00870cf0` (success path only) with `ecx` set to a persistent object address (`0x024d6330`, the same object `0x00870db0`'s per-frame tick uses — §3.1). Listing (CONFIRMED):

```
0x00870b20  ebx = [SteamAPI_UnregisterCallback]  ; cached
0x00870b28  ebp = [SteamAPI_RegisterCallback]    ; cached
            if the object already has a registration (flag bit 0x1 at +0x4): call ebx (unregister) first
            set the object's own dispatch-function pointer (+0x10) to 0x00870150
            push callback id 0x4b2 (1202 decimal) ; push the object ; call ebp (register)
```

This is the shape of the public Steamworks SDK's header-only `CCallback<T>` helper (constructor registers, destructor unregisters) — HIGH confidence by structure, not from any non-public source. The callback id pushed, `0x4b2` = 1202 decimal, matches the publicly published `k_iSteamNetworkingCallbacks` (1200) + 2 = `P2PSessionRequest_t` in Valve's own published `steamclientpublic.h` enum — HIGH confidence (numeric match to a publicly documented constant, cited for naming only), consistent with this being Saints Row 3's co-op P2P session-request handling. Not traced further (OPEN: what `0x00870150` itself does when the callback actually fires).

---

## 3. The per-frame tick and the on-demand clusters

### 3.1 Per-frame: `0x00870db0`

Called from exactly one site (`0x0086febb`, inside a function beginning at `0x0086fea0`) — consistent with one "service platform/network layer" step in the main loop (HIGH, from the single-caller shape; not confirmed to be literally once per rendered frame).

```
0x00870db4  call [SteamAPI_RunCallbacks]           ; first thing this function does
            drain up to two locally-queued "pending request" slots (two pairs of dwords, 0x024d6298/629c
              and 0x024d62a0/62a4):
              - if neither slot is set: skip to the tail
              - if the second slot is set: build a small local record and dispatch it through what looks
                like the engine's own generic event-object pipeline (0x0086bc00, 0x0087f3b0, 0x0087f290,
                0x0087f420 — HIGH, structural; not traced to a Lua-visible event name)
              - else, if a one-shot flag (byte 0x024d6295) is clear: call [SteamMatchmaking] through vtable
                slot +0x6c with the first slot's two dwords, then set the flag so this only fires once
0x00870f36  call 0x008702c0 (reads/advances a small connection-phase state dword, 0x024d6290 — values 0,2,3,
              4,5 seen written elsewhere; meanings OPEN)
0x00870f41  call 0x005bae10 with argument 0xa3 (163) — a generic poll (OPEN, role not traced)
            if that poll says so, tail-jump into a second, logically separate chunk at 0x008706b0 that
              checks a session/lobby-owner condition and, if met, opens the Steam friends overlay on a
              specific friend via SteamFriends() vtable slot +0x50 with the literal string "chat"
              (data at 0x01165af4) — the same vtable slot the already-specced `game_show_coop_partner_
              gamercard` Lua binding uses for ActivateGameOverlayToUser (spec-lua-api-behaviour.md §44.2)
```

Ghidra's own function-body range for `0x00870db0` (`0x008706b0`–`0x00870f59`) merges these two chunks into one listing because of the tail-jump back edge; they read as two logically separate pieces of behaviour (a RunCallbacks-and-drain-requests tick, and a separate "auto-open chat overlay" check it occasionally falls into) and are described separately here rather than as one function. CONFIRMED for the instruction trace; the "two logically separate pieces" framing is HIGH.

### 3.2 Matchmaking / lobby cluster (`0x0086e0d0`–`0x0086e680`)

Heaviest user of `SteamAPI_RegisterCallback`/`UnregisterCallback` *and* `SteamAPI_RegisterCallResult`/`UnregisterCallResult` together — the shape of the SDK's header-only async-call-result pattern layered on top of a standing callback. `0x0086e1f0` registers with callback id `0x1f9` = 505 decimal, matching the publicly published `k_iSteamMatchmakingCallbacks` (500) + 5 = `LobbyDataUpdate_t` (HIGH, numeric match, public enum, naming only). `0x0086e0d0` iterates up to a fixed cap (`0x15e0` = 5600) reading matchmaking data through a cached `SteamMatchmaking()` pointer — HIGH: a lobby-member or lobby-data iteration loop, not traced to field level. `0x0086e230` is a 3-way dispatch on a small integer read from its first argument — HIGH: an `OnRunCallback`/call-result outcome dispatcher, exact meaning OPEN.

### 3.3 Startup-adjacent `SteamUser`/`SteamFriends`/`SteamMatchmaking`/`SteamUserStats` cluster (`0x00870340`–`0x00870993`)

The same small address range as the init/tick functions (§2.2, §3.1). Mostly tiny one-line getters over the flag bytes the tick function sets (`0x00870340`, `0x00870350`, `0x00870360` — each a single `mov al,[flag]; ret`), plus `0x00870700`/`0x00870880` which write the two pending-request slot pairs the tick function drains (§3.1) and, in `0x00870880`'s case, also reads `SteamMatchmaking()` directly. HIGH: this is the "incoming request" half of the same local queue the tick function drains; not traced to a Lua-visible entry point in this pass.

### 3.4 Cloud-storage cluster (`0x00b95eb0`–`0x00b967e0`)

Heaviest user of `SteamRemoteStorage` and a secondary user of `SteamUser`. `0x00b95eb0` calls `SteamRemoteStorage()` once (cached in `edi`) and then makes two virtual calls through different vtable slots (`+0xc`, then `+0x4`) each taking a local output buffer — HIGH: an existence/size-style query pair (`FileExists` then a size/info call), not confirmed to exact method names. `0x00b96050` calls `SteamUser()` and **explicitly null-checks the result** (`test eax,eax ; jne ...`), returning a specific error code (6) on the null branch rather than dereferencing it (CONFIRMED, direct read — this is the per-call defensive check mentioned in §0 point 7). Because the stub's `SteamUser()` never returns null, this branch is dead on this build, but it is real, deliberate code. Not traced further into what the cloud operation itself is (save-game sync is HIGH by the cluster's shape and this project's known `spec-save-format.md`, but not confirmed here).

### 3.5 Smaller, single-purpose clusters (not traced beyond identification)

- `SteamApps` — `0x0045bd40`/`0x0045be39` (a very early address, consistent with a boot-time DLC/ownership check elsewhere in this project's own numbering convention), `0x00701da0`, `0x00849660`. OPEN: which DLC/ownership flags, not traced.
- `SteamUtils`/`SteamUserStats`/`SteamNetworking` — 2–4 reference sites each, scattered (`0x00701da0`, `0x00711e40`, `0x00870410`, `0x00ddea60`/`0x00ddec50`/`0x00ddee40`). OPEN: not traced individually in this pass; each is small enough (1–4 sites) to pick up directly in a spec pass without further scoping.

### 3.6 Already specified at the Lua-binding level — do not duplicate

Several Steam-touching call sites are **already specified**, one Lua binding at a time, in `spec-lua-api-behaviour.md`:
- `game_steam_open_overlay` (`0x008420c0`) — calls the `SteamFriends` overlay-activation method; §41.2 there.
- `game_show_coop_partner_gamercard` (`0x008425a0`) — `SteamFriends()` vtable slot `+0x50` (`ActivateGameOverlayToUser`, HIGH), not null-checked; §44.2 there.
- `game_is_connected_to_internet` / `game_is_connected_to_service` — `SteamUser()` non-null **and** `ISteamUser::BLoggedOn`; §44.2 there.
- A handful of co-op/matchmaking Lua bindings (`game_set_coop_join_type`, `game_coop_kick_player`, `game_is_joinable`, `game_join_syslink_game`, …) that sit next to the Steam-touching ones in the same registrar (`0x00845aa0`) but are about the game's own lobby/session bookkeeping rather than direct Steam calls — already specced there too, not re-covered here.

This note's census (§1) is the **native-code call-boundary layer** underneath that binding layer — the init/shutdown/per-frame/registration machinery and the on-demand clusters that are not Lua-exposed. The two layers are complementary, not overlapping; a future spec should cross-reference rather than duplicate.

---

## 4. Proposed unit

**Small enough to spec directly as one unit — no need to split into a U0–Un sequence the way Havok/Wwise were.** The entire native-code call boundary is ~45 functions and a few hundred instructions of actual orchestration logic (the on-demand clusters' own business logic — matchmaking/lobby bookkeeping, cloud-save content — is larger, but that content is a "what does the game do with the result" question for whichever system spec already owns it — e.g. a future save/cloud spec, not this boundary doc).

**Proposed: `spec-steamworks-boundary.md`, a single new file**, scoped to exactly what Team B needs to link the real SDK against:
1. The 17-import call-site table (§1 here, tightened to CONFIRMED addresses and argument shapes).
2. The init sequence and its failure branch (§2.2), stated as an exact contract: "call `SteamAPI_Init`; on true, call `SteamUser`→`GetSteamID`, call `SteamClient`→`SetWarningMessageHook`, register one callback (id 1202); on false, do none of that and keep running."
3. The shutdown-registration mechanism (§2.3) stated as a contract: "if init succeeded, the `SteamAPI_Shutdown` thunk address was registered into the generic LIFO cleanup list; it will be invoked once, at process exit, after everything registered later than it."
4. The per-frame contract (§3.1): call `SteamAPI_RunCallbacks` once per tick; the local request-queue draining is engine-side bookkeeping Team B's real SDK doesn't need to know about, only that `RunCallbacks` must be pumped on this cadence.
5. A short "on-demand call shape" section listing each remaining interface's vtable-slot uses found in §3, each still OPEN on exact method identity — explicitly left for a spec pass to firm up slot-by-slot, not resolved here.
6. A pointer to `spec-lua-api-behaviour.md` §41.2/§44.2 for the already-specced Lua-binding-level Steam calls, so the two documents don't duplicate.

This is comfortably one file, one spec pass — **not** worth a multi-unit breakdown. The only sub-parts worth calling out as separately checkable (for review/validation purposes, not separate dispatch) are: (a) the init/shutdown/registration machinery (§2, fully traced already in this note), and (b) the on-demand clusters (§3, identified but not traced to method-name certainty — the remaining real work).

---

## 5. Notes for whoever takes it up

- **Tooling.** The 17 IAT slots sit together in `.rdata` at `0x0101c544`–`0x0101c587`; a reference-manager + raw-LE-scan `xref` query against each slot is a complete, cheap census (no RTTI or heuristics needed, same as the audio census's Wwise export-table approach). The register-cached reads (§1) are real boundary crossings — don't rely on a `CALL`-only scan.
- **Ghidra's function-body merging.** `0x00870db0`'s reported body spans two logically separate pieces of behaviour joined by a tail-jump (§3.1) — read the listing, don't trust the body range as "one function" at face value.
- **Two "always true" gates on this build.** Both the pre-`SteamAPI_Init` exit gate (`0x00d48390`) and `SteamAPI_Init` itself (the stub DLL) are hardcoded to succeed. A spec pass should state the failure branches as real, specified behaviour anyway — Team B's real SDK can genuinely fail `SteamAPI_Init` on a machine without Steam running, and this is the exact branch that will fire.
- **Out of bounds, unaffected by anything here:** Steamworks' own behaviour (the real SDK, whatever it does, is out of scope by the task brief); `.czn_pc` zone data (not touched, not relevant to this topic).
- Every label above is from this scoping pass only. A spec pass must re-derive each claim from the executable before recording it.

## 6. Clean-room check

- Addresses are plain hex throughout. No Ghidra auto-names (`FUN_*`/`DAT_*`/`LAB_*`) appear anywhere in this file — every function and global is referenced only by address. No decompiler output was used or pasted; only Ghidra listing-mode dumps (`nodecomp:1`) and direct Capstone disassembly.
- Names quoted as data: Steamworks flat-C export names come from `steam_api.dll`'s own PE export table (Valve's public API surface); the one callback-id naming (`P2PSessionRequest_t`, `LobbyDataUpdate_t`) cites Valve's publicly published header enum values, used for naming only, the same convention already established for Wwise/Bink in this project.
- The project's standard auto-name/pseudocode regex was run on the finished file: 0 hits.
- No spec file was edited by this pass. The Ghidra session was opened `-readOnly -noanalysis` against the project's existing `SR3` project (nothing created, nothing saved); scratch output under `scratchpad/steam_xref_out/`, `scratchpad/steam_func_out/` and the probe scripts are intermediate working files, not part of this note.
