# Saints Row: The Third — Agora Games "Hydra SDK" / GDS Telemetry Call Boundary (Game Side Only)

**Prepared by:** SPEC TEAM (cleanroom reverse-engineering process)
**Phase:** single-unit spec, written directly from a fresh game-call-site census performed in this same pass (no separate scoping note — judged, per the task brief's own steer, that this boundary is bounded similarly to `spec-steamworks-boundary.md`'s ~45-function/104-site scale, so it goes straight to spec form). No prior census of game→Hydra call sites existed before this pass; `scratchpad/middleware_census_2026-10-07.md` §7 identified the vendor and the exported surface only, and explicitly flagged "no game→Hydra-SDK call-site census has been done" as an open gap (its own §12/§13 follow-on notes repeat the same gap). This file closes that gap.
**Scope:** the game's own side of the call boundary into Agora Games' "Hydra SDK" — the vendor behind this project's previously-unidentified "GDS" telemetry symbols (`spec-output.md` §1.1's "~30 GDS-prefixed symbols... vendor not identified", resolved by the middleware census's retained-debug-path finding: `D:\projects\sr3\pc_rc\sr3\src\vendor\Agora Games (Hydra SDK)\agoraHttp\...`/`agoraBase\...`). Per the task's explicit, direct scope decision (relayed from the user): **Agora Hydra's own internals are not reverse-engineered here** — it is closed, proprietary middleware, and its real backend (`sr3.gds.thqconnect.com`, `sr3.hydra.agoragames.com`) is long dead, so there is nothing to gain from its internals. This file documents only: which Hydra/GDS-touching functions the game calls, from where, with what arguments, when (startup/shutdown/on-demand), and — the single most load-bearing question, since the real backend is gone — **what the game does when an HTTP request fails, times out, or the network/servers are unreachable.** This is the same shape of question `spec-steamworks-boundary.md` already answered for Steamworks' own call boundary, and §2–§5 below are deliberately parallel to that file's §2–§4.
**Explicit scope limit (binding):** Hydra SDK's own internals (`agClient`/`agRequestHandle`/`agLog`/`agArrayType`/`agHashType`/`agUTF8Type`/`agString` proper, libcurl's own transport internals, OpenSSL, the exact wire format sent to the backend) are **not** characterized here beyond the few bytes needed to show where the game-side wrapper's own observable behavior comes from — the same depth convention `spec-steamworks-boundary.md` §2.1 used for the stub `steam_api.dll`'s own export bodies. The real GDS/Hydra backend no longer exists; nothing here should be read as a claim about what the genuine live service used to do.
**FORBIDDEN sources (unchanged, obeyed):** no leaked Saints Row IV/Re-Elected source, no leaked Volition/Havok/Audiokinetic/RAD material, no other leaked material of any kind. `.czn_pc` is not touched (not relevant to this topic). Every Win32 API name quoted below (`InterlockedExchangeAdd`, `EnterCriticalSection`, `GetTickCount`, etc.) is read directly from this executable's own PE import table — Microsoft's own public, documented API — the same convention this project already applies to citing Steamworks' and Wwise's public API surfaces. The Steamworks interface/vtable-slot naming reused below (`SteamUser`, vtable slot `+0x8` = `GetSteamID`) is carried over by direct cross-reference from the already-published `spec-steamworks-boundary.md`, not re-derived from anything partner-gated.
**Method:** Ghidra-free this pass, per the task brief's stated preference — direct Capstone disassembly (x86, 32-bit) via a throwaway script following the same read-only pattern as `tools/harnesses/exe_va.py`, against the shipped `SaintsRowTheThird.exe` (DX9 build, the project's standing reference binary). Two census techniques, both modeled on `scoping-steamworks.md`'s own two-pass method: (1) a full PE export-table parse (`pefile`) to enumerate every `GDS_*` export with its exact address; (2) a byte-level census — not a linear disassembly sweep, to avoid any risk of instruction-boundary desync in a 12 MB `.text` section — that scans every file offset for the `0xE8` (`CALL rel32`) opcode, computes each call's absolute target, and matches it against a target-address set (the direct-call analogue of the Steamworks census's raw little-endian immediate scan against IAT slots; intra-module calls here are direct near calls, not through an import slot, so this is the correct adaptation of that method rather than a weaker substitute). A plain 4-byte LE scan (same idea as the Steamworks census's "register-cached reads" column) was run alongside it to catch non-`CALL` references (table entries, pushed function pointers); every hit it returned outside of confirmed data tables was manually verified by disassembling its context, and several turned out to be coincidental byte collisions inside unrelated binary data — called out explicitly below, the same way `scoping-steamworks.md` §2.4 called out its one coincidental hit for `SteamAPI_RestartAppIfNecessary`. No Ghidra project was opened for this pass.
**Confidence key:** **CONFIRMED** (read directly off a Capstone disassembly or a direct byte/structure read this pass); **HIGH** (follows from structure — call shape, a literal format string, a named Win32 API, or a PE-format guarantee such as zero-initialized BSS — but not traced to 100% certainty); **HYPOTHESIS** (plausible, not checked). Matches this project's usual convention.
**Relationship to other specs:** `scratchpad/middleware_census_2026-10-07.md` §7 is this file's starting point (vendor identity, exported-symbol count, debug-path/hostname/field-name evidence) — not re-derived here, only cited. `spec-output.md` §1.1 is the original "~30 GDS symbols, vendor unidentified" flag this chain resolves. `spec-steamworks-boundary.md` is cited directly at two points below: the exact same `SteamUser` import slot (`0x0101c54c`) and the exact same vtable-slot-`+0x8` `GetSteamID` call that file's §2.2 step 8 already documents are reused by Hydra's own init sequence (§2.2 below) — a genuine, confirmed cross-middleware dependency, not a coincidence of address proximity. `spec-physics-runtime.md`'s already-published Havok shutdown address (`0x007590d0`, per that file's own lifecycle list) is cited directly in §3.1 below to confirm this file's own shutdown-sequence placement claim.

---

## 0. Results up front

1. **The prior census's three example export names were wrong; the real export surface is narrower and entirely about one thing: a 16-byte tagged-value type.** `scratchpad/middleware_census_2026-10-07.md` §7 quoted `GDS_event_tag`, `GDS_server`, and `GDS_version` as examples "per the PE export table." A direct PE export-table parse this pass (both `fast_load` and full parse, cross-checked) finds **exactly 31 exports, all of the form `GDS_value_*`, at contiguous ordinals 169–199, address range `0x004d1bd0`–`0x004d2bb0`** — none of those three names is among them. They do exist as literal ASCII strings elsewhere in the binary (`"GDS_event_tag [tag]"`, `"GDS_server [server name]"`, `"GDS_version [version string]"`, at file offsets `0xd8ec18`/`0xd8ec64`/`0xd8ecf8`) — usage/help text for three small internal functions, not export names (§1, §6).
2. **The game-side boundary is small, fully censused, and funnels almost entirely through one shared function.** A byte-level `CALL rel32` census against every one of the 31 exports plus every address this pass identified as part of the wrapper layer finds **65 confirmed call sites into one shared "queue a telemetry record" function (`0x00401ca0`)**, from roughly 65 individually-coded, mostly near-identical game-side functions, plus a handful of shared infrastructure functions (init, shutdown, a response-processing state machine) — comfortably the same order of magnitude as `spec-steamworks-boundary.md`'s own ~45-function/104-site census (§4).
3. **Every one of the 65 on-demand call sites ignores its own return value.** Sampled across every site this pass read in full: none test, branch on, log, or otherwise react to what `0x00401ca0` returns. This is true fire-and-forget telemetry at the game's own call sites — the clearest possible version of "the game never blocks on this" (§4.1).
4. **A dual gate decides, before any work happens at all, whether a report call does anything.** 63 of the 65 on-demand functions open with a call to a shared check (`0x00403070`) that fails closed on either of two conditions: (a) a session-id sentinel pair at `0x013521c0`/`0x013521c4` reads as the "no session" value (`-1`/`-1`, the exact value shutdown resets it to); or (b) a "connected" flag at `0x031719f4` is zero — and that flag's storage is **CONFIRMED, by PE-format guarantee, to start at 0 on every process launch** (it lives in the zero-filled extension of `.data` beyond the section's raw image, so the loader itself zeroes it; no static initializer sets it otherwise). Until something sets it nonzero — this pass did not find a confirmed trigger for that — every one of these 63 call sites does nothing at all: no array is built, no `GDS_value_set_text` call happens, `0x00401ca0` is never even reached (§4.2, §5.2).
5. **A single HTTP 4xx response permanently disables all further reporting for the rest of the process.** Read directly, at two separately-reached but byte-identical call sites inside the game's own response-processing function: when an in-flight request's HTTP status code is in the client-error range `400`–`499` inclusive, the master readiness flag (`0x0031719bc`) is unconditionally set to `0xffffffff` (the same "not ready" value it holds before init ever runs) — and nothing in this pass found anywhere that ever sets it back to a ready state except the one-time, idempotency-guarded init call. **Status `200` triggers a response-decode step; everything else (notably the `5xx` server-error range, and presumably outright connection failure/timeout, not traced to the same certainty) resets per-request bookkeeping and continues without touching the readiness flag at all** — i.e., 4xx is treated as fatal-to-the-subsystem, 5xx/other is treated as a per-request miss (§5.1).
6. **Startup and shutdown are each a single, unconditional, once-per-process call, sitting inside the engine's already-documented serial subsystem bring-up/tear-down sequence** — the same shape `spec-steamworks-boundary.md` §2.2 found for `SteamAPI_Init`. Startup derives a client/session identifier from the first Ethernet/Wi-Fi network adapter's MAC address plus a direct call through the exact same `SteamUser` import slot and `GetSteamID` vtable position `spec-steamworks-boundary.md` §2.2 already documents (§2). Shutdown sits immediately after Havok's own shutdown call in the same teardown sequence (§3).
7. **Three apparent debug/console commands (`GDS_event_tag`, `GDS_server`, `GDS_version`) have zero confirmed call sites anywhere in the binary** — the exact same "no genuine use, by this method" finding `spec-steamworks-boundary.md` §2.4 reported for `SteamAPI_RestartAppIfNecessary` (§6).
8. **24 of the 31 exported `GDS_value_*` functions are never called as real functions by the game's own code at all.** The handful that are simple enough (bool/int32/int64/float32/float64/crc16/crc32/data setters) are hand-inlined at every call site instead of called — their trivial 2–4 instruction bodies are duplicated directly into the ~65 on-demand functions rather than invoked (§4.3).

---

## 1. The export census, and a correction to the prior pass

### 1.1 The real 31-export surface

CONFIRMED, direct PE export-table parse (both `pefile` fast-load and full-parse modes, cross-checked, identical results): the executable exports exactly 225 symbols total (matching `spec-output.md` §1.1's own count), of which exactly 31 begin with `GDS_`, at **contiguous ordinals 169–199** (no gaps), address range `0x004d1bd0`–`0x004d2bb0`. Every one of the 31 is of the form `GDS_value_<verb>` — a single tagged-value ("variant") type's full public C API: `init`/`done`/`copy`/`setref`, `get_type`/`set_type`, `get_isnull`/`set_isnull`, nine `get_<kind>`/`set_<kind>` pairs for `bool`/`int32`/`int64`/`float32`/`float64`/`crc16`/`crc32`/`data`/`text`, and three array-level operations: `encoded_byte_count`, `encode_values`, and `decode_i32`/`decode_value`/`decode_values`. This matches the middleware census's own retained source-path evidence (`agArrayType.cpp`/`agHashType.cpp`/`agUTF8Type.cpp` — exactly the module names you'd expect behind a typed-value system with per-kind array support) and is consistent with the census's field-name evidence (`profile.platform_account_id.guid`) — this value type is the payload container Hydra's HTTP layer serializes into request bodies.

### 1.2 Correcting `scratchpad/middleware_census_2026-10-07.md` §7's three example names

That census's §7 states, of the 31-export finding: *"re-confirmed exactly against `spec-output.md`'s earlier '~30' estimate — `GDS_event_tag`, `GDS_server`, `GDS_version`, `GDS_value_copy`, ... and more, per the PE export table."* The count (31, matching "~30") is correct and reproduced exactly by this pass's independent parse. **The first three names in that list are not exports.** They do not appear anywhere in the PE export directory (CONFIRMED — full parse, `NumberOfNames`/`NumberOfFunctions` both 225, cross-checked against the raw export-name string table itself). They exist only as three literal, null-terminated ASCII strings in `.rdata`, well away from the real export-name table: `"GDS_event_tag [tag]"` (file offset `0xd8ec18`), `"GDS_server [server name]"` (`0xd8ec64`), `"GDS_version [version string]"` (`0xd8ecf8`) — bracketed usage text, the exact shape of a console-command's help string, not a function's exported symbol. §6 below traces what actually references these three strings. This correction does not change the overall "31 exports, vendor identified" finding, which stands confirmed; it only retracts three specific example names from that prior pass's own write-up.

### 1.3 Which of the 31 exports the game's own code actually calls

Byte-level `CALL rel32` census against all 31 export addresses, scanned across the whole `.text` section raw range: **71 total direct call sites**, overwhelmingly concentrated on one export:

| Export | Address | Direct call sites | Calling context |
|---|---|---|---|
| `GDS_value_set_text` | `0x004d1e60` | **64** | Scattered across ~63 of the on-demand report functions (§4.2) — the one non-trivial setter (real strlen-or-explicit-length logic, clamped to `0xffffff` bytes), not hand-inlined like its siblings |
| `GDS_value_done` | `0x004d1bf0` | 1 | `0x004d1cab` — **inside** the exported block itself (`GDS_value_copy` calling `GDS_value_done` on an old ref) — SDK-internal, not a game call site |
| `GDS_value_get_crc32` | `0x004d1f80` | 1 | `0x00401d1a` — inside the shared queue wrapper `0x00401ca0` (§4.1) |
| `GDS_value_encoded_byte_count` | `0x004d2000` | 1 | `0x00401d2e` — same wrapper |
| `GDS_value_encode_values` | `0x004d2230` | 1 | `0x00401d83` — same wrapper |
| `GDS_value_decode_i32` | `0x004d2760` | 1 | `0x00401e58` — inside the response-decode function `0x00401e30` (§5.1) |
| `GDS_value_decode_value` | `0x004d2810` | 1 | `0x004d2bef` — **inside** `GDS_value_decode_values`'s own body — SDK-internal, not a game call site |
| `GDS_value_decode_values` | `0x004d2bb0` | 1 | `0x00401e95` — inside `0x00401e30` |
| **remaining 24 exports** | — | **0** | No confirmed call site anywhere (§1.4) |

### 1.4 The other 24 exports: not called, because their logic is hand-inlined instead

CONFIRMED by direct disassembly of several of the zero-call-site exports (`GDS_value_set_crc32` at `0x004d1e10`: a 5-instruction body, `mov eax,[esp+4]; mov ecx,[esp+8]; mov byte ptr[eax+8],7; mov dword ptr[eax],ecx; ret` — store the tag byte `7` and the raw value, nothing else). The same exact tag-byte-plus-raw-store pattern (tag `7` = crc32, tag `9` = text/string, by direct cross-reference to `GDS_value_set_text`'s own tag write) appears duplicated inline, dozens of times, inside the ~65 on-demand report functions' own field-building code (§4.2) — the compiler (or the original source, via an inline-forced setter) never bothered emitting a real call for anything simple enough to inline, leaving only the one setter with real branching logic (`GDS_value_set_text`'s strlen/clamp) as a genuine call target. This also explains why none of the nine `get_<kind>` exports have a confirmed call site at all: this binary is a telemetry **producer**, not a consumer — nothing in the game's own code ever reads a `GDS_value` back out through the public getters.

---

## 2. Startup contract

### 2.1 The game-side wrapper (`0x00bd9130`) and its single call site

CONFIRMED, direct disassembly. Called from exactly one site, `0x005d364e`, itself inside a long, linear sequence of other subsystem bring-up calls (each of the form `call <subsystem init>; call 0x00707800; test al,al; jne <bail>` — a shared "did this stage succeed" check used by *other* subsystems in the same sequence) at `0x005d3600`–`0x005d36d5`. The GDS/Hydra call itself is **not** wrapped in that shared success check — immediately after it, the code unconditionally calls the same generic warning/log sink `0x00754410` that `spec-steamworks-boundary.md` §2.2 step 10 already identifies as the function pointer Steamworks' own init passes to `SteamClient()->SetWarningMessageHook` — called here directly, twice in a row, with no branch on GDS/Hydra init's own outcome.

Full branch trace of `0x00bd9130` (CONFIRMED, direct listing):

1. If a master config flag byte at `0x013123d1` is clear, return immediately — do nothing (this flag's own writer was not traced in this pass; OPEN item 1, §7).
2. If an "active" flag byte at `0x029897a5` is already set, return immediately — idempotency guard, same shape as `spec-steamworks-boundary.md`'s own guard byte at `0x024d6800`.
3. **Set the "active" flag (`0x029897a5`) to `1` unconditionally, before the real init call below even runs, and never roll it back on failure.** If a sibling byte `0x029897a4` is clear, zero two 8-byte default-string fields (`0x02949788`, `0x02949790`).
4. Call a setup helper (`0x00db57a0`) that configures a fixed-capacity request-queue/buffer structure (arguments include a literal size `0x40000` and a capacity `0x20`) — not traced further; this is the boundary into Hydra's own internal queue machinery (out of scope per this file's own limit).
5. Call a registration helper (`0x00401720`) with four function-pointer-shaped arguments (`0x00bd7f80`, `0`, `0x00bd8910`, `0x00bd7f60`) — these addresses sit immediately adjacent to the three debug-command stub functions traced in §6, HIGH confidence this registers the `GDS_event_tag`/`GDS_server`/`GDS_version` console-command handlers (not traced to the registration table's own consumer).
6. Build four config-override arguments, each either a built-in empty default or a game-config value (`0x013123d4`, `0x013123cc`, `0x013123d8`, `0x013123c8` — not traced further, OPEN item 2, §7).
7. **Call the real init function, `0x004024d0`** (§2.2), passing those four arguments.
8. Call a post-init hook, `0x00710560` (not traced further, OPEN item 3, §7) — into game code, not Hydra's.
9. **Store the real init call's return value into a diagnostic field (`0x013123c4`) and never branch on it** — no retry, no rollback of the "active" flag set in step 3, no user-facing effect either way.

### 2.2 The real init function, `0x004024d0`

CONFIRMED, direct disassembly. Called from exactly one site (`0x00bd91dc`, inside `0x00bd9130` above).

1. Store the four caller-supplied arguments into globals `0x031719c0`/`0x031719c4` and two others used by later steps.
2. **Set the master readiness flag `0x031719bc` to `0xffffffff` ("not ready") unconditionally** — this is the exact same flag §5.2's failure path later resets to the identical value.
3. Call a client-identity helper, `0x004023a0` (traced far enough to characterize, not into its own internals): it walks the adapter list from an internal call at `0x00d1d56a` (inside the vendor code range `0x00d1d570`–`0x00d4c800` the middleware census already hypothesized as shared zlib/libcurl/Hydra territory — this call sits one instruction's worth of bytes below that range's own stated start, effectively the same region), filtering for interface type `6` (Ethernet) or `0x47`=71 (IEEE 802.11/Wi-Fi) and copying the first match's 6-byte MAC address into `0x013521d8`–`0x013521dd`. It then **calls the exact `SteamUser` import slot `spec-steamworks-boundary.md` §1's own census table rows under that exact address, `0x0101c54c`** and, if the returned interface pointer is non-null, makes a virtual call through vtable slot `+0x8` — the identical slot that file's §2.2 step 8 already identifies as `GetSteamID` — into a local output buffer; on a null interface, it falls back to two already-zeroed default dwords rather than dereferencing null. This is a genuine, confirmed cross-middleware dependency: **Hydra's own client-ID derivation calls into Steamworks**, not a coincidence of address proximity.
4. If a second helper (`0x00402880`) returns nonzero, **return `0xffffffff` (failure) immediately** — a real, reachable failure branch (not traced further what specifically can make it fail; OPEN item 4, §7).
5. If a third helper (`0x004018b0`, called with one of the caller's arguments) returns nonzero, **return `0xffffffff` (failure) immediately** — likewise a real, reachable failure branch, not traced further (OPEN item 5, §7).
6. **On success: set `0x031719bc` to `1` and call a final setup step (`0x00402f40`) — return `0`.**

**Contract summary:** `0x031719bc` only ever reaches `1` ("ready") via this exact path, and only on the success branch; every other path through this function — including both of the two untraced failure helpers in steps 4–5 — leaves it at `0xffffffff`. Since this function is called exactly once per process (guarded by the wrapper's own idempotency flag, §2.1 step 2), **if it fails for any reason on a given run, the readiness flag never becomes `1` for the rest of that process's life**, and (per §2.1 step 9) nothing anywhere notices or reacts to that failure.

---

## 3. Shutdown contract

### 3.1 The game-side wrapper (`0x00bd8110`) and its single call site

CONFIRMED, direct disassembly. Called from exactly one site, `0x005d3aac`, itself immediately after a sequence of other subsystems' own shutdown calls at `0x005d3a60`–`0x005d3aa7`, the first of which (`0x005d3a60 call 0x007590d0`) is the **exact same address `spec-physics-runtime.md`'s own already-published lifecycle list identifies as Havok's shutdown function** — confirming GDS/Hydra's shutdown runs immediately after Havok's own teardown, late in the engine's serial shutdown sequence. The caller's very next instruction after the call to `0x00bd8110`, `0x005d3ab1  jmp 0x00bce470`, tail-jumps into a later continuation not chased further (OPEN item 6, §7) — that jump is in the *caller*, not inside `0x00bd8110`'s own body.

```
0x00bd8110  if "active" flag (0x029897a5) == 0: return immediately (never initialized / already shut down)
0x00bd8119  call 0x00402530                          ; the real shutdown (below)
0x00bd811e  push 0; push 0; mov ecx, 0x029897f0; call 0x00db5e30   ; release the queue/buffer struct §2.1 step 4 set up
0x00bd812c  ret
```

### 3.2 The real shutdown function, `0x00402530`

CONFIRMED, direct disassembly. Called from exactly one site (`0x00bd8119`, above).

```
0x00402530  if 0x031719bc == 0: skip to ret (already shut down)
0x00402539  call 0x00402fe0     ; resets the session-id sentinel (below) and a "connected" bookkeeping table
0x0040253e  call 0x00401a30     ; not traced further (OPEN item 7, §7)
0x00402543  call 0x00402900     ; releases two held resource handles (0x031719e0, 0x031719e4) via calls into
                                    the vendor code range, then a further cleanup call 0x004026c0 and a
                                    tail-jump to 0x00403320 — not traced past this point (OPEN item 8, §7)
0x00402548  0x031719bc = 0       ; readiness flag -> "shut down" (distinct from the "0xffffffff = never
                                    succeeded / failed" value both §2.2 and §5.1 use)
0x00402552  ret
```

**`0x00402fe0` (CONFIRMED, direct read) is the function that resets the session-id sentinel pair §4.2/§5.2 depend on:**

```
0x00402fe0  0x013521c0 = 0xffffffff
0x00402fee  0x013521c4 = 0xffffffff   ; "no active session" sentinel — the exact pair §4.2's gate checks
0x00402ff8  0x031719f8 = 0            ; a "currently connected" bookkeeping flag, cleared
0x00402ff8  0x031719cc = 0
            [walks and frees a small hash table at 0x013521bc, same table shape 0x00403070 (§4.2) reads]
```

This is the single clearest piece of evidence for what §4.2's "no session" sentinel actually means: it is literally the value shutdown resets things to, i.e. "no live session," not an arbitrary magic number.

---

## 4. On-demand reporting — the shared queue wrapper and the ~65 report call sites

### 4.1 The shared queue/encode function, `0x00401ca0`

CONFIRMED, direct disassembly. **65 confirmed call sites** (byte-level census against this exact address, across the whole `.text` raw range) — the game-side funnel point every on-demand report eventually reaches.

1. `InterlockedExchangeAdd(&0x013521c8, 1)` (CONFIRMED by import-table name resolution of the call target, `0x0101c1b0`) — a thread-safe sequence-number generator; the fetched value becomes this call's own request ID.
2. **If the master readiness flag `0x031719bc` is not strictly greater than `0`, return `0xffffffff` immediately.** This is the gate §2.2/§5.1 set/clear; nothing past this point runs when it fails.
3. If the caller's second argument (a function-pointer-shaped value, HIGH confidence a completion callback — not confirmed to exact semantics, OPEN item 9, §7) is null, return `0xffffffff` immediately.
4. Allocate a request record via `0x00401c40`; if allocation fails, return `0xffffffff` immediately.
5. Populate the record's fields from the caller's remaining arguments.
6. **If the caller supplied a value-array pointer and a field count of at least 3:** call `GDS_value_get_crc32` (`0x004d1f80`) and `GDS_value_encoded_byte_count` (`0x004d2000`) on it, validate the resulting size is in a sane range (`1`–`0xfffe`), allocate a transport buffer via the generic allocator import cached at `0x013521e0`, and call `GDS_value_encode_values` (`0x004d2230`) into it.
   - **If the size check fails, or the allocation fails, or the encode call itself reports failure:** invoke the caller's own callback with a specific failure code (CONFIRMED distinct literal codes `0x100` and `0x200` seen at different failure points in this branch), release the record via `0x00401c80`, and return `0` (not `0xffffffff` — a different "it's done, unsuccessfully" return shape from the three earlier gate failures).
   - **On success:** store the encoded buffer and size into the record, call `0x00401b30` (hands the record to Hydra's own transport/queue — not traced past this boundary, out of scope) and return the sequence number from step 1.
7. **If the caller supplied no array (or fewer than 3 fields):** skip straight to invoking the caller's callback with code `0x200`, release the record, and return `0`.

**Every sampled call site into this function (§4.2) ignores the return value entirely** — no `test`/`cmp`/branch on `eax` follows any of the 65 call instructions this pass read. Whatever this function reports back — success, the three `0xffffffff` gate failures, or the two `0` degenerate-completion paths — the calling game code proceeds exactly as if it had called a `void` function.

### 4.2 The ~65 individual report functions

Byte-level census against `0x00401ca0` finds exactly **65 distinct call sites**, each inside its own small, mostly near-identical function, clustered in two shapes:

**Shape A — 63 functions, each gated by a hash-keyed existence/connectivity check.** Representative addresses and their embedded 32-bit constants (read directly, a small sample of the 63 — the remainder were confirmed to share the identical structural shape by spot-checking several more, not individually traced to completion, consistent with this project's own convention of not chasing every leaf of a large, structurally-uniform cluster):

| Entry function | Embedded constant | Builder/payload function | Calls `GDS_value_set_text`? |
|---|---|---|---|
| `0x00422fb0` | `0xafb6ad38` | `0x00423f70` | yes, at `0x00423fe5` |
| `0x00422ff0` | `0x5835ae68` | `0x00424390` | yes, at `0x0042440b` |
| `0x00423030` | `0xb81b1161` | (not individually traced further) | — |

Each opens with the identical idiom (CONFIRMED, read at every one of the three sampled):

```
mov ecx, <constant>
call 0x00403070        ; dual existence/connectivity gate, §4.2.1 below
test eax, eax
je <continue>           ; eax == 0 ("not found / not yet reported") -> proceed to build+report
xor eax, eax; ret       ; eax != 0 -> return immediately, nothing built, nothing sent
```

If the gate passes, the function reads a large number of fields (predominantly 32-bit floats, a handful of integers, one string) out of a caller-supplied game-object pointer into a local array of 16-byte value-slots (CONFIRMED stride and layout from direct reads: each slot stores its payload in the first 8 bytes and a 1-byte type tag at offset `+8` — matching `GDS_value_set_crc32`'s own body exactly, tag `7`; `GDS_value_set_text`'s own tag write, `9`, is the only slot built via a real call rather than an inlined store, §1.4). The sampled function at `0x00423f70` builds a 54-slot (`0x36`) array this way before tail-calling `0x00401ca0`; field counts were not confirmed identical across all 63. **Which named milestone, achievement, or gameplay event each of the 63 distinct constants corresponds to was not traced** — the constants read as opaque 32-bit values from this pass's vantage point, and resolving them would mean either finding a name→hash table (not located) or brute-forcing candidate strings, neither attempted (OPEN item 10, §7). What calls each of these 63 entry functions was spot-checked for exactly one of them (`0x00422fb0`, called from `0x00bd8d6f`, not traced further) and not chased for the rest (OPEN item 11, §7).

**Shape B — 2 reusable functions, no hash gate, driven by a caller-supplied object pointer directly:** `0x004051c0` and `0x00405410`. Both take a `this`-style pointer in `ecx`, build a small (4–8 field) array directly from its fields with no existence check beforehand, and call `0x00401ca0` (at `0x004052e2` and `0x0040546e` respectively). `0x00405410` is reached by a tail-jump from `0x00402e40` — a function that, under its own two-part gate (`if (arg==0) && (0x031719f8 != 0)`), walks a hash table identical in shape to the one `0x00403070` consults (§4.2.1) and, on a specific condition, sets the "connected" flag `0x031719f4` to `1` at its very end (`0x00402efb`). **This is the one write site this pass found for that flag** — HIGH confidence this is "mark the session as live/connected," but the exact triggering condition (what calls `0x00402e40`, and under what circumstance) was not traced (OPEN item 12, §7) — `0x00402e40` itself has zero confirmed `CALL`-site references anywhere in `.text` by this pass's census, the same "no confirmed caller found" result §6 reports for the three debug-command stubs, though for a function this substantive a tail-jump or indirect dispatch this method can't see is at least as plausible as dead code (unlike the debug stubs, not asserted dead here).

### 4.2.1 The dual gate, `0x00403070` — CONFIRMED, the single most important function in this file

```
0x00403070  eax = 0x013521c0
0x00403075  eax &= 0x013521c4
0x0040307b  cmp eax, 0xffffffff
0x0040307e  jne 0x00403084
0x00403080  return 0xffffffff          ; <<< "no session": both halves of the session-id pair are the
                                           -1 sentinel §3.2's shutdown-reset function writes
0x00403084  cmp dword ptr [0x031719f4], 0
0x0040308b  je 0x00403080               ; <<< "not connected": the flag §4.2 Shape B sets, defaulting to 0
0x0040308d  [... walks the hash table keyed by the caller's constant, looking for an existing entry;
             returns a nonzero "found" result, or inserts the entry and returns 0 ("not found, now
             recorded") — this half of the function is a genuine milestone-deduplication registry, not
             traced to field-level completion, not needed to be: its own callers only test for zero/nonzero]
```

**Both failure branches return the identical sentinel (`0xffffffff`) the caller's `test eax,eax; je continue` treats exactly the same as "already reported."** This function is doing two logically distinct jobs behind one shared short-circuit: "has this already been recorded" and "is there any point even trying" collapse into the same early-out. §5.2 states the direct consequence for a dead backend.

### 4.3 What the game does with any of this, once queued

Nothing past `0x00401b30` (§4.1 step 6's success path) was traced — that is the boundary into Hydra's own transport/queue machinery, explicitly out of scope per this file's own limit (the task brief: no internals RE). What *is* in scope, and traced in full, is the response side of that same request once Hydra's own machinery (on whatever thread/mechanism it uses — not located, see OPEN item 13, §7) reports an outcome back: §5 below.

---

## 5. Failure, timeout, and no-network behavior — the central question

### 5.1 The HTTP response-code dispatch — CONFIRMED, read directly at two call sites

A per-request polling/state-machine function spanning `0x00401f40`–`0x0040238f` processes in-flight requests through observed state values `0`, `2`, `3`, `4`, `5` (meanings beyond "4 = a response has arrived, go inspect its status code" not individually confirmed — OPEN item 14, §7). **This function itself has zero confirmed `CALL`-site references anywhere in `.text`** by this pass's byte-level census — the same "no confirmed caller found" result as the three debug stubs in §6, except here the function is substantive (over 1,000 bytes, real control flow, genuine thread-safety locking via `EnterCriticalSection`/`LeaveCriticalSection`, import slots `0x0101c1c4`/`0x0101c1c8`), making dead code far less plausible than for the trivial stubs in §6; HIGH confidence it is dispatched from inside Hydra's own internal worker thread (the binary imports `CreateThread`, `CreateMutexA`/`CreateMutexW`, `CreateEventA`/`CreateEventW` — none of which this pass traced into, since doing so would cross into Hydra's own internals) rather than from any per-frame game-side tick. **This is the one place `spec-steamworks-boundary.md`'s own "per-frame `SteamAPI_RunCallbacks` pump" shape does *not* have a confirmed analogue here** — flagged honestly as OPEN rather than asserted (OPEN item 13, §7).

Within the "response arrived" handling, read directly, twice, at byte-identical logic reached via two different paths through the same function:

```
0x00402017  eax = <this request's stored HTTP status code>
0x0040201c  cmp eax, 0x190            ; 400 decimal
0x00402033  jl 0x004020b4              ; eax < 400 -> not a 4xx, go reset-and-continue
0x00402035  cmp eax, 0x1f3             ; 499 decimal
0x0040203a  jg 0x004020b4              ; eax > 499 -> not a 4xx, go reset-and-continue
0x0040203c  EnterCriticalSection(&0x02cc7d48)
0x00402047  esi = 0x02cc7d40
0x0040204d  0x031719bc = 0xffffffff   ; <<< THE MASTER READINESS FLAG, KILLED
            [... walk/release a linked list of pending handlers at esi ...]
0x004020a9  LeaveCriticalSection(&0x02cc7d48)
0x004020b4  [reset per-request bookkeeping fields; call 0x00402c10; continue the state machine]
```

and, in the sibling path reached when the status is checked against the literal `0xc8` (200) first:

```
0x00402125  cmp eax, 0xc8             ; 200 decimal
0x0040212a  jne 0x0040215d
            [... status == 200: call the response-decode function, 0x00401e30 (which itself calls the
             exported GDS_value_decode_i32 at 0x004d2760 and GDS_value_decode_values at 0x004d2bb0);
             on decode failure, fall through to the same reset-and-continue path below; no readiness-
             flag change either way ...]
0x0040215d  cmp eax, 0x190            ; 400
0x00402162  jl 0x004021e7
0x00402168  cmp eax, 0x1f3            ; 499
0x0040216d  jg 0x004021e7
0x0040216f  EnterCriticalSection(&0x02cc7d48)
0x0040217a  esi = 0x02cc7d40
0x00402180  0x031719bc = 0xffffffff   ; <<< SAME KILL, reached via this second path
```

**Three-way outcome, CONFIRMED:**
- **Status `200`:** decode the response body (via the exported `GDS_value_decode_*` functions); on decode success or failure, continue normally. The readiness flag is never touched by this branch.
- **Status `400`–`499` inclusive:** the readiness flag `0x031719bc` is set to `0xffffffff` — the exact "not ready" value it holds before init ever succeeds (§2.2). **Nothing anywhere in this pass's census found a path that ever sets it back to a ready value except the one-time, idempotency-guarded `0x004024d0` init call (§2.2) — which runs exactly once per process.** A single 4xx response therefore permanently disables every subsequent call into `0x00401ca0` (§4.1 step 2) for the remainder of that process's life: every later report silently returns `0xffffffff` at its very first real check, before building anything or encoding anything.
- **Everything else** (notably the `5xx` server-error range, and any status value outside `0`–`599`): falls through to the generic "reset this request's bookkeeping and continue the state machine" path (`0x004020b4`/`0x004021e7`) — no readiness-flag change, i.e. treated as a transient, per-request miss rather than a subsystem-ending event.

**What a genuine connection-level failure (DNS resolution failure, connection refused, TCP timeout — i.e., no HTTP response at all) maps to in this state machine was not traced to the same certainty.** The dispatch above only runs once a request reaches the "response arrived" state (`4`); this pass did not locate the specific state transition (or distinct state value) that a connection-level failure produces before any status code exists to inspect. Given the fully generic reset-and-continue path already handles "got a response I don't specifically recognize" without disabling the subsystem, and given no crash, hang, or blocking wait was found anywhere in this pollset's own logic, the most defensible statement is: **a connection-level failure is HIGH confidence to be handled by the same non-fatal reset-and-continue path as a 5xx response** (both are "this one request didn't work, move on"), but this is not CONFIRMED to the instruction level the 4xx path is (OPEN item 15, §7).

### 5.2 The practical consequence for a dead backend, stated plainly

Putting §2.2, §4.2.1, and §5.1 together, for Team B's actual situation (no reachable `sr3.gds.thqconnect.com`/`sr3.hydra.agoragames.com`):

1. **Every one of the 63 Shape-A report functions (§4.2) checks, before doing any work at all, whether a session is considered live** (`0x00403070`, §4.2.1). On a fresh process, before anything ever sets the "connected" flag `0x031719f4` nonzero (§4.2 Shape B's own trigger was not fully traced, but the flag's zero-initialized **default** is a PE-format guarantee, not a guess — §0 point 4), **every one of these 63 call sites is a guaranteed, two-instruction, side-effect-free no-op.** No array is built, no string is copied, no HTTP request is ever attempted by the game's own calling code at this layer.
2. **Even for the 2 Shape-B functions and anything that does reach `0x00401ca0`**, the master readiness flag `0x031719bc` only ever becomes `1` via a fully successful `0x004024d0` init (§2.2) — itself dependent on two untraced-but-real failure branches (OPEN items 4–5, §7) plus a dependency on `SteamUser()` resolving (dead on the GOG stub build per `spec-steamworks-boundary.md`'s own finding that every Steamworks accessor on this build trivially succeeds — but a genuine failure there falls back gracefully, §2.2 step 3, not a crash).
3. **If a request ever does get far enough to receive a real HTTP response, and that response is anything in the 400–499 range** (a very plausible outcome for a request to a long-dead or deregistered hostname, once DNS/connection succeeds against whatever now occupies that name, if anything) **— the entire reporting subsystem permanently disables itself for the rest of the process, silently, with no further effect anywhere.**
4. **At no point in any of this does the game branch, retry, log a user-visible message, crash, or block.** The worst-case behavior, in every path this pass traced, is "the call does nothing." A reimplementation or no-op stub of this boundary does not need to simulate any server behavior at all to be faithful to what the shipped game actually does when the real backend is unreachable — it only needs to let the existing gates (§2.2, §4.2.1, §5.1) continue failing closed exactly as they already do.

---

## 6. Three debug/console commands — apparently dead code

CONFIRMED, direct disassembly, cross-checked against the literal format string `"GDS event tag is '%s'"` (file offset `0x118fc2c`, read directly from `.rdata`, confirming the first pair below is a real "print the current value" / "print usage" console-command pair, not a coincidental address grouping):

| Command | "Print current value" function | "Print usage" function (the fallback on wrong arg count) |
|---|---|---|
| `GDS_event_tag` | `0x00bd7f0f` (logs via `0x006f68b0` with format string `0x118fc2c`) | `0x00bd7f2c` (`push 0; push "GDS_event_tag [tag]" (0x118fc18); call 0x00754410`) |
| `GDS_server` | `0x00bd87b3` | `0x00bd87d0` (`"GDS_server [server name]"`, `0x118fc64`) |
| `GDS_version` | `0x00bd88de` | `0x00bd88fb` (`"GDS_version [version string]"`, `0x118fcf8`) |

**Every one of these six addresses has zero confirmed call sites anywhere in the binary** — neither a direct `CALL rel32` nor (after manually verifying every raw 4-byte-pattern hit this pass's secondary scan returned) a genuine function-pointer table reference. The handful of raw-pattern hits that did turn up for the three "print usage" addresses sit inside clearly non-code byte sequences (disassembling the surrounding bytes as instructions produces nonsensical sequences like `pop gs`/`scasd`/`rol byte ptr [edi+0x390000bd],1` in a row) — coincidental 4-byte collisions inside unrelated binary data, the exact same kind of false positive `spec-steamworks-boundary.md` §2.4 identified and ruled out for its own single `SteamAPI_RestartAppIfNecessary` hit. §2.1 step 5 found a registration call (`0x00401720`) passed four addresses immediately adjacent to these six (`0x00bd7f60`, `0x00bd7f80`, `0x00bd8910`), HIGH confidence that *is* the real registration path for these commands into whatever console/debug-command table consumes it — but that consumer table itself was not located, so "registered, but the dispatcher that would ever call them back was not found" is the most defensible statement, not "provably dead," consistent with how this project treats an unresolved absence of evidence (OPEN item 16, §7).

---

## 7. OPEN items

1. The master config flag `0x013123d1` that gates the entire init wrapper (§2.1 step 1) — its own writer not traced; whether it's a build-time constant, a config-file value, or a runtime toggle is unknown.
2. The four config-override values the init wrapper reads (`0x013123d4`, `0x013123cc`, `0x013123d8`, `0x013123c8`, §2.1 step 6) — not traced to their own source.
3. The post-init hook `0x00710560` (§2.1 step 8) — not traced; into game code, not Hydra's.
4. The real init function's first untraced failure helper, `0x00402880` (§2.2 step 4) — what specifically can make it fail is unknown.
5. The real init function's second untraced failure helper, `0x004018b0` (§2.2 step 5) — likewise unknown.
6. The shutdown wrapper's tail-jump target, `0x00bce470` (§3.1) — a further continuation, not chased.
7. Shutdown's `0x00401a30` (§3.2) — not traced.
8. Shutdown's `0x00402900` cleanup chain and its own tail-jump to `0x00403320` (§3.2) — not traced past the two resource-handle releases this pass did confirm.
9. The exact semantics of `0x00401ca0`'s second argument (HIGH confidence a completion callback, §4.1 step 3) — not confirmed to exact calling shape or when/whether it's ever actually invoked by Hydra's own transport layer after a successful queue.
10. What named milestone/achievement/gameplay event each of the 63 Shape-A functions' embedded 32-bit constants corresponds to (§4.2) — no name→hash table was located; resolving this would need either that table or a brute-force string search, neither attempted.
11. What calls each of the 63 Shape-A entry functions (spot-checked for exactly one, `0x00422fb0` ← `0x00bd8d6f`, not chased further or for the remaining 62) — i.e. the exact gameplay trigger for each milestone report.
12. The exact trigger condition for `0x00402e40` setting the "connected" flag `0x031719f4` (§4.2 Shape B) — this pass found the one write site but not what calls the function that contains it (itself zero confirmed callers by this census, though HIGH confidence real/reachable given its substance, unlike §6's stubs).
13. What dispatches the response-processing state machine (`0x00401f40`–`0x0040238f`, §5.1) at all — zero confirmed game-side callers found; HIGH confidence it runs on an internal Hydra worker thread (the binary does import `CreateThread`/mutex/event primitives) rather than a per-frame game tick, but not traced into Hydra's own thread-management code, consistent with this file's own scope limit.
14. The full meaning of the per-request state machine's observed values `0`, `2`, `3`, `4`, `5` (§5.1) beyond "4 = response arrived, inspect status code."
15. The exact state-machine path a genuine connection-level failure (DNS failure, connection refused, timeout — no HTTP response at all) takes (§5.1's closing paragraph) — HIGH confidence it shares the generic 5xx-style reset-and-continue path rather than the 4xx kill path, not confirmed to the instruction level.
16. Whether the three debug/console commands (§6) are genuinely unreachable on this build, or dispatched through a registration-table mechanism this census method can't see (the registration call `0x00401720` was found; its own consumer was not).

---

## 8. Clean-room check

- Addresses and offsets are plain hex throughout, everywhere in this file. No Ghidra auto-names (`FUN_*`/`DAT_*`/`LAB_*`/`iVar*`/`local_*`/etc.) appear anywhere below, and no decompiler output — pseudocode expression, auto-named local, or decompiler-specific variable — was read, pasted, or quoted anywhere in this pass. Every function and global is cited only by its plain address or by a named offset from one; behavior is described in prose, numbered steps, and short literal instruction listings transcribed directly from Capstone's own mnemonic/operand output (not a decompiler).
- Names quoted as data: the 31 `GDS_value_*` export names are read directly from this executable's own PE export-name table — the vendor's own public API surface, the same basis `spec-steamworks-boundary.md` cites Steamworks' flat-C export names on. The three literal strings quoted in §1.2/§6 (`"GDS_event_tag [tag]"`, `"GDS_server [server name]"`, `"GDS_version [version string]"`, `"GDS event tag is '%s'"`) are read directly from this executable's own static `.rdata`. The Win32 API names quoted (`InterlockedExchangeAdd`, `EnterCriticalSection`, `LeaveCriticalSection`, `GetTickCount`, `CreateThread`, `CreateMutexA`/`CreateMutexW`, `CreateEventA`/`CreateEventW`) are read directly from this executable's own PE import table — Microsoft's own public, documented Win32 API, the same convention this project already applies to citing any other public vendor API surface. `SteamUser` and the `GetSteamID` vtable-slot naming are carried over by direct citation from `spec-steamworks-boundary.md`'s own already-published findings, not re-derived.
- Self-check result, run on the finished file as the last step before publishing: the project's standard strict word-bounded regex for Ghidra auto-named decompiler variables, stack/register-input names, and auto-generated function/data/label names returned **0 hits**; the plain-substring backstop for the same token families (run separately, since the regex's word-boundary behavior can silently under-match a token immediately followed by a digit) also returned **0 hits**. **0/0.**
- No other spec file was edited by this pass. No Ghidra session was opened for this pass at all — every address, byte sequence, and instruction-level claim above was read via direct Capstone disassembly and `pefile` PE-structure parsing against the shipped `SaintsRowTheThird.exe`, following the same read-only pattern as `tools/harnesses/exe_va.py`; no project file was created or modified.

---

## Changelog

- **2026-10-07 — Initial version** (SPEC TEAM). New file, written directly from a fresh census performed in this same pass — no prior game-call-site census of this boundary existed (`scratchpad/middleware_census_2026-10-07.md` §7/§12/§13 identified the vendor and exported surface only, and explicitly flagged the call-site census as an open gap). Judged, per the task brief's own steer, bounded similarly enough to `spec-steamworks-boundary.md`'s ~45-function/104-site scale to go straight to spec form rather than a separate scoping note. Contents:
  - §1: the real 31-export `GDS_value_*` surface (contiguous ordinals 169–199), a correction to the prior census's three bogus example export names (`GDS_event_tag`/`GDS_server`/`GDS_version` are literal usage strings tied to apparently-dead debug commands, not exports — §6), and the 71-site call census against all 31 exports (64 to `GDS_value_set_text`, the rest internal-only or inside the shared queue wrapper).
  - §2: the startup contract in full — the game-side wrapper `0x00bd9130` (single call site, inside the engine's documented serial bring-up sequence) and the real init function `0x004024d0`, including its confirmed cross-middleware dependency on the exact `SteamUser`/`GetSteamID` call `spec-steamworks-boundary.md` already documents.
  - §3: the shutdown contract in full — `0x00bd8110`/`0x00402530`, confirmed to run immediately after Havok's own shutdown via direct cross-reference to `spec-physics-runtime.md`'s already-published address, and the session-sentinel reset (`0x00402fe0`) that defines what "no session" means for §4.2's gate.
  - §4: the on-demand reporting pipeline — the shared queue/encode wrapper `0x00401ca0` (65 confirmed callers, every sampled caller ignoring its return value), the two structural shapes of report function (63 hash-gated "milestone" functions, 2 generic reusable ones), and the dual existence/connectivity gate `0x00403070` that is this file's single most load-bearing function.
  - §5: the failure/timeout/no-network section — the HTTP status-code dispatch read directly at two call sites (200 = decode; 400–499 = permanently kill the master readiness flag; everything else = reset and continue), and the practical, plainly-stated consequence for a dead backend (§5.2).
  - §6: the three apparent debug/console commands, found to have zero confirmed call sites — the same "no genuine use by this method" shape `spec-steamworks-boundary.md` §2.4 already found for `SteamAPI_RestartAppIfNecessary`.
  - §7: 16 OPEN items, none closed in this pass by design (this pass's brief was the game-side boundary and its failure behavior, not Hydra's own internals).
  - Self-check: 0/0 (strict regex and plain-substring backstop).
