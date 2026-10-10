# Saints Row: The Third — Steamworks Call Boundary (Game Side Only)

**Prepared by:** SPEC TEAM (cleanroom reverse-engineering process)
**Phase:** single-unit spec, per `review/scoping-steamworks.md` §4's own verdict — the whole native-code Steamworks call boundary is small enough (~45 functions, a few hundred instructions of orchestration) to specify in one pass, with no U0–Un breakdown.
**Scope:** the game's own side of the call boundary into the 17 imported Steamworks functions — which of them are called, from where, with what arguments, when (startup / per-frame / shutdown / on-demand), and what the game does when `SteamAPI_Init` fails or an interface accessor comes back unusable. This file covers the **runtime call contract** a replacement SDK binding must satisfy: the init/shutdown/per-frame sequencing and the shape of each on-demand cluster's use of the remaining interfaces.
**Explicit scope limit (binding, carried over from the scoping note and the task brief):** Steamworks itself is **not** reverse-engineered here. The GOG build ships a 15 KB no-op stub `steam_api.dll`; Team B will link the real Steamworks SDK against this contract. The stub's own bodies are cited only to the extent needed to show every gate on this exact build resolves to "proceed" (§2.1) — nothing about the real SDK's behavior is characterized, and nothing here should be read as a claim about what the genuine library does.
**FORBIDDEN sources (unchanged, obeyed):** no leaked Saints Row IV/Re-Elected source, no leaked Volition/Havok/Audiokinetic material, no other leaked material of any kind. `.czn_pc` is not touched (not relevant to this topic). The two Steamworks callback-id names quoted below (§2.5, §4.1) cite only Valve's publicly published SDK header enum values (`steamclientpublic.h`), the same convention this project already applies to Wwise's and Bink's public API names — never partner-gated documentation.
**Method:** this file specifies directly from `review/scoping-steamworks.md`'s own two-pass investigation (a reference-manager + raw-little-endian-immediate census against all 17 import slots, same method as the project's Wwise export-table census; and listing-mode dumps, no decompile, of the functions that matter for the init/shutdown/per-frame story, cross-checked with direct Capstone disassembly of the stub DLL). No new Ghidra or disassembly work was performed for this pass — every address, count, and instruction-level claim below is carried over from the scoping note's own census, tightened into spec form; nothing was re-derived from scratch.
**Cleanroom compliance:** no decompiled code, decompiler variable names, or pasted pseudocode appear anywhere below. Every function and global is cited by its plain hex address or offset — no Ghidra auto-names. Steamworks flat-C export names (`SteamAPI_Init`, `SteamUser`, `SteamFriends`, …) are read from `steam_api.dll`'s own PE export table — the publisher's own public API surface, quoted as data. The two callback-id names (§2.5, §4.1) are Valve's own publicly published header enum values, used for naming only.
**Confidence key:** **CONFIRMED** (read directly off a listing or direct Capstone disassembly, this pass or the scoping note's); **HIGH** (follows from structure — call shape, vtable-slot position, or a numeric match to a public constant — but not traced end to end); **HYPOTHESIS** (plausible, not checked). Matches the scoping note's own convention.
**Relationship to other specs:** `review/scoping-steamworks.md` is the scoping note this file turns into a spec; every address and count below is traceable to that note's own §1–§5. `spec-lua-api-behaviour.md` §41.2 and §44.2 already specify the Lua-binding-level Steam calls (the friends-overlay bindings, the co-op/online query bindings); this file is the **native-code call-boundary layer underneath** — the init/shutdown/per-frame/registration machinery and the on-demand clusters that are not Lua-exposed at all. §5 below cross-references rather than duplicates those two sections. `spec-output.md` §1.4 already identifies the stub `steam_api.dll` itself (15 KB, 4 sections, 17 exports); not re-derived here.

---

## 0. Results up front

1. **The call boundary is small and already fully censused.** 104 distinct reference sites to the 17 import slots, across ~45 calling functions — 60 direct `CALL`/`JMP [slot]` sites plus 44 "cache the interface pointer in a register, use it later" sites the earlier whole-binary census's flat `CALL`-only scan could not see (§1).
2. **`SteamAPI_Init`'s failure path is a silent skip — no retry, no dialog, no exit.** If `SteamAPI_Init()` returns false, the init function (`0x00870cf0`) returns immediately: it never calls `SteamUser()` or `SteamClient()`, never registers its one callback, and never registers the `SteamAPI_Shutdown` cleanup hook either. The game simply continues running with none of that having happened (§2.2).
3. **A second, independent `exit()` gate sits immediately before `SteamAPI_Init`, and it is also dead on this build** — its own helper (`0x00d48390`) always returns true on this build, so the adjacent `exit(1)` branch never fires here. Combined with the stub DLL's own trivially-succeeding `SteamAPI_Init` body, every gate on this exact build resolves to "proceed" — but both failure branches are real code that will run against a genuine Steamworks SDK on a machine without Steam running (§2.1, §2.2).
4. **`SteamAPI_Shutdown` is registered into a generic LIFO cleanup list, not called directly** — which is exactly why no flat scan ever finds a `CALL [SteamAPI_Shutdown]` anywhere. Registration only happens on the `SteamAPI_Init` success path, so shutdown is symmetric with the failure path in point 2: if init failed, nothing Steam-shaped runs at process exit either (§2.3).
5. **`SteamAPI_RestartAppIfNecessary` has no real call site at all** — its one raw byte-pattern hit sits inside dead/unreached bytes an unconditional jump skips clean over; a coincidental 4-byte collision, not a use (§2.4).
6. **The per-frame contract is one `SteamAPI_RunCallbacks` pump plus engine-side bookkeeping a real-SDK binding doesn't need to reproduce** (§3).
7. **The remaining 14 on-demand imports are not funnelled through one adapter** (unlike Wwise's single contiguous block) — they're scattered across roughly a dozen small clusters: matchmaking/lobby, a startup-adjacent request queue, a cloud-storage cluster with an explicit null-check-and-soft-fail pattern, and several smaller single-purpose clusters (§4). At least one cluster (cloud storage) explicitly null-checks its interface accessor and returns a soft error code rather than dereferencing null — real defensive code, dead only because the stub never returns null (§4.3).
8. **Several of the Lua-visible Steam calls are already specified elsewhere** (`spec-lua-api-behaviour.md` §41.2, §44.2) and are cross-referenced, not repeated, here (§5).

---

## 1. The 17-import reference census

Carried over verbatim from `review/scoping-steamworks.md` §1 (CONFIRMED — reference-manager + raw little-endian 4-byte scan against every one of the 17 import-address-table slots, which sit together in `.rdata` at `0x0101c544`–`0x0101c587`). "Direct" = a `CALL`/`JMP` straight through the slot; "cached" = the slot's value is read into a register and used later — a real boundary crossing a flat `CALL`-only scan misses.

| Import (IAT slot) | Direct | Cached | Total | Calling functions (hex only) |
|---|---|---|---|---|
| `SteamAPI_Init` (`0x0101c550`) | 1 | 0 | 1 | `0x00870cf0` |
| `SteamAPI_RunCallbacks` (`0x0101c544`) | 1 | 0 | 1 | `0x00870db0` |
| `SteamClient` (`0x0101c554`) | 1 | 0 | 1 | `0x00870cf0` |
| `SteamAPI_Shutdown` (`0x0101c558`) | 0 | — | 0 | none directly — registered as a callback pointer instead (§2.3) |
| `SteamAPI_RestartAppIfNecessary` (`0x0101c57c`) | 0 | — | 0 | the one raw hit is inside unreached bytes (§2.4) |
| `SteamUser` (`0x0101c54c`) | 14 | 11 | 25 | 24 distinct functions, incl. `0x00870cf0`, `0x00870450`, `0x008704d0`, `0x0087aef0` (×2), `0x0087b120`, `0x00b95ff0`, `0x00b96050`, `0x00b96170`, `0x00b96230`, `0x00b96640`, `0x00b966d0`, `0x00b96760` |
| `SteamFriends` (`0x0101c570`) | 10 | 7 | 17 | 15 distinct functions, incl. `0x008420c0` (Lua-specced, §5), `0x00870450`, `0x008704d0`, `0x00870db0`, `0x008707c0`, `0x00870810`, `0x0087ad80`, `0x0087adf0`, `0x0087aef0`, `0x0087b120`, `0x0087b260` |
| `SteamMatchmaking` (`0x0101c548`) | 7 | 9 | 16 | 11 distinct functions, incl. `0x0086e0d0`, `0x00870db0` (tail-dispatch), `0x008707c0`, `0x00870880`, `0x0087fd70`, `0x00880530`, `0x0088dd80` |
| `SteamRemoteStorage` (`0x0101c584`) | 2 | 7 | 9 | 8 distinct functions, incl. `0x00b95eb0`, `0x00b95f30`, `0x00b96050`, `0x00b96340`, `0x00b963f0`, `0x00b96540`, `0x00b96640` |
| `SteamAPI_UnregisterCallback` (`0x0101c564`) | 7 | 3 | 10 | 10 distinct functions: `0x0086e1f0`, `0x00870a30`, `0x00870b20`, `0x00880290`, `0x008803d0`, `0x00880410`, `0x00880920`, `0x00880950`, `0x00880990`, `0x01018ad0` |
| `SteamAPI_UnregisterCallResult` (`0x0101c568`) | 7 | 0 | 7 | `0x0086e230`, `0x0086e5b0`, `0x0086e640`, `0x00870a30`, `0x0087d180`, `0x0087fad0`, `0x01018ad0` |
| `SteamApps` (`0x0101c580`) | 3 | 1 | 4 | `0x0045bd40`/`0x0045be39` (one caller, two sites), `0x00701da0`, `0x00849660` |
| `SteamAPI_RegisterCallback` (`0x0101c560`) | 2 | 1 | 3 | `0x0086e1f0`, `0x00870b20`, `0x008803d0` |
| `SteamUserStats` (`0x0101c574`) | 1 | 2 | 3 | `0x00711e40`, `0x00870410`, `0x00870450` (tail-call region) |
| `SteamNetworking` (`0x0101c55c`) | 1 | 3 | 4 | `0x00870151`-adjacent thunk region, `0x00ddea60`, `0x00ddec50`, `0x00ddee40` |
| `SteamUtils` (`0x0101c578`) | 2 | 0 | 2 | `0x00701da0`, and one site near `0x0087067a` (not grouped into a function by Ghidra) |
| `SteamAPI_RegisterCallResult` (`0x0101c56c`) | 1 | 0 | 1 | `0x0087fad0` |
| **Total** | **60** | **44** | **104** | ~45 distinct functions |

`SteamAPI_RegisterCallResult` (1 site) vs. `SteamAPI_UnregisterCallResult` (7 sites) is a real, unresolved asymmetry — carried forward as OPEN item 1 (§6).

---

## 2. Startup and shutdown contract

### 2.1 Why every gate resolves to "proceed" on this exact build — and why that doesn't matter for the contract

Direct Capstone disassembly of the shipped `steam_api.dll` (15 KB, 4 sections, 17 exports — matches `spec-output.md` §1.4's identification): `SteamAPI_Init`'s body is `mov al,1 ; ret` (always true); `SteamAPI_Shutdown`'s body is a bare `ret` (no-op); `SteamAPI_RestartAppIfNecessary`'s body is `xor al,al ; ret` (always false); every interface accessor (`SteamUser`, `SteamClient`, `SteamFriends`, `SteamUtils`, `SteamApps`, `SteamMatchmaking`, `SteamNetworking`, `SteamRemoteStorage`, `SteamUserStats`) is `mov eax,<constant .rdata address> ; ret` — a distinct non-null pointer per interface (all CONFIRMED). This is as far into the stub's own behavior as this file goes — it is the emulated Steamworks side, explicitly out of scope. The one fact that matters for the contract: **every accessor returns non-null, and `SteamAPI_Init` always reports success, so none of the failure branches below are ever actually taken on this build** — but they are real code, and the contract below states them as real, specified behavior a genuine SDK binding must satisfy.

The pre-init gate: a separate helper, `0x00d48390`, is called at `0x00870d19` and always returns true on this build (`mov eax,1 ; ret`, CONFIRMED) — so the adjacent `if false: push 1; call __exit` branch at `0x00870d20` is a genuine process-exit gate that simply never fires here.

### 2.2 The init function (`0x00870cf0`) — contract

Called from exactly one site (`0x0086fc30`, inside a function beginning at `0x0086fbe0`) — a single, unconditional, once-per-process boot call (CONFIRMED, from the function's own reference list). A guard byte at `0x024d6800` makes the function a no-op if it has already run once.

**Contract, stated as an exact sequence (CONFIRMED, direct listing read, no decompiler):**

1. If the guard byte at `0x024d6800` is already set, return immediately (already ran).
2. Call the pre-init gate helper (`0x00d48390`); if it returns false, call `exit(1)` (§2.1 — dead on this build, real on a genuine SDK).
3. Call an unexamined helper (`0x00d449d0`, OPEN item 2, §6) and register an unrelated subsystem's cleanup entry (`0x00870320`, a two-instruction stub unrelated to Steam) into the generic cleanup list described in §2.3 — unconditionally, before `SteamAPI_Init` is even called.
4. **Call `SteamAPI_Init()`.** Store the return value into the guard byte at `0x024d6800`.
5. **On false:** return immediately. Nothing past this point runs — no `SteamUser()`, no `SteamClient()`, no callback registration, no shutdown-hook registration, no retry, no dialog, no process exit tied to this specific check (CONFIRMED — this is the note's headline finding, carried forward unchanged).
6. **On true (the only path that reaches steps 7–10):**
7. Register the `SteamAPI_Shutdown` import-thunk address (`0x00870330`) into the same generic cleanup list (§2.3).
8. Call `SteamUser()` and make a virtual call through its interface's vtable slot `+0x8` — HIGH CONFIDENCE `GetSteamID`, by slot position matching the publicly documented flat-C++ interface layout (cited for naming only). Store the result.
9. Call `0x00870b20` (§2.5) to register the one standing callback this path sets up.
10. Call `SteamClient()` and make a virtual call through its interface's vtable slot `+0x54`, passing function pointer `0x00754410` as an argument — HIGH CONFIDENCE `SetWarningMessageHook`, same basis as step 8.
11. Reset a small state dword to 0, call a further unexamined helper (`0x00d1de80`, OPEN item 3, §6), and tail-jump to a generic continuation (`0x0101b5b0`, not chased further, OPEN item 4, §6).

**Contract summary for a real-SDK binding:** call `SteamAPI_Init()`; on true, call `SteamUser()->GetSteamID()`, call `SteamClient()->SetWarningMessageHook(0x00754410)` (the function pointer itself is this executable's own address — a real binding supplies its own equivalent hook), and register callback id `0x4b2` (§2.5); on false, do none of that, and keep running with no user-visible indication anything failed.

### 2.3 The shutdown registration mechanism

**CONFIRMED, direct read.** `0x00870330` disassembles to `jmp dword ptr [0x0101c558]` — it **is** the `SteamAPI_Shutdown` import thunk. Its *address*, not a call to it, is what gets pushed as an argument to a small generic list-append helper, `0x00dc43f0`:

- `0x00dc43f0`: reads a counter at `0x02a07708`; stores the pushed function-pointer argument into a table at `0x02a07218`, indexed by that counter; increments and stores the counter back; returns.
- A sibling, `0x00dc4410` (immediately adjacent in the binary), walks that same table **back-to-front**, calling each entry, then calls the CRT `__exit` — a LIFO cleanup-on-exit list, the same shape as a C-runtime `atexit` chain (CONFIRMED mechanism; "this is the engine's generic shutdown-hook registry, not something built only for Steam" is HIGH, inferred from the shape — `0x00870320`, registered earlier in the same boot sequence for an unrelated subsystem, uses the identical mechanism).

Because step 7 of §2.2 only runs on the `SteamAPI_Init` success path, **the shutdown hook is only ever registered if init succeeded** — symmetric with the failure path in §2.2 step 5.

**Contract:** if init succeeded, the `SteamAPI_Shutdown` thunk's address was appended to the generic LIFO list at `0x02a07218` (counter at `0x02a07708`); it is invoked exactly once, at process exit, by the walker at `0x00dc4410`, after every cleanup entry registered later than it and immediately before the CRT's own `__exit`. This explains why no flat `CALL`-site scan ever finds a direct call to `SteamAPI_Shutdown` — there isn't one.

### 2.4 `SteamAPI_RestartAppIfNecessary` — confirmed unreachable, not a real call site

The raw scan's single hit for this import sits at `0x00870d07`. The instruction stream around it (part of §2.2's own listing): an unconditional jump at `0x00870d00` (`JMP 0x00870d19`) jumps clean over the byte range `0x00870d05`–`0x00870d18`, and `0x00870d07` falls inside that unreached range — the 4-byte value matching the import's IAT address there is a coincidental collision inside dead/padding bytes, not an instruction operand (CONFIRMED by reading the bytes in context). **This import has no confirmed call site anywhere in the binary** — a real-SDK binding does not need to reproduce any call to it on this build, though whether it's genuinely unused or reached through a mechanism this method can't see (e.g. an unenumerated function-pointer table) is OPEN item 5 (§6).

### 2.5 The one standing callback registration (`0x00870b20`)

Called only from step 9 of §2.2 (init success path), with `ecx` set to a persistent object at `0x024d6330` — the same object the per-frame tick (§3) uses.

**CONFIRMED, direct listing:**

1. Cache `SteamAPI_UnregisterCallback` in one register, `SteamAPI_RegisterCallback` in another.
2. If the object already carries a registration (flag bit `0x1` at offset `+0x4`), call the unregister accessor first.
3. Set the object's own dispatch-function-pointer field (`+0x10`) to `0x00870150`.
4. Push callback id `0x4b2` (1202 decimal), push the object, call the register accessor.

This is the shape of the public Steamworks SDK's header-only `CCallback<T>` helper (constructor registers, destructor unregisters) — HIGH CONFIDENCE by structure, not sourced from anything non-public. The callback id, `0x4b2` = 1202 decimal, matches Valve's own publicly published `steamclientpublic.h` enum value `k_iSteamNetworkingCallbacks` (1200) + 2 = `P2PSessionRequest_t` — HIGH CONFIDENCE (a numeric match to a publicly documented constant, cited for naming only), consistent with this being Saints Row 3's co-op P2P session-request handling. What `0x00870150` itself does when the callback fires was not traced — OPEN item 6 (§6).

**Contract:** a real-SDK binding must register a `P2PSessionRequest_t` (id 1200+2) callback once, immediately after a successful `SteamAPI_Init`, and unregister it on shutdown via the same cleanup-list mechanism (§2.3) — `0x00870150` is this executable's own handler and is not itself part of the contract Team B needs to reproduce, only the registration/unregistration shape is.

---

## 3. Per-frame contract

`0x00870db0`, called from exactly one site (`0x0086febb`, inside a function beginning at `0x0086fea0`) — consistent with one "service platform/network layer" step per call of that enclosing function (HIGH, from the single-caller shape; not confirmed to be literally once per rendered frame).

**CONFIRMED, direct listing trace:**

1. **First instruction: call `SteamAPI_RunCallbacks()`.** This is the entire Steamworks-mandated part of the per-frame contract.
2. Drain up to two locally-queued "pending request" slot pairs (`0x024d6298`/`0x024d629c`, and `0x024d62a0`/`0x024d62a4`):
   - if neither slot is set, skip to step 3;
   - if the second slot is set, build a small local record and dispatch it through what looks like the engine's own generic event-object pipeline (`0x0086bc00`, `0x0087f3b0`, `0x0087f290`, `0x0087f420` — HIGH, structural; not traced to a Lua-visible event name, OPEN item 7, §6);
   - otherwise, if a one-shot flag byte (`0x024d6295`) is clear, call `SteamMatchmaking()` through vtable slot `+0x6c` with the first slot's two dwords, then set the flag so this fires only once.
3. Call `0x008702c0`, which reads/advances a small connection-phase state dword (`0x024d6290`; values 0, 2, 3, 4, 5 seen written elsewhere; meanings OPEN item 8, §6).
4. Call `0x005bae10` with argument `0xa3` (163) — a generic poll whose role was not re-traced in this pass (OPEN item 9, §6). **Cross-reference note:** `0x005bae10` is the same button-query entry point `spec-player-runtime.md` §1.8 documents as part of the input-query contract; that spec's own §0 point 2 and its later §13.0 text currently disagree with each other on whether this function reports "pressed this frame" or "held," so this file does not take a position on which semantic applies at this call site, or on what action code `0xa3` (163) names — carried forward as OPEN, not re-derived.
5. If that poll says so, tail-jump into a second, logically separate chunk at `0x008706b0`: checks a session/lobby-owner condition, and if met, opens the Steam friends overlay on a specific friend via `SteamFriends()` vtable slot `+0x50` with the literal string `"chat"` (data at `0x01165af4`) — the same vtable slot the already-specced `game_show_coop_partner_gamercard` Lua binding uses for `ActivateGameOverlayToUser` (`spec-lua-api-behaviour.md` §44.2, §5.2 below).

Ghidra's own function-body range for `0x00870db0` (`0x008706b0`–`0x00870f59`) merges steps 1–4 and step 5 into one listing because of the tail-jump back edge; they read as two logically separate pieces of behavior — a `RunCallbacks`-and-drain-requests tick, and a separate "auto-open chat overlay" check it occasionally falls into — and are described separately here rather than as one function (CONFIRMED instruction trace; "two logically separate pieces" framing is HIGH).

**Contract:** pump `SteamAPI_RunCallbacks()` once per invocation of this per-frame step. The request-queue draining (step 2) and the occasional auto-open-overlay behavior (step 5) are engine-side bookkeeping layered on top of the Steamworks call itself — a real-SDK binding only needs to guarantee `RunCallbacks` is pumped on the same cadence; it does not need to reproduce steps 2–5, which are the game's own business logic, not part of the Steamworks call contract.

---

## 4. On-demand call clusters

The remaining 14 imports are not funnelled through one adapter (contrast Wwise's single contiguous block, `scoping-audio.md` §1.1) — they are scattered across roughly a dozen small clusters. Carried over from `review/scoping-steamworks.md` §3; none of these were re-traced to method-name certainty in this pass, per the scoping note's own §3.5/§4 recommendation that they're small enough to pick up individually in a future spec pass if finer detail is ever needed.

### 4.1 Matchmaking / lobby cluster (`0x0086e0d0`–`0x0086e680`)

The heaviest combined user of `SteamAPI_RegisterCallback`/`UnregisterCallback` *and* `SteamAPI_RegisterCallResult`/`UnregisterCallResult` — the shape of the SDK's header-only async-call-result pattern layered on top of a standing callback.

- `0x0086e1f0` registers with callback id `0x1f9` = 505 decimal, matching Valve's publicly published `k_iSteamMatchmakingCallbacks` (500) + 5 = `LobbyDataUpdate_t` (HIGH, numeric match to a public enum, naming only).
- `0x0086e0d0` iterates up to a fixed cap (`0x15e0` = 5600) reading matchmaking data through a cached `SteamMatchmaking()` pointer — HIGH: a lobby-member or lobby-data iteration loop, not traced to field level.
- `0x0086e230` is a 3-way dispatch on a small integer read from its first argument — HIGH: an `OnRunCallback`/call-result outcome dispatcher; exact meaning OPEN item 10 (§6).

### 4.2 Startup-adjacent request-queue cluster (`0x00870340`–`0x00870993`)

The same small address range as the init/tick functions (§2.2, §3). Mostly tiny one-line getters over the flag bytes the per-frame tick sets (`0x00870340`, `0x00870350`, `0x00870360` — each a single "load flag byte, return" body), plus `0x00870700`/`0x00870880`, which write the two pending-request slot pairs the tick function drains (§3 step 2); `0x00870880` additionally reads `SteamMatchmaking()` directly. HIGH: this is the "incoming request" half of the same local queue the per-frame tick drains; not traced to a Lua-visible entry point (OPEN item 11, §6).

### 4.3 Cloud-storage cluster (`0x00b95eb0`–`0x00b967e0`)

The heaviest user of `SteamRemoteStorage`, and a secondary user of `SteamUser`.

- `0x00b95eb0` calls `SteamRemoteStorage()` once (cached in a register) and then makes two virtual calls through different vtable slots (`+0xc`, then `+0x4`), each taking a local output buffer — HIGH: an existence/size-style query pair (`FileExists` then a size/info call), not confirmed to exact method names.
- `0x00b96050` calls `SteamUser()` and **explicitly null-checks the result** (test-and-branch), returning a specific error code (6) on the null branch rather than dereferencing it (CONFIRMED, direct read). This is the one place in the whole census where the game's own code is written expecting the interface accessor to sometimes come back unavailable at the individual-call level, not only at startup — real, deliberate code, dead on this build only because the stub's `SteamUser()` never returns null.

What the cloud operation itself is was not traced further (save-game sync is HIGH by the cluster's shape and this project's own `spec-save-format.md`, but not confirmed here — OPEN item 12, §6).

### 4.4 Smaller single-purpose clusters (identified, not traced further)

- **`SteamApps`:** `0x0045bd40`/`0x0045be39` (a very early address, consistent with a boot-time DLC/ownership check per this project's own address-numbering convention elsewhere), `0x00701da0`, `0x00849660`. Which DLC/ownership flags these resolve is OPEN item 13 (§6).
- **`SteamUtils` / `SteamUserStats` / `SteamNetworking`:** 2–4 reference sites each, scattered (`0x00701da0`, `0x00711e40`, `0x00870410`, `0x00ddea60`/`0x00ddec50`/`0x00ddee40`). Not traced individually in this pass — each is small enough (1–4 sites) to pick up directly in a future spec pass without further scoping (OPEN item 14, §6).

---

## 5. Cross-references — already specified at the Lua-binding level

The following are specified at the Lua-binding level in `spec-lua-api-behaviour.md` and are **not duplicated here** — this file's census (§1) is the native-code call-boundary layer underneath that binding layer; the two documents are complementary.

### 5.1 `spec-lua-api-behaviour.md` §41.2

`game_steam_open_overlay` (`0x008420c0`): 1 string argument (a non-string argument is passed through as null, unchanged), no return. Calls the `SteamFriends` interface's overlay-activation virtual method, gated on the interface pointer resolving. What Steam does with a null string was not traced there (OPEN, carried as-is).

### 5.2 `spec-lua-api-behaviour.md` §44.2

The co-op/network/Steam cluster of 7 functions in registrar `0x00845aa0`:

- `game_show_coop_partner_gamercard` (`0x008425a0`): opens the Steam friends overlay on the remote player's Steam ID via `SteamFriends()` vtable slot `+0x50` (HIGH CONFIDENCE `ActivateGameOverlayToUser`) — **not null-checked**, unlike this file's §3 step 5 use of the identical vtable slot via the literal string `"chat"`, and unlike §5.1's sibling overlay call.
- `game_is_connected_to_internet` / `game_is_connected_to_service`: the same handler (`0x008428e0`) — both resolve to `SteamUser()` non-null **and** `ISteamUser::BLoggedOn`, distinct from a separate, weaker "interface exists" test elsewhere in that document.
- `game_set_coop_join_type`, `game_coop_kick_player`, `game_is_joinable`, `game_join_syslink_game`, `game_coop_get_starting_syslink` — the remaining 5 of the 7 are the game's own lobby/session bookkeeping sitting next to the Steam-touching ones in the same registrar, not direct Steam calls themselves; already specced there, not re-covered here.

---

## 6. OPEN items

1. `SteamAPI_RegisterCallResult` (1 confirmed site) vs. `SteamAPI_UnregisterCallResult` (7 confirmed sites) — a real asymmetry; plausibly a single shared register helper against per-call-result inlined unregister sites, but HYPOTHESIS, not resolved (§1).
2. `0x00d449d0`, called from the init function before `SteamAPI_Init` — role not traced; not confirmed Steam-specific by its call position alone (§2.2 step 3).
3. `0x00d1de80`, called from the init function's success tail — role not traced (§2.2 step 11).
4. `0x0101b5b0`, the init function's tail-jump target — a generic continuation, not chased further; not Steam-specific (§2.2 step 11).
5. Whether `SteamAPI_RestartAppIfNecessary` is genuinely unused by this build, or reached through a mechanism this census method can't see (e.g. a function-pointer table not enumerated) — no confirmed call site exists either way (§2.4).
6. `0x00870150`, the dispatch body set as the standing `P2PSessionRequest_t` callback's handler — what it does when the callback actually fires was not traced (§2.5).
7. The generic event-object pipeline (`0x0086bc00`, `0x0087f3b0`, `0x0087f290`, `0x0087f420`) the per-frame tick's pending-request drain dispatches through — structural only, not traced to a Lua-visible event name (§3 step 2).
8. The connection-phase state dword `0x024d6290`'s values (0, 2, 3, 4, 5 seen written elsewhere) and `0x008702c0`'s exact role — meanings OPEN (§3 step 3).
9. The generic poll at `0x005bae10` with argument `0xa3` (163), called from the per-frame tick before deciding whether to auto-open the friends overlay — role not re-traced this pass; see the cross-reference note at §3 step 4 regarding an unresolved internal disagreement elsewhere in this project about this same function's exact semantics.
10. `0x0086e230`'s 3-way dispatch — an `OnRunCallback`/call-result outcome dispatcher by shape; exact meaning of each of the three branches OPEN (§4.1).
11. The startup-adjacent request-queue cluster's writers (`0x00870700`, `0x00870880`) — not traced to a Lua-visible entry point (§4.2).
12. What the cloud-storage cluster's own operation actually is at the content level (save-game sync is HIGH by shape and cross-reference to `spec-save-format.md`, not confirmed from this side) (§4.3).
13. Which DLC/ownership flags the `SteamApps` cluster (`0x0045bd40`/`0x0045be39`, `0x00701da0`, `0x00849660`) resolves (§4.4).
14. The `SteamUtils`/`SteamUserStats`/`SteamNetworking` scattered single-purpose sites — not traced individually; each small enough for a future pass to open directly without further scoping (§4.4).

---

## 7. Clean-room check

- Addresses and offsets are plain hex throughout, everywhere in this file. No Ghidra auto-names for functions, globals, or labels appear anywhere below, and no decompiler output — pseudocode expression, auto-named local, or raw-asm listing dump — was read, pasted, or quoted. Behavior is described in prose, numbered steps, and plain tables; every function and global is cited only by its address or a named offset from one.
- Names quoted as data: Steamworks flat-C export names (`SteamAPI_Init`, `SteamUser`, `SteamFriends`, `SteamClient`, `SteamMatchmaking`, `SteamRemoteStorage`, `SteamApps`, `SteamUtils`, `SteamUserStats`, `SteamNetworking`, and the `SteamAPI_*` free functions) are read from `steam_api.dll`'s own PE export table — the publisher's own public API surface. The two callback-id names (`P2PSessionRequest_t`, `LobbyDataUpdate_t`) cite Valve's own publicly published header enum values, used for naming only — the same convention this project already applies to Wwise's and Bink's public API names. The literal data string `"chat"` is read directly from the executable's own static data.
- Self-check result, run on the finished file as the last step before publishing: the project's standard strict word-bounded regex for Ghidra auto-named decompiler variables, stack/register-input names, and auto-generated function/data/label names returned **0 hits**; the plain-substring backstop for the same token families (run separately, since the regex's word-boundary behavior can silently under-match on a token immediately followed by a digit) also returned **0 hits**. **0/0.**
- No spec file other than this one was edited by this pass. No Ghidra session was opened for this pass — every address, count, and instruction-level claim is carried over from `review/scoping-steamworks.md`'s own already-completed census (that note's own Ghidra session was opened `-readOnly -noanalysis`, nothing saved).

---

## Changelog

- **2026-10-07 — Initial version** (SPEC TEAM). New file, written directly from `review/scoping-steamworks.md` in a single pass, per that note's own §4 verdict that the whole native-code Steamworks call boundary is small enough for one spec pass with no sub-unit breakdown. Contents:
  - §1: the full 17-import, 104-reference-site census, carried over unchanged from the scoping note's §1.
  - §2: the init/shutdown contract in full — `SteamAPI_Init`'s exact success/failure branch (`0x00870cf0`), the pre-init `exit()` gate (`0x00d48390`), the `SteamAPI_Shutdown` LIFO-cleanup-list registration mechanism (`0x00870330`/`0x00dc43f0`/`0x00dc4410`), `SteamAPI_RestartAppIfNecessary`'s confirmed-unreachable status, and the one standing callback registration (`0x00870b20`, `P2PSessionRequest_t`).
  - §3: the per-frame contract (`0x00870db0`) — the mandatory `SteamAPI_RunCallbacks` pump versus the engine-side bookkeeping layered on top (request-queue drain, auto-open-overlay check).
  - §4: the on-demand call clusters (matchmaking/lobby, startup-adjacent request queue, cloud storage with its explicit null-check-and-soft-fail pattern, and the smaller single-purpose `SteamApps`/`SteamUtils`/`SteamUserStats`/`SteamNetworking` clusters), grouped as the scoping note mapped them.
  - §5: cross-references (not duplicates) to `spec-lua-api-behaviour.md` §41.2 and §44.2 for the already-specced Lua-binding-level Steam calls.
  - §6: 14 OPEN items carried forward from the scoping note's own unresolved threads, none closed in this pass by design (the brief was to spec what the note already found, not to re-derive further).
  - Self-check: 0/0 (strict regex and plain-substring backstop).
