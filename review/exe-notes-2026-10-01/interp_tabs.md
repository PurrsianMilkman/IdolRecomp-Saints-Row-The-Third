# Interpretation of bridge job `20261001T165637-team-a-tabs` — §27/§28 residual OPEN items (the next-dump lists of `rederive_27.md` and `rederive_28.md`)

Team A, 2026-10-01. Source: CrreishDump output under `D:\Crreish-sync\for-team-a\bus-results-team-a\results\20261001T165637-team-a-tabs\`
(job file `jobs/20261001T165637-team-a-tabs.json`), three steps:

- `func1/` — `func` mode, depth 1, 40 root bodies (the job asked for 40; every root is present per `func1/index.txt`): the six
  function groups of `rederive_27.md` "Next dump" and the eight groups of `rederive_28.md` "Next dumps".
- `func2/` — `func` mode, depth 1, the three bodies of `rederive_28.md` item 9: `0x00a281e0` (§4.5 vehicle-instance resolver)
  and its two halves `0x004dcf00` / `0x0062a190`.
- `glob/` — `xref` mode (cap 200) for the twelve globals the 25-use cap truncated in jobs pwgq/jxxz: `0x02289c28`,
  `0x02289b94`, `0x02282d10`, `0x02282d14`, `0x014c2d10`, `0x024d78e0`, `0x012fcadc`, `0x0290ceca`, `0x0229a317`,
  `0x014ff6c1`, `0x014ff6c2`, `0x012f3b48`.

This note continues `rederive_27.md` and `rederive_28.md` (same folder): each numbered question below is one residual OPEN
sub-item those two notes left in `spec-lua-api-behaviour.md` §27/§28 (and the collateral §2.10 / §4.5 / §1.12 / §4.7
items they named). Nothing settled there is re-derived here unless said so.

Labels: **CONFIRMED — disassembly** = read in a listed instruction stream or decompile of these dumps; **CORRECTED** = the
spec's current wording is wrong and the dump shows what is right; **HIGH CONFIDENCE** = follows from a dumped instruction or
reference list but the body it points at was not dumped; **HYPOTHESIS** = plausible reading, not settled; **OPEN** = not
settled, listed in the final "what to dump next".

Dump caveats are noted per question. Citations are `func1/func_<addr>.txt`, `func2/func_<addr>.txt`, `glob/xref_<addr>.txt`.

---

## Q1. §2.10 / §27.4 — the receiver of `0x008788e0` at `0x006df33a`, and what `audio_conversation_play` actually does

### Q1.1 `0x00a3c9d0` = `audio_conversation_play(id)` is a two-call wrapper — CONFIRMED — disassembly

Body (`func1/func_0x00a3c9d0.txt`, `0x00a3c9d0`–`0x00a3c9f7`; registered by the gameplay registrar `0x00a20840`, pair at
`0x00a20bdf`): argument count, the number at `-n`, truncation toward zero (`0x00ea2596`), then **`0x006df210`(id, 3)**, no
return values. The id reaches the callee as its low byte; the second argument is the literal flags byte 3. Nothing else.

### Q1.2 `0x006df210`(id, flags): the opcode-`0x51` path is a host-gated network record, and the `0x008788e0` receiver is the conversation screen-context pointer `0x014e9ad0` — CONFIRMED — disassembly; CORRECTED (§2.10's "UI-command-queue message")

Depth-1 callee in the same file (`0x006df210`–`0x006df369`, 22 callers), in order:

1. session = `0x0087ba20`() (the Lua-visible singleton `0x024d8534`). If a session exists, **its local member is its host member
   (`+0x5c` == `+0x58`)** and flags bit `0x2` is set: open a record with opcode `0x51` (`0x0086f5f0`), append the literal byte 3,
   append the id byte (both through `0x00881040`), commit with **`0x0086f1b0`(session, 0, 0)** — a broadcast to every session
   member, see Q6 — and close it (`0x0086eb20`). So the §2.10 desk-review reading is right and the old text is wrong: **the
   opcode-`0x51` path is a network record behind a host gate, not an internal UI-command-queue message.** The literal 3 is a
   constant in this body, not the flags value (it is stored before the flags are tested). **[CORRECTED — disassembly.]**
2. `0x006de8a0`() → a conversation record R (not dumped — it is a depth-2 callee; it takes no stack argument, so it reads the id
   from a global or returns the current conversation). If R exists: byte R`+0x11` = 1; and if byte R`+0x10` is non-zero,
   `0x0070ba80`() then `0x0046a3f0`(byte R`+0x1`). Neither callee dumped.
3. If flags bit `0x2` is set: **`0x008788e0`(this = the pointer held in the global `0x014e9ad0`, id)**. That is the receiver
   §2.10 and §27.4 left OPEN. `0x014e9ad0` is a zero-filled runtime pointer (its own reference block is in the dump): written by
   `0x006de8e0` (stores a freshly obtained pointer) and cleared to 0 by `0x006de620`, tested for null by `0x006dee20`, and read by
   `0x006de990`, `0x006debc0`, `0x006dec90`, `0x006df210`, `0x006df4f0` — all in the same `0x006dexxx` conversation cluster, and four
   of those sites (`0x006debe2`, `0x006ded1f`, `0x006dede1`, `0x006df5e7`, in the `0x008788e0` reference list) are further
   `0x008788e0` calls on the same pointer. So **the conversation subsystem owns one screen-context object of the same kind as
   §27.3/§27.4's `completion_screen` context, and `audio_conversation_play` sets that context's state byte to the conversation
   id** through the same state-change method that `Completion_user_is_done_viewing` calls with state 1. Flags bit `0x1` is never
   read by this body. **[CONFIRMED — disassembly for the receiver, the argument and the gates; HIGH CONFIDENCE that `0x014e9ad0`
   points at a record of the `0x024d78e0` screen-context ring — its creator `0x006de8e0` was not dumped and is not among that
   ring's referencing functions (Q8.2), so it obtains the record through a helper.]**
4. `0x00d2f500`() (the `return 1` stub of §27.15) and return.

Consequence for §2.10: the "direct `0x008788e0` fallback call with the conversation id" is neither a fallback nor a parallel of
the record: the record (host only) tells the other machines, and the context call (always, when a context exists) changes the
local screen context's state to the id. The guard inside `0x008788e0` means an id of `0x81` (129, or −127 after truncation)
is silently ignored by that step.

### Q1.3 `0x008788e0`(this = context, state) re-read: the wire record is opcode `0x1a` to every eligible member; the "type-4 message" is the context's own event dispatch — CONFIRMED — disassembly (refines §27.4)

Body (`func1/func_0x008788e0.txt`, `0x008788e0`–`0x00878a46`; thiscall, pops one 4-byte argument, reads it as a byte; 30+
callers across the cutscene (`0x0072bf90`–`0x0072d660`), conversation and lobby code). Let D = the record at ctx`+0x4`.
If byte D`+0x2` (enabled) is zero, or the state is `0x81`: return. Otherwise: byte D`+0x0` = state; open a record with
opcode **`0x1a`**; append **the context's own first byte** (ctx`+0x0` — the id byte that `0x00878ca0` matches against the low
byte of the name hash, §27.3); append the value **4 as an 8-bit field** (`0x00881110`); append the state byte;
**`0x008779b0`(this = ctx, &record)**; the "Changed your state to %u." log goes to the bare-return stub `0x00754410`; then
**`0x00877860`(this = ctx, 4, S`+0x5c`)** with S = the session at ctx`+0x1c` (the local member as subject); close.

So §27.4's one sentence "sends a type-4 message carrying it through 0x008779b0/0x00877860" is two different things: the
**wire record** is opcode `0x1a` = {context id, event 4, state}, sent member by member by `0x008779b0`; and `0x00877860` is
the **local event dispatch** of event 4 ("a member's state changed", subject = the local member) to the context's own
subscribers. Both CONFIRMED; the event-4 name is HIGH CONFIDENCE from the two uses.

### Q1.4 `0x008779b0` and `0x00877860` — CONFIRMED — disassembly

- **`0x008779b0`(this = ctx, record)** (`func1/func_0x008779b0.txt`): S = ctx`+0x1c`; if none, return. Walk S's member ring from
  S`+0x54` through `+0xb28c`; for each member M with **byte M`+0x161` non-zero and byte M`+0x0` zero**: `0x0086f110`(this = record,
  M, 0, 0, 0) — the single-target send of Q6. Six callers, all context methods in `0x00877a00`–`0x00879b60`. `+0x161` is the byte
  after jfue's "announced" flag `+0x160`; its meaning and that of the member's byte `+0x0` (0 here, "≠ 1" in the broadcast of
  Q6) are HYPOTHESIS: `+0x0` is a member state with 1 = the local member, 0 = a connected remote member.
- **`0x00877860`(this = ctx, mask, subject)** (`func1/func_0x00877860.txt`): ctx`+0x24` holds **four 12-byte subscriber slots**
  {callback, event mask, user argument}; for each slot whose callback is set and whose mask shares a bit with `mask`:
  callback(ctx, mask, subject, argument), cdecl. Eighteen callers in the same cluster. Who subscribes to the conversation
  context is in code not dumped.

---

## Q6. `0x0086f1b0` is a whole-session broadcast and `0x0086f110` the single-target send — CORRECTED — disassembly (the spec's "local-only / local-dispatch sibling" label is the wrong way round)

Bodies: `func1/func_0x0086f1b0.txt` (`0x0086f1b0`–`0x0086f2e1`, thiscall on a record object, pops 12 — three arguments) and
the sibling `0x0086f110` as a depth-1 callee in `func1/func_0x008779b0.txt` (`0x0086f110`–`0x0086f1ad`, thiscall, pops 16 —
four arguments). The helpers `0x00881040`, `0x00881110`, `0x0086ec80`, `0x0086f5f0`, `0x008743b0`, `0x00874240`, `0x00bc55a0`,
`0x00bcdb00` are all read; `0x00874290` and `0x008741c0` (the per-member send primitives) were not dumped (`maxfuncs`).

**The record object** (from the helpers): `+0x0` buffer, `+0x4` capacity in bytes, `+0x8` byte cursor, `+0xc` index of the byte
currently being bit-filled, byte `+0x10` bit index 0–7 inside it, `+0x20` current bit position, `+0x24` header descriptor
(its `+0x14` bit 0 selects the send primitive), `+0x28` pointer to the 16-bit length field inside the header. `0x0086f5f0`(this,
opcode) initialises the object (`0x008812c0`) and writes the header (`0x0086edc0`(opcode, 0)). `0x00881040`(src, n) appends n
whole bytes at the cursor (null when they do not fit); `0x00881110`(src, nbits) appends the whole bytes through it and then the
remaining bits one at a time, least significant first. `0x0086ec80` writes the length field as ceil(bitpos/8) − 3 (a 3-byte
header), or 0 when the record has no capacity or no descriptor. **[CONFIRMED — disassembly.]**

**Both commits first test `0x00bc55a0`()** = (global `0x029443bc` == 1). When true, neither touches the session: the length is
patched and the record's bytes go to `0x00bcdb00`(buffer, bytes) → `0x00bcd910`(0, buffer, bytes). `0x029443bc` is a runtime
dword set to 1/2/3 by the `0x00bc4fd0`–`0x00bc6d80` cluster (40 uses); HYPOTHESIS: a replay/demo recorder, 1 = recording, with
every commit diverted into the recording. Otherwise:

- **`0x0086f110`(this = record, target, a, b, c)**: needs `+0x24` and `+0x28`; patch the length; descriptor flag set →
  `0x00874290`(target, record, a, b, c); clear → `0x008741c0`(target, record). **One addressee.**
- **`0x0086f1b0`(this = record, session, filter, argument)**: needs a session, `+0x24` and `+0x28`; patch the length. **No
  filter:** flag set → `0x008743b0`(session, record), which calls `0x00874290`(M, record, 0, 0, 0) for **every member M of the
  ring at session`+0x54`** whose byte `+0x0` ≠ 1; flag clear → `0x00874240`(session, record), the same loop with
  `0x008741c0`(M, record). **With a filter:** the same loops, restricted to members with byte `+0x0` == 0 for which
  filter(M, argument) returns true. **Every member of the session.**

So the two siblings end in the same per-member primitives; the only difference is **one addressee versus the whole session**,
and nothing in either body is "local". The label the spec carries in dozens of places (§4.13, §9.21, §15.29, §22.21, §22.28,
§27.1, §28.11, §28.15 and the §9.2/§9.15/§10.6 census — "`0x0086f1b0` is the local-only / local-dispatch sibling, `0x0086f110`
the genuine multi-target commit") should read: **`0x0086f1b0` = broadcast the record to every member of the given session
(what a host does after a host-gated record); `0x0086f110` = send the record to one member (what a client does toward the
host or owner, and what `0x008779b0`'s per-member loop does).** The host gates recorded throughout the spec are unchanged; in
single player there is no session, so `0x0086f1b0` returns without acting and every "no replication in single player"
consequence stands. The descriptor bit that chooses `0x00874290` over `0x008741c0` is HYPOTHESIS: reliable versus unreliable
delivery (five arguments versus two). **[CORRECTED — disassembly for both bodies; the primitives' own behaviour is OPEN.]**

---

## Q2. §27.13 / §27.14 / §27.21 / §27.22 — the four player-record operations: the records are the Steam friends list

The accessors §27 already read (`0x007c93b0`, `0x007c93e0`, `0x007c9410`, `0x007c9440`) bounds-check a slot against the
count `0x02289b94` and index the array `0x02289c28` with stride `0x20c`. The two reference listings (`glob/xref_0x02289c28.txt`,
`glob/xref_0x02289b94.txt`) are now uncapped and short: **the array base is referenced only by those three accessors and by
`0x007c9710` (pushed as an argument), and the count is read only by the four accessors and written only by `0x007c9470`
(two stores)**. So `0x007c9470`/`0x007c9710` are the one filler of the list **[CONFIRMED — xref; their bodies not dumped]**.
What the four operations do with a record R shows what the records are: R`+0x80` is an 8-byte Steam identity (Q2.4), and
R`+0x88` is a flags dword (Q2.3). **The records are the local user's Steam friends** **[HIGH CONFIDENCE]**.

### Q2.1 `0x0088e690`(R, flags) behind `game_send_pause_menu_player_invite` — a queued session invite — CONFIRMED — disassembly

`func1/func_0x0088e690.txt` (`0x0088e690`–`0x0088e76b`, one caller `0x007c93b0`). Returns a code; `0x007c93b0` reports
"code == 0", so the Lua result is true only on the 0 path.

1. `0x0086fde0` → `0x008703c0` (Steam user present and logged on, §27.19) false → **1**.
2. S = `0x0087ba30`() = **the global `0x024d853c`** — a *third* session pointer beside `0x024d8534` (Lua-visible) and
   `0x024d8538` (mode manager, jfue). Its reference block is in the dump: set by `0x0087ba40` (the setter jfue listed), zeroed by
   the subsystem init `0x0087d8a0` and by the destroy `0x0087ed70` when the destroyed session is this one. No S → **1**.
3. Walk S's member ring (S`+0x54`, link `+0xb28c`): if any member's identity at M`+0x142` equals R`+0x80` (`0x00877750`, an
   8-byte compare) → **2** (that friend is already in the session).
4. Pop a node from the free list `0x024e4bcc` (link at node`+0x8`); none → **1**. Node = {R, flags, next = 0}; append it to the
   tail of the pending list `0x024e4bc8`.
5. If the byte `0x024e4b97` is clear, run the pending list now (`0x0088ded0`, Q2.1a). Then `0x0088e5b0`(R) (Q2.1b). → **0**.

(a) **`0x0088ded0`()** — drain the pending list: `0x00d34d20`() (not dumped; by its neighbours a `return 1` stub, HYPOTHESIS),
S = `0x024d853c`; while the list has a head and `0x024e4b97` is clear: if Steam is logged on, S exists, `0x0087c8b0`(this = S, 0)
is true (not dumped) and **S's settings byte `+0x4e` bit `0x40` (the offline bit, jfue) is clear**, call
**`0x0086ff70`(R`+0x80`, flags, S)** — the actual invite (not dumped; HIGH CONFIDENCE by its arguments: Steam id, flags, session);
in every case pop the node and return it to the free list. So invites are dropped silently for an offline session.
`0x024e4b97`, `0x024e4bc8` and `0x024e4bcc` are written otherwise only by `0x0088de50` (the pool initialiser, by shape).

(b) **`0x0088e5b0`(R)** — the throttle table: 12-byte entries {id low, id high, deadline} at `0x024e4bd0`, count `0x024e4bd8`,
capacity `0x024e4bd4`. If R's id is already present, re-arm that entry's deadline to now + 15,000 (`0x00d9e3c0`, the tick
clock `0x01320d9c`, milliseconds by jfue) and return; else, if the table is full, `0x0088e560`(this = table, 0) (not dumped);
then append {id, now + 15,000} if there is room.

### Q2.2 `0x0088dfd0`(R) behind `game_can_send_player_invite` — "not invited in the last 15 seconds" — CONFIRMED — disassembly

`func1/func_0x0088dfd0.txt` (`0x0088dfd0`–`0x0088e022`, one caller `0x007c93e0`, which tail-calls it, so its boolean is the Lua
result). For every entry of the throttle table: if its id equals R`+0x80` and `0x00d9e400`(&deadline) — the "expired" test
§27.11 established (deadline ≥ 0 and reached by `0x01320d9c`, wrap-safe over a 900,000,000 window) — is **false**, return
false. Otherwise true. So **the function answers only "no invite to this friend is still inside its 15-second window"**; it
does not look at Steam, the session or the record's flags, and it is true for an empty table.

### Q2.3 `0x0088e040`(R) behind `game_main_menu_join_friend_in_progress` — join the friend's lobby if they are in this game — CONFIRMED — disassembly (structure); HIGH CONFIDENCE (Steam call identities)

`func1/func_0x0088e040.txt` (`0x0088e040`–`0x0088e068`; callers `0x007c9410` and undefined code at `0x0088b4d5`, which is the
mode-manager step `0x0088b4d0` jfue read as "`0x0088e040`(`0x024e48d0`) true → 1 else 2" — so the mode manager retries the same
join on a record held at `0x024e48d0`). Returns false when the byte `0x024e4b96` is set (written by `0x0088dcf0`, read by
`0x0088e390`/`0x0088e410`; HYPOTHESIS: "a join is already in progress"), when R is null, or when **R`+0x88` lacks bit `0x4` or
bit `0x2`** (the friend's presence flags; which is "online" and which "in a game" is OPEN). Otherwise it tail-jumps to
`0x0088dd80` with R in a register.

`0x0088dd80`: Steam logged on; the matchmaking and friends interfaces exist (imports `0x0101c548`, `0x0101c570`); call the
friends interface's slot `+0x20` with (R's id, &info) — the "what game is this friend playing" query, which fills a 24-byte
record. Require success, **the record's first dword masked to 24 bits == `0xd7be` (55230, this game's Steam application id)**,
and `0x00877670`(this = &info`+0x10`) true (the lobby identity at `+0x10` is valid — not dumped, HIGH CONFIDENCE); then
**`0x00870700`(&lobby id)** — join that lobby (not dumped; HIGH CONFIDENCE from the flow) — and return true. On any failure
after the interface checks: `0x0086fce0` → `0x00870370`(1) (not dumped; a failure notice by position) and false.

So the Lua function returns **true only when the friend is currently in a Saints Row: The Third lobby and the join request was
issued**; it never waits for the join.

### Q2.4 `0x00870810`(&id) behind `game_show_coop_gamercard` — the Steam overlay profile page — CONFIRMED — disassembly

`func1/func_0x00870810.txt` (`0x00870810`–`0x00870833`; its only reference is the jump stub `0x0086fe40` that `0x007c9440`
calls): friends interface slot `+0x50` with (**the literal "steamid"**, id low, id high) — the overlay-to-user call with the
"steamid" dialog, i.e. open the Steam overlay on that user's profile. **[CONFIRMED — the literal and the id; the overlay call's
name is HIGH CONFIDENCE from the dialog string.]** Nothing is returned to Lua.

### Q2.5 Summary for Team B

| Lua name | engine body | result |
|---|---|---|
| `game_send_pause_menu_player_invite(slot)` | queue a session invite for friend `slot` through the third session pointer `0x024d853c`; drained at once unless `0x024e4b97` is set; dropped if the session is offline | true iff queued (Steam logged on, session exists, friend not already a member, pool node free) |
| `game_can_send_player_invite(slot)` | throttle table lookup | false only while an invite to that friend is < 15 s old |
| `game_show_coop_gamercard(slot)` | Steam overlay "steamid" dialog on the friend's id | none |
| `game_main_menu_join_friend_in_progress(slot)` | if the friend's flags `+0x88` have bits `0x2` and `0x4`, Steam says they are in app 55230 with a valid lobby, join it | true iff a join was issued |

In single player with no session, the first returns false (code 1), the second true, the fourth depends only on Steam; none
touch game state. **OPEN:** the invite send `0x0086ff70`, the join `0x00870700`, the friend-list filler `0x007c9470`/
`0x007c9710`, the flag bits of R`+0x88`.

---

## Q4. §27.9 / §28.20 — `0x00853ea0`(object, force, keep) is the object destroy primitive — CONFIRMED — disassembly (raises §27.9's "release primitive" and §28.20's "destroy/detach" from HIGH CONFIDENCE)

`func1/func_0x00853ea0.txt` (`0x00853ea0`–`0x00853f28`, cdecl, 30+ callers across every object kind). Helpers `0x008addb0`,
`0x004571e0`, `0x008537b0`, `0x00457730` read; `0x00884760`, `0x00457130`, `0x00457010`, `0x004bf550`, `0x00457030` not dumped.

1. **Replication guard.** If the object exists, its class row (`0x02cc9900`[byte `+0x34`]) has bit `0x2` at `+0x6`, the network
   module byte `0x024d4461` is 1, `0x008addb0`(object, 1) is false (it forwards the object's `+0x3c` record to `0x00884760`
   and answers true for a null object — the "single gate" of §28.1), the `+0x3c` record exists and **`force` is 0: return
   without doing anything.** So in a session a replicated object that this gate rejects is not destroyed locally unless the
   caller forces it. HYPOTHESIS for the gate's meaning: "this machine owns the object".
2. **Category list removal.** `0x004571e0`(object, &tag) is a category test on the object's byte `+0x32`: the top three bits
   must equal the tag's top three bits and the low five bits must share at least one bit. With tag `0x22` (at `0x011645cb`)
   → kind 1; else with tag `0x24` (at `0x011645cc`) → kind 2; else skip this step. For kind n: `0x008537b0`(this = the object
   table `0x02442750`, object, n) rebuilds the tag as (1 << n) | `0x20`, re-tests it, calls `0x00457130`(this = table, object,
   &tag, 0) (removal from the category's list, by shape), and if the object is the table's `+0x27a0` cursor advances that
   cursor to the object's `+0x28` link (or clears it when the link equals the sentinel `0x00457010`(this = table, tag `0x01`)
   returns); then `0x004bf550`(this = table). So byte `+0x32` = category byte (group in the top three bits, `0x20` = the group
   both tags belong to; which kinds `0x22`/`0x24` name is OPEN).
3. **Destroy.** `0x00457730`(this = table, object, keep): under the critical section `0x031d1510` (`0x00d9f620`/`0x00d9f630`
   enter/leave by shape); unless the object is null or equals table`+0x4`: bits `0x2` and `0x4` of byte `+0x33` must be equal
   (both clear = live, both set = already destroyed; mixed = in progress → return 0). If both are clear: **set bit `0x2`, call
   the object's virtual at vtable `+0x28` (its teardown), then byte `+0x33` = (old & ~1) | (keep & 1) | `0x4`.** In either
   case `0x00457030`(this = table, object, tag `0x01295b8f`) (hand it to the table's list for that tag — the free/dead list by
   position) and return 1.

So `+0x33` bit `0x2` = "destruction begun", bit `0x4` = "destroyed", bit `0x0` = the caller's `keep` flag; and the liveness
test `0x00853b10` the spec uses everywhere ("non-zero for a dead handle", §1.1) is HIGH CONFIDENCE a read of these bits (not
dumped). Both §27.9 (`0x005f8670`: the garage preview vehicle, (object, 0, 0)) and §28.20 (`0x00853ea0`(node, 0, 0) on a
matching child item) therefore **destroy** the object, not merely detach it; neither forces past the replication guard. Bit
`0x10` of `+0x33` (the spec's other liveness gate) is not touched here. **[CONFIRMED — disassembly; the identities of the
category tags, of `0x00884760`'s answer and of the table list helpers are OPEN.]**

---

## Q5. §27.12 — the autosave: `0x006f8370` and `0x00b94ff0`, and the three gate globals — CONFIRMED — disassembly (identity of one flag OPEN)

### Q5.1 `0x006f8370`() is a bare read of the byte `0x014f71d4` — CONFIRMED; its meaning OPEN

`func1/func_0x006f8370.txt`: one instruction. The byte's reference block (uncapped, 7 uses): set to 1 by `0x006f98e0`,
written from a register by `0x006f9a20`, read by `0x006f9250` (the routine jfue found inside the join-in-progress sync's
sub-record 1), `0x006f9b60`, `0x006fac60`, `0x0083b600`, and through this getter by 22 callers — eleven of them in one function
`0x007112d0`, the rest in `0x007108a0`–`0x00711c80` and the save code `0x00b991c0`. So it is a flag of the `0x006f9xxx`
subsystem that both the join sync and the autosave consult; its name needs `0x006f98e0`/`0x006f9a20`. **[OPEN — identity.]**
For Team B: `game_autosave` is also refused while this byte is set.

### Q5.2 `0x00b94ff0`() performs or defers the autosave — CONFIRMED — disassembly

`func1/func_0x00b94ff0.txt` (`0x00b94ff0`–`0x00b95058`; callers `0x00b94510`, `0x00a52b10`, `0x00b95060`, undefined code at
`0x00614800`, `0x006d068b`, `0x00a261c9`, and two data references in `0x0061b740`). In order:

1. Byte `0x0290cedc` set → return (written by `0x00b94650`/`0x00b946c0`). Byte `0x0290ceca` == 1 **and** byte `0x0290cecb` set →
   return. Byte `0x0290ceda` set → return.
2. `0x0086fdd0` → `0x008703b0`: the Steam user interface must exist (§27.19's body: pointer non-null; not "logged on").
3. **Perform now** only if byte `0x02282d1c` is clear **and** the fade state `0x0059f9f0`() (the global `0x012e6aa8` of §26.24)
   is not 1 **and** `0x0059f9c0`() is not 1.0 — that helper returns 0.0 when `0x012e6aa4` == 2 (faded in, §26.24's `sfx_faded_in`
   test) and 1.0 otherwise. Then `0x00b96230`(callback = `0x00b94d50`) and return.
4. **Otherwise defer:** byte `0x0290ceda` = 1 and return. `0x0290ceda` is read and cleared by `0x00b94f70`, whose own
   reference set (`0x0290ceda`, `0x02282d1c`, `0x0086fdd0`, `0x0059f9c0`, `0x00b96230`) is this function's — the retry that fires
   the deferred autosave once the screen is back **[HIGH CONFIDENCE; not dumped]**.

`0x02282d1c` has **no writer in the static binary** (two reads, `0x00b94f70` and here) — it sits right after the dialog ring
anchors `0x02282d10`/`0x02282d14` and pool `0x02282d18` (Q7), so it is either always zero or written through a pointer
**[HIGH CONFIDENCE: inert]**.

`0x00b96230`(callback): needs the Steam user interface; `0x00b949f0`(this = a 0x5c-byte local) and `0x00b95ff0`(&local, 0)
(both undumped; by their reference blocks `0x00b95ff0` resets the slot dword `0x0130f5d8` to `0xffff`); then only if
`0x0130f5d8` == `0xffff` and the callback is set: if `0x00b952d0`(1) is false, show the warning dialog
`SAVELOAD_AUTOSAVE_DEVICE_FULL` under `MENU_TITLE_WARNING` (`0x0084a1b0` twice, `0x007c3de0`) and return; else **call the
callback** — the autosave proper, undefined code at `0x00b94d50` (not dumped; it reads `0x0290ceca`). So the "device full"
notice is the only user-visible failure of `game_autosave`.

### Q5.3 The three gate globals (uncapped listings) — CONFIRMED — xref

- **`0x012fcadc`** (file value 1): cleared to 0 by undefined code at `0x007af5fd`, set to 1 by `0x007af660` and `0x007af6c0`, read
  only by the gate `0x00b95060`. **Autosaves are disabled from start-up until the undefined code at `0x007af5fd` runs**, and
  re-disabled by the two `0x007af6xx` routines (the `0x007aexxx`–`0x007afxxx` range is the lobby / game-start code jfue
  mapped; HYPOTHESIS: cleared when a game starts, set on return to the front end).
- **`0x0290ceca`**: set to 1 by `0x00b944c0`, written by `0x00b94510` (which also writes `0x0290cecb` and calls `0x00b94ff0`),
  read by `0x00b945d0`, `0x00b94680`, `0x00b94690`, `0x00b94a10`, undefined `0x00b94d50`, `0x00b94ff0`, `0x00b95060` — all in the save
  subsystem. HYPOTHESIS: "a save is in progress", with `0x0290cecb` = "and it is an autosave".
- **`0x0229a317`**: set to 1 by `0x0071b640`, written by `0x007dab20`, cleared by undefined code at `0x007de2e3`; read by the
  pause-menu/UI routines `0x007d8c80`, `0x007d98b0`, `0x007dba10` (a Lua binding — it calls the argument-count primitive),
  `0x007dd700`, `0x007df8a0`, `0x0083c040`, and the gate. HYPOTHESIS: a menu-open style flag. **[OPEN — name.]**

---

## Q3. §27.5 — the camera-preset selection in `0x00a9b080` and the row lookup `0x00a9afd0` — CONFIRMED — disassembly (structure); CORRECTED (entry stride and where the record comes from); the final apply call OPEN

Bodies: `func1/func_0x00a9b080.txt` (`0x00a9b080`–`0x00a9b3e1`, cdecl, four parameters; callers `0x00820810`, `0x00819810`,
`0x0081fa80` = `vcust_set_camera_pos`, `0x00a9b670` twice) and `func1/func_0x00a9afd0.txt` (`0x00a9afd0`–`0x00a9b070`, cdecl,
one parameter; callers `0x00a9b080`, `0x00a9b420`). Helpers read: `0x0080b2f0`, `0x00d9e8b0`, `0x00da38e0` (a 3-float → 16-byte
vector copy), `0x004cd810` (partially: a per-component "within ±1e-5 of zero" mask); not dumped: `0x004cd740`, `0x00568de0`,
`0x004cd2d0`, `0x00e23310`.

### Q3.1 `0x00a9afd0`(definition) — the vehicle's row of the global camera table — CONFIRMED

The camera table is a runtime array of **`0xad0`-byte rows at `0x027b3470`, count `0x027b3474`** (allocated by undefined code at
`0x00a9ae5f`, freed by `0x00a9aec0`; both OPEN). Each row has its **name hash at `+0xacc`**, its **preset count at `+0xac8`**, and
its presets from `+0x0`. Given a vehicle definition D (the `+0xbf4` record of the vehicle object): key = D`+0x18` (the hash of
the vehicle's name — the `plane_fighter02` test in Q3.2 compares exactly this field against that name's hash), unless D`+0x8fc`
differs from the engine-wide "none" sentinel `0x029c9964` (790 uses), in which case the override D`+0x8fc` is the key. The row
whose `+0xacc` equals the key is returned; failing that, the row whose hash is that of the literal `"default"`; failing that,
**row 0**. The decompiler misreads the second scan as a comparison with the receiver register; the listing compares against
the local that just received the `"default"` hash. Null only when the table does not exist. So §27.5's "record reached through
the object's `+0xbf4` pointer" is right about the input and wrong about the output: the record is **a row of a global table
keyed by the vehicle definition's name (or its `+0x8fc` override)**, with a `"default"` fallback. **[CORRECTED.]**

### Q3.2 `0x00a9b080`(vehicle, hash, unused, flag) — what the preset does — CONFIRMED — disassembly (geometry); apply call OPEN

1. Vehicle V null → return. row = `0x00a9afd0`(V`+0xbf4`); no row or zero preset count → return.
2. **Presets are `0x3c` bytes apart, not `0x1c`**: each holds **two `0x1c`-byte variants** at `+0x0` and `+0x1c` and the preset's
   **name hash at `+0x38`**. The loop stops at the first preset whose hash equals the second argument. **[CORRECTED — §27.5's
   "0x1c-byte stride".]**
3. Variant index v = `0x0080b2f0`() — 1 when `0x00e23310`(1.0) returns less than 1.0, else 0 (`0x00e23310` belongs to the §26.26
   display-mode family `0x00e23000`–`0x00e23910`; HYPOTHESIS: the two variants are the wide and the 4:3 placements) — **forced to 0
   when D`+0x18` equals the hash of `"plane_fighter02"`** (a hard-coded exception for that one vehicle). E = preset + v·`0x1c`.
4. **Camera position P**: take the horizontal direction from the point R = the 3-vector at `0x013c8740`/`44`/`48` to the
   vehicle's position V`+0x40`/`+0x44`/`+0x48` (the middle component is zeroed before normalising; the normalisation is a
   reciprocal square root with one Newton step), then **P = V.pos − E[`+0x0`] · that unit direction, with P's middle (vertical)
   component = V.pos.y + E[`+0x10`]**. R is a runtime vector of the `0x00564c20`–`0x00571b10` cluster (239 uses; writers
   `0x00564c20`, `0x00565a40`, `0x00566620`, `0x00566a40`); HYPOTHESIS: the current camera position, so the preset keeps the
   present bearing and sets distance and height.
5. Two more 16-byte vectors are built from E[`+0x4`], E[`+0x8`], E[`+0xc`] through `0x004cd810` and `0x004cd740` (the look-at
   and up vectors by position; the arithmetic is not followed here — `0x004cd740` was not dumped). **[OPEN.]**
6. flags = 0 when byte E`+0x18` is set; otherwise **`0x1e` when the fourth parameter is non-zero, else the global `0x01300544`**
   (file value 500 = `0x1f4`; used as an AND mask by `0x0080ba30`, `0x0080bdb0`, `0x0080c500`, `0x00a9b420`). The **third parameter
   is never read** — consistent with §27.5's stale-slot zeros. Then **`0x00568de0`(&P, &vec1, &vec2, flags, E[`+0x14`] as a
   float, hash == hash("Standard"))** — the camera placement proper (not dumped; HIGH CONFIDENCE as the apply call from its
   arguments). Only one preset is applied; the loop then continues to the end without effect.

For §27.5: `vcust_set_camera_pos(name)` hashes the name, finds the vehicle's camera-table row, picks the preset of that name
and its display-aspect variant, and places the camera E[`+0x0`] units from the vehicle along the current horizontal bearing at
height offset E[`+0x10`], with the "is Standard" flag passed to the apply call. **[CONFIRMED — disassembly for everything but
the two orientation vectors and the apply call, which stay OPEN.]**

---

## Q7. §27.20 / §27.11 — dialog creation (`0x007c3d80` → `0x007c3310`, `0x007c18a0`, `0x007c2ac0`) and the ring initialiser `0x007c4c90`; the `0x02282d14` ring is the free-slot list — CONFIRMED — disassembly (closes §27.11's last OPEN)

Bodies: `func1/func_0x007c3d80.txt`, `func1/func_0x007c3310.txt`, `func1/func_0x007c18a0.txt`, `func1/func_0x007c2ac0.txt`,
`func1/func_0x007c4c90.txt` (root bodies; helpers `0x007c2a60`, `0x007c26c0`, `0x00e22a30` read; `0x007c1640` not dumped) and
the uncapped listings `glob/xref_0x02282d10.txt` (60 uses, 12 functions) and `glob/xref_0x02282d14.txt` (33 uses, 9 functions).

### Q7.1 The pool, from `0x007c4c90` (one caller: the systems initialiser `0x005d2400`) — CONFIRMED

- **Four dialog slots** of `0x184` bytes at `0x02282d40`, `0x02282ec4`, `0x02283048`, `0x022831cc`, linked into a ring through
  `+0x4` (next) / `+0x8` (prev) and headed by **`0x02282d14` = the free-slot ring**. `0x02282d10` (the open-dialog ring) = 0.
- **The element pool `0x02282d18`**, one ring (links `+0x8`/`+0xc`) holding: four **body-text blocks** of `0x840` bytes at
  `0x02283350`–`0x02284c10`; three more `0x840`-byte blocks at `0x02285480`, `0x02285cc0`, `0x02286500` plus a block at
  `0x02286d40` and two `0x18`-byte nodes at `0x02285450`/`0x02285468` (taken by the sibling allocators `0x007c1980`/`0x007c1aa0`,
  not dumped); and **sixteen button blocks** of `0x22c` bytes in four `0x8b0`-byte groups from `0x02287580` to `0x02289840`.
  Each allocator takes only nodes inside its own address range (`0x007c18a0`: [`0x02283350`, `0x02285450`); `0x007c2ac0` and
  `0x007c4820`: [`0x02287580`, `0x02289840`)), so one ring serves several block kinds.
- Then the UI document named `dialog` is loaded in the foreground mode (`0x007b1cb0`(name, 1), the same call jfue read for
  `tutorial`), the live document is found by name (`0x00e1f2f0`) and its `+0x580` stored into `0x02282d20`; the UI object
  `dialog_populate` is looked up (`0x00e1e7e0`), its `+0x254` cached in `0x02282d24`, and **the engine callback `0x007c45b0` is
  installed in its `+0x2a0`**; `0x02282d28` is zeroed. So the dialog subsystem is initialised once with the other systems,
  exactly like the tutorial subsystem.

### Q7.2 `0x007c3310`(title, priority, flags, arg4) — take a free slot and insert it into the open ring by priority — CONFIRMED

Pops the head of the free ring `0x02282d14` (null → return 0: **at most four dialogs can be open**); sets its title through
`0x007c2a60` (encodes the UTF-16 title with `0x00e22a30` into a 256-byte local and hands it to `0x007c1640`(this = slot, &text,
flags)); slot`+0x130` = priority, `+0x120` = flags (bit `0x40` added when arg4 is 0), byte `+0x13b` = 0, `+0x180` = arg4; then
**inserts the slot into the ring `0x02282d10` in front of the first slot whose `+0x130` is lower**, so the ring is sorted by
descending priority and **its head is the highest-priority open dialog**; an empty ring gets the slot as head. Returns the
slot. This settles §27.11: `0x02282d10` = the open dialogs ordered by priority (head = topmost), **`0x02282d14` = the free pool
that every close path appends to** — the HYPOTHESIS is CONFIRMED (and the uncapped listings show both anchors are touched only
by the `0x007c1ec0`–`0x007c5910` dialog code).

### Q7.3 `0x007c18a0`(this = slot, text) and `0x007c2ac0`(this = slot, label, a, b) — body text and buttons — CONFIRMED

- `0x007c18a0`: takes the first pool node in the body-text range, unlinks it, calls its virtual `+0x4` with the slot, appends
  it to the slot's element ring at `+0x110` (tail), encodes the text into node`+0x10` (`0x830` bytes) and sets byte slot`+0x124`
  = 1 (rebuild needed). Returns the node, or 0 when no text block is free.
- `0x007c2ac0`: the same for a button block: `0x007c26c0`(this = node, slot, 0, 0) resets it (`+0x4` = slot, two 256-byte strings
  at `+0x10` and `+0x110` cleared, `+0x214`/`+0x218`/`+0x21c` = 0, byte `+0x220`/`+0x228` = 0, `+0x224` = −2), links it into the
  same `+0x110` ring, byte `+0x210` = a, encodes the label into node`+0x10`, byte `+0x211` = b, **slot`+0x0` += 1** (the button
  count), slot`+0x124` = 1. Nothing happens when no button block is free (no return value).
- `0x00e22a30`(dst, capacity, utf16): the text encoding — a lead byte `0x80`, then per UTF-16 unit a tag byte (1, plus 2 if the
  low byte is zero, plus 4 if the high byte is zero, with zero bytes replaced by `0xff`) and the two bytes, closed by 8, 0.
  A zero-byte-safe form of the UTF-16 text. **[CONFIRMED.]**

### Q7.4 `0x007c3d80`(title, body, priority, resultCallback, buttonLabel) — the notice dialog of §27.20 — CONFIRMED

`0x007c3310`(title, priority, 0, 1) → slot or 0; `0x007c18a0`(this = slot, body); `0x007c2ac0`(this = slot, label or the default
pointer at `0x01301a80` → the UTF-16 "OK" at `0x01164094`, 0, 0); slot`+0x13c` = resultCallback. Returns the slot. So §27.20's
reading stands with the parameter roles fixed: **the third parameter is the priority (0 for the sign-in notice), the fourth
the `+0x13c` result callback, the fifth the single button's label**; the fifth defaults to "OK" and the default pointer
`0x01301a80` is rewritten by `0x0084a390` (localisation load, by position). Thirty-odd callers, including the autosave warning
path's sibling `0x007c3de0` (Q5.2) which builds a two-button variant through the same three calls.

---

## Q8. The remaining §27 globals, uncapped — CONFIRMED — xref

### Q8.1 `0x014c2d10` — the minigame singleton (§27.1, §27.25, and §10.1's mode query) — 46 uses in 9 functions

`glob/xref_0x014c2d10.txt`: **written only by undefined code at `0x006a3683` (the creator) and cleared by `0x006a4440`**; read by
`0x006a33b0`, `0x006a3900` (§27.1's getter), **`0x006a3910`** (§10.1's "mode query whose value 8 disables the mechanic" — it reads a
field of this object, so that "mode" is a minigame state), `0x006a3990`, `0x006a3fa0`, `0x006a4130`, `0x006a44c0`, undefined
code at `0x006cbe2d`, and a block of undefined code `0x006a3630`–`0x006a4423` (the minigame's own update). So the cat-and-mouse
minigame object exists only while that activity runs; every reader null-checks it. **[CONFIRMED — xref; the creator's body
is OPEN.]**

### Q8.2 `0x024d78e0` — the screen-context ring (§27.3) — 22 uses in 11 functions

All in the context cluster `0x008777a0`–`0x00879b60`: writers `0x008777a0` (two stores), `0x008778b0`, `0x008780a0` (two),
`0x00878bc0`; readers `0x00878ca0` (the by-name finder), `0x00878ce0`, `0x00878db0`, `0x00879870` (jfue: the slot-table builder
called by the session "open"), `0x00879920` (jfue: called after a member is linked), `0x00879b60`. So contexts are registered
and removed by `0x008777a0`/`0x008778b0`/`0x008780a0` (HIGH CONFIDENCE as the register/unregister pair — not dumped), and the
conversation context's creator `0x006de8e0` (Q1.2) is not a direct writer: it goes through one of them.

### Q8.3 `0x014ff6c1` / `0x014ff6c2` — the live / system-link session-type bytes (§27.23, §27.24) — 5 uses each, complete

Writers: the two setters `0x007027b0`/`0x007027d0` (each sets its own byte and clears the other) and **`0x00703f50`, which
clears both from a zeroed register** — the same routine that calls `0x008788e0` at `0x00704132` (Q1.3's reference list), i.e. a
session leave/reset that also changes a screen context's state. Readers: the getters `0x007027f0`/`0x00702800` (the OR of the
two is `game_is_...` of §…7898) and a data reference inside the `0x007031b8`/`0x007031c1` stretch next to the friendly-fire
setter. **[CONFIRMED — the complete writer set.]**

### Q8.4 `0x012f3b48` — the horde end action (§27.8) — 3 uses, complete

Written only by the setter `0x006e4600` (`horde_results_set_end_action`); **read only by `0x006f1f80`** (two reads) — the one
consumer of the value, HIGH CONFIDENCE the horde results handler (not dumped). File value 2.

### Q8.5 `0x012fcadc`, `0x0290ceca`, `0x0229a317` — see Q5.3.

---

## Q9. §28.11 — what "recycle" (`0x006fd4f0`) does to the object; `0x006fbc90` — CORRECTED (it respawns the object at a fresh random entry point of the flow; it is not returned to a pool)

Bodies: `func1/func_0x006fd4f0.txt` (`0x006fd4f0`–`0x006fd8a4`, thiscall on a debris-flow row, one argument = the entry index;
callers `0x006fd8b0` (§28.11's path) and `0x006fda40`), `func1/func_0x006fbc90.txt`. Depth-1 callees read: `0x006fc520`,
`0x006fc430`, `0x00da5930`, `0x00dab6a0`, `0x00da6320`, `0x006fcb00` is **not** among them (the dump stopped at six: fourteen
callees undumped, including every vehicle-side helper named below).

### Q9.1 The row's fields seen here — CONFIRMED

`0x006fbc90`(this = row, id low, id high) returns the index of the entry whose id pair matches, scanning **`0x18`-byte entries
from row`+0x40`, count row`+0x48`**, only while byte row`+0x6e9` (flow active) is set; −1 otherwise or for a null pair. `0x006fc430`
(this = row, &out) gives the flow's **origin**: the runtime default triple `0x029cdb98`/`9c`/`a0` (the same three §28.2 found
zero in the image) when inactive; otherwise row`+0x4`/`+0x8`/`+0xc`, plus the position of the **anchor object whose id pair is at
row`+0x6e0`/`+0x6e4`** when it resolves (`0x00458230` on `0x024433a8`/`0x031d152c`, `+0x33` bit `0x10` clear, class bit `0x8` at `+0x6`,
alive). Row`+0x10` is the flow **axis** (a direction), row`+0x1c`/`+0x20` the **minimum and maximum radius**.

### Q9.2 `0x006fd4f0`(this = row, index) — CONFIRMED (structure); identities of the vehicle helpers OPEN

1. Index must be in [0, count). O = `0x006fc520`(id pair, 0): resolve through `0x00458230`, require `+0x33` bit `0x10` clear, class bit
   `0x8` at `+0xa`, alive; then O = its virtual `+0x70` (the instance cast). Null → return.
2. **New position**: p = origin (Q9.1); build an orthonormal frame from the axis (`0x00da5930`: third row = axis, first row = a
   perpendicular from (axis.z, 0, −axis.x), second = their cross product, with a fixed frame when the axis is nearly vertical);
   **rotate it about the axis by a random angle in [0, 2π)** (`0x00dab6a0`(0, 6.2831855) — one draw of §26.27's random ring —
   then `0x00da6320`); **p += a perpendicular row of the rotated frame × a random radius in [row`+0x1c`, row`+0x20`]** (second ring
   draw). If `0x006fcb00`(this = row, &p) rejects the point, push it a further 4 units along that same direction and retry, at
   most five times. **[CONFIRMED — disassembly; "radial direction" HIGH CONFIDENCE from the frame row used; `0x006fcb00` not
   dumped — a placement-validity test by shape.]**
3. **Move the object there.** P = `0x00b71950`(O), Q = `0x00b71970`(O) (neither dumped).
   - If P exists and P`+0xbf8` holds an object V: `0x00ab89a0`(P) → a saved byte, `0x00ac04a0`(P) → a flag f (if f: `0x00ac0e10`(P, 0));
     V's virtual `+0x60`, then W = V's virtual `+0x70`. **If W is null the entry's id pair is zeroed and the function returns
     without the final step** (the object is dropped from the flow). Else `0x00ab98f0`(W, saved byte); if f, `0x00ac0c90`(W,
     &copy of W`+0x58`/`+0x5c`/`+0x60`, 0); **hook slot 11** of the array at V`+0xb0` (`0x0069ccf0`(this = V`+0xb0`, &11) — `+0xb0` is
     the vehicle hook base of §21.1/§28.17): if a callback is registered, `0x00e0cfb0`(this = callback, `0x00a3a140`(W)) fires it
     with W's name; then **`0x00a874f0`(W, p.x, p.y, p.z, W`+0x4c`, 0, 0)** — the teleport with the current orientation (HIGH
     CONFIDENCE) — and `0x00a77d70`(W, 300) (§…5852's "(vehicle, duration)" helper).
   - Else, if Q exists: Q`+0x130` must exist (its virtual `+0x60` is called), then Q's virtual `+0x44`(&p, 1); if neither: O's own
     virtual `+0x44`(&p, 1) — the plain "set position".
4. `0x006fcc80`(this = row, index) (not dumped; by position the entry's state/timer reset).

So **recycle = respawn the object at a new random point on the flow's entry annulus** (random angle around the axis, random
radius, nudged outward until valid), re-applying a vehicle's saved state and firing its hook-11 callback when it is a vehicle;
the entry stays in the row's list. §28.11's HYPOTHESIS "return the object to the flow's pool" is **CORRECTED**: nothing is
pooled; the object re-enters the flow in place. The two branches' identities (`0x00b71950` = a vehicle-type wrapper, `0x00b71970`
= an alternative handle) are HYPOTHESIS; the vehicle helpers `0x00ab89a0`/`0x00ac04a0`/`0x00ac0e10`/`0x00ab98f0`/`0x00ac0c90`
(engine/light state by their range) and `0x00a874f0`/`0x00a77d70`/`0x006fcb00`/`0x006fcc80` are OPEN.

---

## Q10. §28.7 / §1.12 — `0x00778bc0` (category 47) and the three trigger shapes `0x00dbdc90` / `0x00dbe2a0` / `0x00dbe050` — CONFIRMED — disassembly (the shape names are no longer HYPOTHESIS; the meaning of 47 is data)

### Q10.1 `0x00778bc0`(this = table, &out, a, b) — a 128 × 128 category-compatibility bit matrix — CONFIRMED

`func1/func_0x00778bc0.txt` (`0x00778bc0`–`0x00778c41`, thiscall, three stack arguments, seven callers including both sites in
`0x0075d4b0`). Writes a byte result:

1. If the current thread's record (TLS slot `0x02cc4700`, byte `+0xc` bit `0x10`) has that bit set and **either code is 59**
   (`0x3b`): true. A per-thread override — HYPOTHESIS: a debug/editor "match everything" category.
2. Else, if both codes share the same **non-zero upper 16 bits**: false (two members of the same instance group never match).
3. Else: bit = (a & `0xffff`) × 128 + (b & `0xffff`); result = that bit of the dword array at table`+0x34`.

So the category literal 47 of §28.7 is a **row index of a 128 × 128 bit matrix held by the receiver object**, and
`0xc0069` is group `0xc`, category 105. **The meaning of 47 cannot be read from code: it is table content** (the matrix is
data, filled by the receiver's loader — not dumped). What the dump settles is the mechanism: `0x0075d4b0` keeps a candidate when
matrix[47][candidate code & `0xffff`] is set, and appends its second query when matrix[47][105] is set. That 47 is the "human"
category is consistent with the Lua name and with the human-class gate the caller applies afterwards, but stays HYPOTHESIS.
**[CONFIRMED — the lookup; OPEN — the matrix's source and the receiver's identity (HYPOTHESIS: the collision-category filter).]**

### Q10.2 The three shape tests — CONFIRMED — disassembly

- **`0x00dbdc90`(&point, &origin, &frame, &min, &max)** = **oriented box**: the point minus the origin is multiplied by the 3 × 3
  matrix at `frame` (`0x00da2810`: row-major, result[i] = Σ frame[3i+j] · d[j]) and the result must satisfy min ≤ p' ≤ max on all
  three axes. (`0x00da2810` is a world-to-local transform shared with eleven other callers.)
- **`0x00dbe2a0`(&center, radius, &point)** = **sphere**: |point − center|² ≤ radius².
- **`0x00dbe050`(&base, &axis, length, radius, &point)** = **capped cylinder**: t = axis · (point − base) must lie in [0, length], and
  the squared distance from the point to the axis line (`0x00da1330` on the foot of the perpendicular) must be ≤ radius².

So the `+0x8c` shape dispatch of the trigger method `0x0093bfc0` (§1.12, §28.7) is **0 = oriented box (frame from the trigger's
virtual `+0x60`), 1 = sphere (radius `+0x90`), 2 = cylinder (`+0x90` = radius, `+0x94` = length)**. All three are inclusive tests
with single-precision inputs computed in double. **[CONFIRMED.]** The caller's exact argument-to-field mapping for the cylinder
is as §28.7 recorded; not re-derived here.

---

## Q11. §28.8 — the two predicates inside `0x00ad4040` (`get_char_vehicle_is_in_air`) — CONFIRMED — disassembly (mechanics); names HIGH CONFIDENCE

Bodies: `func1/func_0x00ad4040.txt`, `func1/func_0x00ad3b40.txt` (with `0x00767cd0`), `func1/func_0x00ad3c50.txt`.

- **`0x00ad4040`(vehicle)** = true iff `0x00ad3b40` is false, `0x00ad3c50` is false and bit 0 of byte `+0xc8` (§24.7's in-water bit)
  is clear — as §28.8 already had it.
- **`0x00ad3b40`(vehicle)** — "the body touches something that is not an object": the vehicle's own id pair (`+0x8`/`+0xc`) is
  mapped through `0x00767cd0` to a 16-byte contact record (`0x00767150`(id, 0x1000) then `0x00767090`(this = the contact
  manager `0x016f23d0`, id, …) → a slot index into the array at `0x016fe3e0`, or the empty record `0x0170e418` when the index is
  `0xffff`); the ring at record`+0x8` (nodes: an id pair at `+0x0`/`+0x4`, next at `+0x14`) is walked and **true is returned as
  soon as a node's partner id pair is zero**. A null-id partner is static world geometry by elimination (every live object has
  an id) — so this is "resting on / colliding with the world" **[HIGH CONFIDENCE for the name; the ring walk is CONFIRMED]**.
- **`0x00ad3c50`(vehicle)** — "a wheel has ground contact": requires byte `+0xbd0` == 1 and the three sub-objects `+0x7fc`,
  `+0x808`, `+0x81c`; walks the entries of the `+0x7fc` object (count at its `+0x20`, pointers from `+0x24`) and returns true at the
  first entry whose dword `+0xf0` has none of bits `0x600000` set and for which the `+0x808` object's virtual `+0x48`(index)
  returns anything but −1. By shape: `+0x7fc` = the wheel set, `+0x808` = the suspension controller whose slot `+0x48` reports a
  wheel's contact (−1 = none), bits `0x600000` = a disabled/detached wheel **[HYPOTHESIS for the names; the loop is CONFIRMED]**.

So for Team B: a vehicle is "in air" when it has no world contact on its body, no wheel reporting a contact, and is not in
water. Nothing here depends on velocity or height.

---

## Q12. §28.4 — the three helpers behind `guardian_angel_enable_indicators` — CONFIRMED — disassembly (bodies); the owning object's identity stays OPEN

- **`0x00d34d40`** (`func1/func_0x00d34d40.txt`): a two-instruction `return 1` stub (seventeen callers across unrelated code).
  The suspicion recorded at §27.24 is CONFIRMED; the call in `0x00626210` is inert.
- **`0x006d2910`()** (`func1/func_0x006d2910.txt`): M = the active mission object `0x014c8460` (§27.25); if M exists and its class
  row (`0x02cc9900`[byte `+0x34`]) has bit `0x10` at `+0xc`, tail-jump to **the second virtual (slot `+0x8`) of the object at
  M`+0x164`** and return its result; otherwise −1. So the "mode == 3" test of §28.4 is **"the active mission's `+0x164`
  sub-object reports 3"**; what that sub-object is (HYPOTHESIS: the mission's activity instance, 3 = the guardian-angel
  activity) is not in the dump. Fourteen callers, mostly the `0x00614d10`–`0x00617400` mission-script helpers and the tutorial
  routine `0x007169e0`.
- **`0x00614cb0`()** (`func1/func_0x00614cb0.txt`): returns `[[0x014b2ddc] + 0x1c] + 0x1c`, or 0 when either pointer is null.
  `0x014b2ddc` is the singleton of the `0x00614a00`–`0x00615dc8` cluster (178 uses in 36 functions, all there); thirty-odd callers
  of this getter sit in the mission/AI script code `0x00626d3a`–`0x006e8dd0` (including `0x0062a150`/`0x0062a170`, the neighbours of
  the vehicle resolver `0x0062a190`). So the **`+0x57d` indicator byte lives on the object two pointer hops below `0x014b2ddc`**.
  **[CONFIRMED — the dereference chain; OPEN — the object's identity. HYPOTHESIS: the mission-script singleton → its player
  record → the player's character.]**

---

## Q15. §28.1 / §28.2 — `0x00a79470`, the vehicle "AI record" getter — CONFIRMED — disassembly: the record is embedded at vehicle `+0x100`

`func1/func_0x00a79470.txt` (`0x00a79470`–`0x00a79492`; 30+ callers, nearly all in the helicopter AI code `0x00b34xxx`–`0x00b42xxx`):
calls `0x00ad3220`(vehicle) and, when that is false, `0x00ad3200`(vehicle) — **both results are discarded** — then returns
**vehicle + `0x100`**. The two helpers test the vehicle definition's `+0x2c` (through `+0xbf4`) for 3 and 4 respectively, i.e.
the two helicopter types that §28.2's `0x00ad31a0` combines; here they are the remains of a stripped assertion and have no
effect. So "the AI record" is not a separate object: §28.1's bit `0x8` of byte `+0x2` is **vehicle byte `+0x102`**, §28.2's
`+0x298`, `+0x430`..`+0x438` and `+0x4b0` are **vehicle `+0x398`, `+0x530`..`+0x538` and `+0x5b0`**. What those fields mean is still
OPEN (nothing in this job reads them); the getter itself is closed.

---

*(Numbering note: Q13 and Q14 below are `rederive_28.md` "Next dumps" items 6 and 7 — the two func1 groups not yet
interpreted when this note was resumed; Q16 is item 9, the `func2/` step. Q1–Q12 and Q15 above are unchanged.)*

## Q13. §28.23 — `0x00943a20` (the skip predicate) and `0x004b1b90` (the animation start) behind `cellphone_animate_start_do`

Bodies: `func1/func_0x00943a20.txt` (`0x00943a20`–`0x00943afb`, cdecl, one argument = the character; 30+ callers across the
weapon, seat, camera and animation code) with the depth-1 callees `0x0096f4f0`, `0x004b2190`, `0x009b9180`, `0x005fe3e0`,
`0x0091f5e0` (`0x009ba300` and `0x00d9e1e0` queued, not dumped); `func1/func_0x004b1b90.txt` (`0x004b1b90`–`0x004b1ee5`, cdecl,
seven arguments; 30+ callers including itself) with `0x004b11a0`, `0x004b1630`, `0x00db5930`, `0x00dab660` dumped and
`0x004cc590`, `0x004cbd20`, `0x004bce30`, `0x00d9e140` not (`maxfuncs`).

### Q13.1 `0x00943a20`(character) = "the character is in its combat-ready (weapon-raised) stance" — CONFIRMED — disassembly (every test); HIGH CONFIDENCE (the name)

In order; each "→ false" returns at once:

1. `0x0096f4f0`(C) — §3.5's dead predicate (null, state `+0xcc8` == 5, or byte `+0xe3` bit 1) → false.
2. S = `0x004b2190`(the animation controller at [C`+0xd24`]`+0x10`) = the controller's dword **`+0x44`** (the same field
   `0x004b1630` keys its state lookup on, Q13.2 — the controller's current set id). If `0x009b9180`(C) is true (the seat
   state `+0x16d4` ≠ 0: in or entering a vehicle, §21.11) **and S is 0 or `0xc`** → false.
3. W = `0x005fe3e0`(this = C`+0x1b78`, 0) — the item equipped in the first slot pair of the equipment sub-object (§18's
   items 16–18); D = W`+0x19c`, the item's definition record (the chain §18 item 17 reads the name through). If D exists
   and byte D`+0x10` has bit `0x20` → false.
4. If `0x0091f5e0`(C) (C is local player 1 — two calls of `0x009da4e0` and a compare), D exists, D`+0x1c` == 7, and (byte
   C`+0xccc` ≠ 0 or the byte `0x0130ef98` == 0) → false. `0x0130ef98` is a file-backed zero byte with **nine readers and no
   writer anywhere** (`0x005b64c0`, `0x007f69e0`, `0x00907160`, `0x00908930`, `0x009270e0`, `0x00927820`, `0x009e91c0`, undefined
   code at `0x00a5a1eb`, and this body), so the parenthesis is always true: **the local player holding a kind-7 item is never
   combat-ready** [HIGH CONFIDENCE — inert global, the same reasoning as Q5.2's `0x02282d1c`]. `+0xccc` is the movement-state
   byte that §21's `0x00941e60` tests for 3.
5. Byte C`+0x2b8` bit `0x10` set, or byte C`+0x2bb` bit `0x80` set → false.
6. **Flags C`+0x1c98` bit `0x1` set → true.** That bit is `human_force_flagsalways_combat_ready` (§18 item 17 / §18.31 #6),
   which is where the name comes from: the predicate answers exactly the question that flag forces.
7. `0x009ba300`(C) true → true (not dumped).
8. Otherwise true iff `0x00d9e1e0`(this = C`+0x1390`) returns 0 — a query on a timer record in the `0x00d9exxx` deadline family
   (Q2's `0x00d9e3c0`/`0x00d9e400`). HYPOTHESIS: "the stance hold-down has not run out", i.e. the weapon stays raised for a
   while after the last combat event.

For §28.23: **`cellphone_animate_start_do` plays "Cell Phone Answer" only while local player 1 is not combat-ready.** Being
dead, seated with the controller in set 0/`0xc`, holding a weapon whose definition has bit `0x20` at `+0x10` or kind 7, or
carrying the `+0x2b8`/`+0x2bb` state bits all count as "not combat-ready" (the animation plays); the flag, `0x009ba300` or the
running `+0x1390` timer suppress it. **[CONFIRMED — disassembly for every test; HIGH CONFIDENCE for the name; OPEN —
`0x009ba300`, `0x00d9e1e0`, the two definition fields (`+0x10` bit `0x20`, `+0x1c` == 7) and the two state bits.]**

### Q13.2 `0x004b1b90`(controller, state, a3, flags, f1, f2, f3) — start an animation state on a controller and all its children — CONFIRMED — disassembly (slot allocation, state lookup, the two kinds of state); the three floats and flag `0x80` are routed but not interpreted (OPEN — `0x004bce30`)

1. **Children first.** The child ring at controller`+0x8` (next pointer at node `+0x0`, the list §18.8's `0x004b21a0` walks): the
   same call is made recursively on every child with all seven arguments unchanged, before anything else.
2. controller`+0x20` (its rig id, by its use in step 7) must not be −1, else return −1.
3. **Slot.** The active-state array: `0x84`-byte entries at `+0x4c`, count `+0x48`. Take the first entry whose flags dword
   `+0x80` has bit 0 (in use) clear, or — **unless `flags` has bit `0x400`** — whose `+0x8` already equals `state` (restart in
   place). None → `0x004b11a0` grows the array by exactly one entry (the pool allocator `0x00db5930` on the heap object
   `0x03424868`; copy; re-base the `+0x50`/`+0x54` cursors and each entry's `+0x78` pointer; new entries zeroed with `+0x0` =
   `+0x6c` = −1). Still none → −1.
4. **State row.** entry`+0x0` = `0x004b1630`(controller, state): for each of the two table slots at controller`+0x30`/`+0x34`
   (null ones skipped), `0x004ccf30`(table, controller`+0x44`, state) → a row index; −1 if none matches or `+0x44` is −1.
   −1 → return −1 (the failure path §7.11 describes for `set_script_animation_state`). Row R = `0x02ef2178` + index × `0x44`
   when 0 ≤ index < `0x03171be8` (the row count; the rows are filled by `0x004ae7a0`/`0x004aea30`), otherwise no row.
5. If R has bit `0x80000000` at `+0x2c` and controller`+0x28` is null: controller`+0x28` = a zeroed block of 8 × the word
   `0x03171bc8` bytes (written only by `0x004cae20`; HYPOTHESIS: the bone count). Allocation failure → −1.
6. entry`+0x80` |= 1, &= ~`0x400`; entry`+0x8` = state; **entry`+0xc` = a3** (§28.23's −1 — stored, never read here).
7. With a row: `flags` bit `0x4000` or R`+0x2c` bit `0x8` → entry`+0x80` |= `0x400`, controller`+0x1a4` |= `0x40`,
   controller`+0x1a0` = 0.0. Then by R`+0x2c`:
   - bit `0x1` → `flags` |= `0x2000`;
   - else bit `0x80000000` (**the managed kind**): entry`+0x80` |= `0x10`; a previous entry`+0x6c` handle is released
     (`0x004cc590`); entry`+0x6c` = `0x004cbd20`(controller`+0x20`, word R`+0x8`) — an index into the `0x4c`-byte rows at
     `0x03133a90` (free-listed by `0x004cb990`); that row's `+0x40` = **f1**, `+0x38` = controller`+0x44`; byte controller`+0x24`
     += 1; entry`+0x10` = 1.0; **return −1** — no clip instance is made for this kind. (When `0x004cbd20` returns −1 the row
     pointer is null and the two stores would fault; the engine relies on it never failing.)
   - bit `0x8000000` → controller`+0x1a4` |= `0x40`, controller`+0x1a0` = 0.5 (`0x0126d2cc`).
   - byte R`+0x27` non-zero → entry`+0x70` = `0x00d9e140`(this = entry`+0x70`, `0x00dab660`(R byte `+0x24` × 180, R byte
     `+0x25` × 180)): a deadline (milliseconds, per `0x00d9e140`'s use in §28.25) drawn uniformly from the row's own
     [min, max] × 180 ms through the random ring `0x013214d4` (cursor `0x013214d0`, 8192 entries — the ring Q9.2's `0x00dab6a0`
     draws from); else entry`+0x70` = −1. HYPOTHESIS: the state's random hold / re-trigger interval.
   - bit `0x40` → `flags` |= `0x10000`. Then entry`+0x14` = `flags`.
   - bit `0x80000000` clear → **entry`+0x4` = `0x004bce30`(flags, 0.0, f2, f1, f3, −1.0)**, with controller`+0x20` and R`+0x8`
     passed in registers — the clip-instance creation (not dumped). Order verified from the frame: the pushes are −1.0
     (`0x012a2d54`), f3, f1, f2, 0.0, then `flags`.
8. `flags` bit `0x200000` → controller`+0x60` = state, `+0x64` = entry`+0x4` (make it the controller's current pair). Return
   entry`+0x4` (the instance; 0 when there was no row).

For §28.23's call (controller = the player's `+0xd24`→`+0x10`, "Cell Phone Answer", −1, `0x80`, 1.0, 1.0, −1.0): none of the
bits this body consumes (`0x400`, `0x4000`, `0x200000`) is set, so an entry already playing that state is restarted in place
and the play is not made the controller's current pair; **bit `0x80` goes through to `0x004bce30` unread here**; a3 = −1 lands
in entry`+0xc` unread; f1 = 1.0 is the value stored into the managed row's `+0x40` and the fourth float of `0x004bce30`, f2 =
1.0 the third, f3 = −1.0 the fifth. **The meaning of the three floats and of bit `0x80` is inside `0x004bce30` — OPEN**
(HYPOTHESIS: f1 = playback rate, f2 = blend weight, f3 = −1 "default start/duration"). The routing is CONFIRMED, which is
what §28.23 asked for; the row-flag vocabulary above (`+0x2c` bits `0x1`, `0x8`, `0x40`, `0x8000000`, `0x80000000`; bytes
`+0x24`/`+0x25`/`+0x27`) is new and HIGH CONFIDENCE only as far as named here.

---

## Q14. §28.15 — the six helpers of `0x005f1cc0` (`crib_unlock_strongold`): tutorials, phone contacts, zone swaps, trigger/door groups, the save row and the owned-object notice

Bodies: `func1/func_0x007161a0.txt`; `func1/func_0x005f1690.txt` (with `0x005f1450`, `0x00d1d700`, `0x005f0b30`);
`func1/func_0x005f1750.txt` (with `0x0087ba20`, `0x005eea30`, `0x0084a940`, `0x0084a910`, `0x005f1450`; nine callees queued,
not dumped); `func1/func_0x005eea30.txt` (with `0x0071ccf0`); `func1/func_0x00b98d00.txt`; `func1/func_0x005e8fb0.txt` (with
the record helpers of Q6 and `0x0086f1b0`). Not dumped: `0x00bd2950`, `0x00bd3510`, `0x00710910`, `0x005e8b30`,
`0x008da510`/`0x008da550`, `0x008d6ba0`, `0x004d47e0`, `0x006055d0`, `0x0084a6c0`/`0x0084a710`/`0x0084a7f0`, `0x009601d0`.

### Q14.1 `0x007161a0`(index, force) — arm a tutorial entry — CONFIRMED — disassembly

If the tutorial-disable byte `0x0151d5a6` (§6.19, dispatcher step 1) is zero and index ≤ 209: the entry's state (table
`0x0151d600`, 36-byte entries, state at `+0xc`) becomes 1 when it is 0, or when it is 4 (rich widget issued) and `force` is
set. That is §6.19's step 9 without the display, plus the forced re-arm. Callers: `0x005f1cc0` four times — **unlocking a
stronghold arms tutorials 7, 8, 9 and 10** (force 0, so prompts already issued stay issued) — and undefined code at
`0x00a60599` in the Lua-binding range. Which prompts 7–10 are is in the name table `0x012f5930` (§6.19), not dumped.

### Q14.2 `0x005eea30`(this = stronghold, level) → `0x0071ccf0`(&name, 0, 0) — unlock the level's phone contacts — CONFIRMED — disassembly (mechanism); HIGH CONFIDENCE (the table's identity)

`0x005eea30` (two callers: `0x005f1cc0`'s client branch with 0, `0x005f1750` with level − 1): for every entry i of the
stronghold's name-hash array `+0x138` (count `+0x134`) whose key `+0x13c`[i] equals `level`: `0x0071ccf0`(&`+0x138`[i], 0, 0).

`0x0071ccf0`(&hash, a, b) (seven callers in the `0x0060exxx`/`0x0061bxxx` mission code and the `0x007bexxx`–`0x007c0xxx` UI
code): hash equal to the "none" sentinel `0x029c9964` → return. Find the `0x100`-byte row at `0x01522750` (count `0x015226fc`)
whose first dword is the hash; none → return. Row byte `+0xe1` = 1 and byte `+0xe2` = (a == 0). Then, when row`+0x4` ==
`0x20`: P = `0x009da4e0`() (local player 1); a == 0 → **return here**; b == 0 → `0x009601d0`(&local, row`+0x8`) and return if
P`+0x1ca0` is below the result (not dumped; by shape a respect/rank threshold the player must meet). Otherwise, and for
every other row kind: return if byte row`+0xe0` is set; `0x0101b4a0`() (the zero-argument stub §23 notes); return if
row`+0xf0` is set and a == 0; append the row to the list `0x015220f8` (count `0x015220f0`, capacity 384); and if the hash is
that of the literal `"HomieSlot1"` (`0x00d9e8b0`(name, 0, −1) into a local), **`0x007169e0`(100, 1000, 0, 0, 0, 0)** — the
tutorial dispatcher of §6.19 with index 100 and argument 1000.

The table `0x01522750` is the cellphone contact (homie) table: `0x100`-byte rows keyed by name hash, `"HomieSlot1"` among
them, read by the `0x0071b6d0`–`0x0071fd30` cluster (fifty uses). **[HIGH CONFIDENCE.]** Byte `+0xe1` = available; `+0xe2` =
"newly unlocked" (a == 0 marks it) and `+0xe0` = disabled, `0x015220f8` = the pending new-contact notice list — all
HYPOTHESIS. So §28.15's client branch (`0x005eea30`(this, 0)) and, on the host, `0x005f1750`(this, 1) → `0x005eea30`(this, 0)
(Q14.4) both unlock the contacts the stronghold grants at level index 0: **every machine unlocks them locally.**

### Q14.3 `0x005f1690`(this = stronghold), `0x005f1450` and `0x005f0b30` — enable the stronghold's trigger and door groups — CONFIRMED — disassembly (structure); the door-side helpers OPEN; settles §1.1's hidden-argument OPEN

`0x005f1690` (reached only through the thunk `0x005eea00`; `0x005f1cc0` clears `+0x7a` bit `0x2` just before, §28.15):

1. If `+0x7a` bit `0x2` is clear (always, on that path): `0x005f1450`(this, `+0x144`, 1) — enable the group of the current
   second-swap level (`+0x144`; −1 before the first `0x005f1750`).
2. A one-dword local object carrying the vtable `0x011265e4` (→ `0x005ee9c0`) is handed to `0x00d1d700`, which ignores it and
   returns `0x1b6` — a stripped stub like Q12's `0x00d34d40`. Inert.
3. `0x005f0b30`(this, 1 if `+0x7a` bit `0x2` is clear, else 0) → 1 on this path.

**`0x005f1450`(this = stronghold, group, enable)** — three id-pair arrays keyed by a group index (= a level index, −1 = none):

- `+0xbc` = the number of entries of the dword array `+0xc4` (count `+0xb8`) equal to `group` (kept for later readers).
- For each pair of `+0xcc` (count `+0xc8`) whose key `+0xd0`[i] == group: resolve through `0x00458230` on the object table
  `0x024433a8`, `+0x33` bit `0x10` clear, **class bit `0x2` at `+0x7`**, alive → `enable` ? `0x0093d320`(this = object, 3) :
  `0x0093bde0`(this = object, 3). Those are the pair §1.1 `trigger_enable` calls "with only 1 visible argument": **the hidden
  argument is the trigger handle in the receiver register, the visible one the flag value** (3 here). So class bit
  `0x2`@`+0x7` = trigger objects [HIGH CONFIDENCE] and these are the stronghold's per-level trigger groups.
- The single pair `+0xd8`/`+0xdc`: the same enable/disable, but enable = `+0x7a` bit `0x2` set — a trigger that is **on while
  the stronghold is still locked and off once it is unlocked** (HYPOTHESIS: the take-over trigger).
- For each pair of `+0x128` (count `+0x124`) whose key `+0x12c`[i] == group: resolve, **class bit `0x20` at `+0x8`**, alive: if
  `enable` and `0x004d47e0`(stronghold) (not dumped; the undecompiled predicate §7 also meets): `+0x130` = `0x008da510`(this =
  object, callback `0x005ee7f0`, stronghold) — register a callback with the stronghold as its context, returning a handle —
  and, if byte object`+0x7d` bit 1 is set and a local player exists, **player`+0x209c` = stronghold** and `0x006055d0`(player).
  Else: if `+0x130` ≠ −1, `0x008da550`(this = object, `+0x130`) and `+0x130` = −1 (unregister). `0x008da510`/`0x008da550` sit in
  the door cluster (`0x008d6ba0`/`0x008d6f10`, §1.10/§9.14) → HYPOTHESIS: the stronghold's entrance doors, whose use fires
  `0x005ee7f0` with the stronghold.

  **Cross-section:** §23.19 reads player `+0x209c` as "a per-player garage-vehicle-list pointer" passed as `this` to
  `0x005f0f90`. The store above shows **`+0x209c` is the player's current crib (the stronghold object)**; the garage list is a
  member of it and `0x005f0f90` a method of the `0x005fxxxx` crib cluster. [CONFIRMED — the store; HIGH CONFIDENCE — the
  re-reading of §23.19's call.]

**`0x005f0b30`(this, flag)**: `0x00d34cf0`() (a stub by position, between `0x00d34cd0` and Q12's `0x00d34d40`; not dumped); for
each pair of `+0xa4` (count `+0xa0`): resolve, **class bit `0x8` at `+0x9`**, alive → **`0x008d6ba0`(this = object, flag)** — the
routine §9's `door_lock` applies to a door's attached object with the lock boolean. Flag = 1 on the unlock path. **OPEN —
`0x008d6ba0`'s polarity**: if it is "set locked", unlocking a stronghold *locks* the `+0xa4` objects (HYPOTHESIS: the gang's
barricades / exterior doors are sealed once the crib is yours); if it is a generic "apply state", they open.

### Q14.4 `0x005f1750`(this = stronghold, level) — set the upgrade level (1–3), host only — CONFIRMED — disassembly (structure); identifies `0x0084a910`/`0x0084a940` as the city-zone-swap on/off pair (closes part of §1.9's NEEDS-EXE)

Callers: `0x005f1cc0` (level 1), `0x0071f170` (beside §28.15's resolver `0x0071f0d0` — the crib Lua helpers) and `0x00812400`
(the store-UI range). Nothing happens unless 1 ≤ level ≤ 3, a session exists and the local machine is its host (`+0x5c` ==
`+0x58`). With L = level − 1:

1. `0x005eea30`(this, L) — Q14.2.
2. **First swap set** `+0x148` (three hashes, one per level): if `+0x140` ≥ 0 and `+0x148`[L] is not the none sentinel:
   `0x0084a940`(&`+0x148`[`+0x140`]). Then `0x0084a910`(&`+0x148`[L]); `+0x140` = L.
3. **Second swap set** `+0x154`: if `+0x154`[L] is not none: if `+0x144` ≥ 0: `0x005f1450`(this, `+0x144`, 0) and
   `0x0084a940`(&`+0x154`[`+0x144`]); then `0x0084a910`(&`+0x154`[L]); `+0x144` = L; `0x005f1450`(this, L, 1).
4. `0x00bd3510`(this) (not dumped; see Q14.5).
5. Record opcode `0x45`: 8-bit **3**; `0x00a017c0` (the identity serialiser, §15.10) on the stronghold; two 4-byte fields
   **`+0x140` + 1 and `+0x144` + 1** (0 = none on the wire); broadcast to the whole session (`0x0086f1b0`(session, 0, 0), Q6);
   if `+0x140` ≥ 2 (level 3 reached): `0x00710910`(`0x2c`) (not dumped; HYPOTHESIS: an achievement/unlock notice, id 44); close.

**`0x0084a910`(&hash)** and **`0x0084a940`(&hash)** (both dumped): a hash equal to the none sentinel → return 1 without acting;
otherwise `0x0084a6c0` (resp. `0x0084a710`), with the pointer in a register → false → return 0; true → `0x0084a7f0`(1) (resp.
`0x0084a7f0`(0)) and return 1. These are the two normal-path calls of §1.9 `city_zone_swap(name, bool)` — "`0x0084a940` if
false, `0x0084a910` if true" — so **`0x0084a910` activates the zone swap named by the hash and `0x0084a940` deactivates it**, and
both take a pointer to the hashed name (what §1.9's `0x00d9e8b0` output buffer feeds, as §28.18 already found). [CONFIRMED —
the shape, the sentinel test and the 1/0 handed to `0x0084a7f0`; OPEN — the inner bodies.] A stronghold therefore carries
**two per-level zone-swap names** (`+0x148`, `+0x154`; HYPOTHESIS: the exterior and the interior variant of each upgrade level),
and the second set also keys the trigger/door groups of Q14.3.

Opcode `0x45` is the crib opcode: sub-type 2 = unlock (§28.15), 3 = level change (here), `0x1d` = Q14.6's owned-object notice.

### Q14.5 `0x00b98d00`(&id, level, variantFlag) — the persisted per-stronghold row — CONFIRMED — disassembly (mechanism); HIGH CONFIDENCE (identity)

One caller (`0x005f1cc0`'s client / no-session branch). Scans the `0x18`-byte rows at `0x0290f188` (count `0x0290f180`): {id
low, id high, flags byte `+0x8`, dword `+0xc`, dword `+0x10`}. For every row whose id pair matches: clear bit 0 of the flags
byte, `+0xc` = level, `+0x10` = variantFlag. Rows are created by `0x00b9a250` (writes the pair, sets bit 0) and read by
`0x00b97320` and `0x00b9a380` — all in the save-game code of Q5 (`0x00b94xxx`–`0x00b9axxx`; `0x00b98c00` next door writes the
tutorial byte `0x0151d5a6`). So this is **the save file's stronghold table: {id, bit 0 = still locked, upgrade level,
second-variant present}.** The two values the client writes, (1, `+0x154`[0] ≠ none), are exactly `+0x140` + 1 and
"`+0x144` ≥ 0" after the host's `0x005f1750`(this, 1): the client records what the host's level change produces, and
`0x00bd3510`(this) in Q14.4 is HYPOTHESIS the host-side writer of the same row.

### Q14.6 `0x005e8fb0`(&id, flag) — notify the stronghold's owned world objects — CONFIRMED — disassembly (structure); the per-object effect OPEN

Six callers (`0x005f1cc0`, `0x005e9990`, `0x006a75e0`, `0x007be600`, `0x008d8420`, `0x00a03490`). If flag == 0, a session exists
and the local machine is host: record opcode `0x45`, 8-bit **`0x1d`**, then the 8-byte id pair (`0x0086f500` →
`0x00881040`(&pair, 8)), broadcast, close. Then `0x00d34cd0`() (stub by position) and: L = the world-object list `0x03171a64`
(created by `0x00457640` in the object-table code of Q4; §8.10 reads emitter slots from it and §26's cutscene prep its
"scene-bearing world objects"); for i in 0 .. L`+0x198` − 1: O = L`+0x58`[ the 16-bit index L`+0x190`[i] ]; **if O`+0x58`/`+0x5c`
equals the id pair: `0x005e8b30`(this = O, flag, 1)** (through the thunk `0x005e8840`; not dumped — it builds a record of its own,
`0x005e8be0` calls `0x0086f500`). So every world entry whose owner pair is the stronghold is told the flag (0 on the unlock
path). HYPOTHESIS: flag = the locked state applied to the stronghold's gang-spawn / scene entries, and the sub-type-`0x1d`
receiver on clients calls the same routine with flag 1 to suppress the re-send. **OPEN — `0x005e8b30`.**

### Q14.7 What §28.15 should now say

Unlocking, as host: arm tutorials 7–10; clear the locked bit; enable the current trigger/door group and apply
`0x008d6ba0`(1) to the `+0xa4` door list; count `0x014a0f80` up; set level 1 — unlock the level-0 phone contacts, activate both
level-1 zone swaps, re-target the trigger/door group, replicate (opcode `0x45` sub-type 3 with both level indices + 1).
Unlocking, as client or without a session: write the save row (level 1, second-variant flag) and unlock the level-0 contacts
locally. Both: `0x00bd2950` (not dumped) and the owned-object notice (sub-type `0x1d` goes out from the host only). Helper
identities still OPEN: `0x00bd2950`, `0x00bd3510`, `0x00710910`, `0x005e8b30`, `0x008d6ba0`'s polarity, `0x008da510`/`0x008da550`,
`0x004d47e0`, `0x006055d0`, `0x0084a6c0`/`0x0084a710`/`0x0084a7f0`, `0x009601d0`.

---

## Q16. §4.5 / §18.31 #1–#2 — `0x00a281e0` and its two halves `0x004dcf00` / `0x0062a190` (the `func2/` step) — CONFIRMED — disassembly; two details the spec does not yet have

Bodies: `func2/func_0x00a281e0.txt` (`0x00a281e0`–`0x00a28258`, cdecl, one argument; 30+ callers — every `0x00a281e0`-resolved
binding) with `0x00a280c0`, `0x004dcf00`, `0x0062a190`, `0x00853b10`; `func2/func_0x004dcf00.txt` with `0x00458230` and
`0x00853b10`; `func2/func_0x0062a190.txt` with `0x004588f0`.

1. **`0x00a280c0`(name, &found)** — the first attempt of the generic/`#PLAYER#` chain. Null name → 0. The second argument is an
   **optional "found" out-byte** (a local stands in when it is null), cleared first. O = `0x00a27e20`(this = `0x02442750`, name)
   (the per-kind by-name lookup of §4.13); O alive → found = 1, return O. Otherwise, if the name equals `"#PLAYER#"` byte for
   byte → found = 1, return `0x009da4e0`() (local player 1). Otherwise 0. So the "(name, 0)" the spec cites in dozens of places
   means "no out-byte wanted", not a mode flag. **[CONFIRMED.]**
2. **`0x00a281e0`(name)**: H = `0x00a280c0`(name, 0). If H exists and its pair `+0x16c0`/`+0x16c4` is non-zero: **return
   `0x004dcf00`(low, high, 0), whatever it yields** — a character whose cached pair points at a dead or non-vehicle object
   resolves to nothing, and the name fallback is *not* tried. Otherwise (no H, or a zero pair), with a non-null name: V =
   `0x0062a190`(this = `0x02442750`, name); V exists and is alive; V's virtual `+0x68` true → tail-jump to V's virtual `+0x70`
   (the instance); else 0. As §18.31 #2 has it, plus the no-fallback detail. **[CONFIRMED.]**
3. **`0x004dcf00`(low, high, ignoreDead)**: a zero pair → 0. O = `0x00458230`(this = the object-by-id table `0x024433a8`, &pair,
   `0x031d152c`). `0x00458230` (dumped for the first time): x = (low ^ high) × `0x1001`; x ^= x >> 22; x ×= 17; x ^= x >> 9;
   x ×= `0x401`; x ^= x >> 2; x ×= `0x81`; x ^= x >> 12; bucket = x mod the bucket count at table`+0x4`; then linear probing
   (wrapping) over at most table`+0x10` (the entry count) slots of the pointer array table`+0x8`, returning the first object
   whose own `+0x8`/`+0xc` pair matches, else 0. **Its second stack argument (`0x031d152c`, pushed by every caller in the
   project — 2,436 uses) is popped and never read by this body.** O then needs `+0x33` bit `0x10` clear and class bit `0x80` at
   `+0x6`, and is returned if alive (`0x00853b10`: non-null, `+0x33` bit `0x4` clear, class byte `+0x34` ≠ `0xff` — the Q4
   reading of the liveness test, now CONFIRMED from its body) or `ignoreDead` is set. Exactly §18.31 #1. **[CONFIRMED.]**
4. **`0x0062a190`(this = `0x02442750`, name)**: the vehicle-name registry at table`+0x2660` (count `+0x265c`, must be > 0).
   `0x004588f0`(this = registry, name, &out) (dumped): bucket = `0x00dab330`(name, registry`+0x20`) (a string hash reduced by
   the bucket count); walk the chain from registry`+0x1c`[bucket] (node: `+0x0` object, `+0x4` next, `+0xc` the name) comparing
   with **`_stricmp`** — **vehicle names resolve case-insensitively**; a hit → out = the object, true. Then `+0x33` bit `0x10`
   clear and class bit `0x8` at `+0xb` (§4.13/§9.9) → the object, else 0. No liveness test inside; the caller applies it.
   **[CONFIRMED.]**

For §4.5: its resolver paragraph stands; add "no fallback when the cached pair is stale" and "the vehicle-name lookup is
case-insensitive". Its own NEEDS-EXE (the two `+0xc68` setters and the record-vs-flip order) concerns `0x00a63710`'s callees,
which this job did not dump — still OPEN. For §1.1/§4.2 and every other place that cites `0x00458230` by role: it is the
id-pair hash lookup on the object table, and `0x031d152c` is a dead parameter.

---

## Spec pointers from Q13–Q16 (for the editor; nothing edited here)

- §28.23: replace "OPEN — `0x00943a20` and the exact meaning of `0x004b1b90`'s trailing arguments" with Q13: the play is skipped
  while the player is combat-ready (Q13.1); `0x004b1b90` restarts/creates the state entry on the controller and its children,
  a3 → entry`+0xc`, flags → entry`+0x14`, f1/f2/f3 → `0x004bce30` (Q13.2). OPEN shrinks to `0x004bce30`, `0x009ba300`, `0x00d9e1e0`.
- §28.15: replace "OPEN — the helper identities" with Q14.7's paragraph; sub-types 2/3/`0x1d` of opcode `0x45`.
- §1.1 `trigger_enable`: the hidden argument of `0x0093d320`/`0x0093bde0` is the trigger handle (receiver register) — Q14.3.
- §1.9 `city_zone_swap`: `0x0084a910` = activate, `0x0084a940` = deactivate, both on a pointer to the name hash — Q14.4 (the
  inner `0x0084a6c0`/`0x0084a710`/`0x0084a7f0` stay OPEN, as do `0x006cec60`/`0x006cfdb0`).
- §23.19: player `+0x209c` = the player's current crib (stronghold object), not a garage-vehicle-list pointer — Q14.3.
- §4.5 / §18.31 #1–#2: no fallback on a stale cached pair; case-insensitive vehicle-name lookup; `0x00458230` = the id-pair hash
  lookup on `0x024433a8` with `0x031d152c` a dead parameter; `0x00a280c0`'s second argument is a "found" out-byte — Q16.

## Next dumps (depth 1 unless noted; in order of payoff)

1. **§28.15 crib cluster:** `func 0x00bd2950 0x00bd3510 0x005e8b30 0x008d6ba0 0x008da510 0x008da550 0x004d47e0 0x006055d0
   0x00710910 0x0084a6c0 0x0084a710 0x0084a7f0 0x009601d0`; the opcode-`0x45` receiver (search the handler registrations of
   `0x008adbb0`'s callees for `0x45`, then dump it — it names sub-types 2/3/`0x1d`); `ptrs 0x012f5930 210` (names of tutorials
   7–10 and 100); `xref 0x014a0f80` (the unlocked-stronghold counter's readers).
2. **§28.23 animation:** `func 0x004bce30 0x009ba300 0x00d9e1e0 0x004cbd20 0x004ccf30 0x004ae7a0 0x004aea30` (the last two fill
   the `0x02ef2178` rows and will name the `+0x2c` bits and bytes `+0x24`/`+0x25`/`+0x27`).
3. **Q1 / §2.10 conversation:** `func 0x006de8a0 0x006de8e0 0x0070ba80 0x0046a3f0`; the writer of ctx`+0x24` (the subscribe
   method among `0x008777a0`–`0x00879b60`) and its callers in the `0x006dexxx` cluster.
4. **Q6 send primitives:** `func 0x00874290 0x008741c0 0x008743b0 0x00874240` (reliable vs unreliable); the `0x029443bc`
   writers `0x00bc4fd0`–`0x00bc6d80` (replay-recorder hypothesis; also §6.19 step 2's OPEN).
5. **Q2 friends:** `func 0x0086ff70 0x00870700 0x007c9470 0x007c9710 0x0088dcf0 0x0088de50 0x00d34d20`; the writer of R`+0x88`
   inside `0x007c9710` names its presence bits.
6. **Q4 destroy:** `func 0x00884760 0x00457130 0x00457010 0x004bf550 0x00457030`; `xref 0x011645cb 0x011645cc` (who else tests
   the category tags `0x22`/`0x24`).
7. **Q5 autosave:** `func 0x006f98e0 0x006f9a20 0x00b94d50 0x00b94f70 0x0071b640 0x007dab20`; define and dump the undefined
   code at `0x007af5fd` (the only clearer of `0x012fcadc`).
8. **Q3 camera:** `func 0x00568de0 0x004cd740 0x00e23310`; define and dump `0x00a9ae5f` (the camera-table allocator).
9. **Q8 / Q12 singletons:** define and dump `0x006a3683` (minigame creator); `func 0x008777a0 0x008778b0 0x008780a0`
   (context register/unregister); `xref 0x014b2ddc` writers and the writer of the mission object's `+0x164`.
10. **Q9 debris flow:** `func 0x006fcb00 0x006fcc80 0x00b71950 0x00b71970 0x00a874f0 0x00a77d70 0x00ab89a0 0x00ac04a0 0x00ac0e10
    0x00ab98f0 0x00ac0c90`.
11. **Q10 category matrix:** the receiver `0x0075d4b0` hands to `0x00778bc0` (read its load site) and the filler of its `+0x34`.
12. **Q15 AI record fields:** `func` of the first three helicopter-AI callers in `func1/func_0x00a79470.txt` to name vehicle
    `+0x102` bit `0x8`, `+0x398`, `+0x530`..`+0x538`, `+0x5b0`.
13. **§4.5 NEEDS-EXE (not this job's scope):** `func 0x00a63710` with its two `+0xc68` setters (record-vs-flip order).

## Summary of labels

| item | label |
|---|---|
| Q1.1 `0x00a3c9d0` = `audio_conversation_play(id)` → `0x006df210`(id, 3) | CONFIRMED — disassembly |
| Q1.2 opcode-`0x51` path = host-gated broadcast record (not a UI-queue message); `0x008788e0`'s receiver = the conversation screen context `0x014e9ad0` | CORRECTED / CONFIRMED; ring membership HIGH CONFIDENCE |
| Q1.3 `0x008788e0` = state change: opcode-`0x1a` record {ctx id, 4, state} per member + local event-4 dispatch | CONFIRMED — disassembly |
| Q1.4 `0x008779b0` per-member send (`+0x161` set, `+0x0` == 0); `0x00877860` = four 12-byte subscriber slots at ctx`+0x24` | CONFIRMED; member-byte meanings HYPOTHESIS |
| Q6 `0x0086f1b0` = broadcast to every session member, `0x0086f110` = one addressee (the spec's labels are reversed) | CORRECTED — disassembly; `0x00874290`/`0x008741c0` OPEN |
| Q2 the player records = the Steam friends list; filler `0x007c9470`/`0x007c9710` | CONFIRMED — xref (filler) / HIGH CONFIDENCE (identity) |
| Q2.1 `0x0088e690` = queue a session invite through `0x024d853c`; 15 s throttle table | CONFIRMED; `0x0086ff70` OPEN |
| Q2.2 `0x0088dfd0` = "no invite to this friend less than 15 s old" | CONFIRMED — disassembly |
| Q2.3 `0x0088e040` = join the friend's lobby if they are in app 55230 with a valid lobby | CONFIRMED (structure) / HIGH CONFIDENCE (Steam calls) |
| Q2.4 `0x00870810` = Steam overlay `"steamid"` page | CONFIRMED — disassembly |
| Q4 `0x00853ea0`(object, force, keep) = destroy primitive: replication guard, category-list removal, `+0x33` bits 2/4 | CONFIRMED; tags `0x22`/`0x24` and `0x00884760` OPEN |
| Q5.1 `0x006f8370` = read of byte `0x014f71d4` | CONFIRMED; identity OPEN |
| Q5.2 `0x00b94ff0` = perform (`0x00b96230` → `0x00b94d50`) or defer (`0x0290ceda`) the autosave; `0x02282d1c` inert | CONFIRMED; retry `0x00b94f70` HIGH CONFIDENCE |
| Q5.3 `0x012fcadc` cleared at `0x007af5fd`, set by `0x007af660`/`0x007af6c0`; `0x0290ceca`/`0x0229a317` writer sets | CONFIRMED — xref; names HYPOTHESIS / OPEN |
| Q3.1 `0x00a9afd0` = row of the global camera table (`0x027b3470`, `0xad0`-byte rows) keyed by the definition's name or `+0x8fc` override, `"default"` fallback | CORRECTED — disassembly |
| Q3.2 `0x00a9b080`: presets `0x3c` apart with two `0x1c` variants; distance/height along the current bearing; `0x00568de0` applies | CONFIRMED (geometry) / CORRECTED (stride) / OPEN (apply call, orientation vectors) |
| Q7.1 `0x007c4c90` = four dialog slots, the element pool, the `dialog` document | CONFIRMED — disassembly |
| Q7.2 `0x007c3310` = pop a free slot, insert by priority; `0x02282d14` = free ring, `0x02282d10` = open ring (closes §27.11) | CONFIRMED — disassembly |
| Q7.3 `0x007c18a0` / `0x007c2ac0` = body text / button; `0x00e22a30` = zero-safe UTF-16 encoding | CONFIRMED — disassembly |
| Q7.4 `0x007c3d80`(title, body, priority, result callback, button label) | CONFIRMED — disassembly |
| Q8.1 `0x014c2d10` minigame singleton: created at `0x006a3683`, cleared by `0x006a4440` | CONFIRMED — xref; creator OPEN |
| Q8.2 `0x024d78e0` ring written by `0x008777a0`/`0x008778b0`/`0x008780a0` | CONFIRMED — xref; register/unregister HIGH CONFIDENCE |
| Q8.3 `0x014ff6c1`/`0x014ff6c2` writer set complete (`0x00703f50` clears both) | CONFIRMED — xref |
| Q8.4 `0x012f3b48` read only by `0x006f1f80` | CONFIRMED — xref |
| Q9 recycle `0x006fd4f0` = respawn at a random point of the flow's annulus; nothing is pooled | CORRECTED — disassembly; vehicle helpers OPEN |
| Q10.1 `0x00778bc0` = 128 × 128 bit-matrix lookup; category 47 is table content | CONFIRMED (lookup) / OPEN (matrix source, receiver) |
| Q10.2 `0x00dbdc90` oriented box, `0x00dbe2a0` sphere, `0x00dbe050` capped cylinder | CONFIRMED — disassembly |
| Q11 `0x00ad3b40` = the body touches a null-id partner (world); `0x00ad3c50` = a wheel reports contact | CONFIRMED (loops) / HIGH CONFIDENCE and HYPOTHESIS (names) |
| Q12 `0x00d34d40` = `return 1` stub; `0x006d2910` = the mission's `+0x164` sub-object, slot 2; `0x00614cb0` = `[[0x014b2ddc]+0x1c]+0x1c` | CONFIRMED; object identities OPEN |
| Q13.1 `0x00943a20` = combat-ready (weapon-raised) stance predicate; `0x0130ef98` inert | CONFIRMED (tests) / HIGH CONFIDENCE (name) |
| Q13.2 `0x004b1b90` = start a state on the controller and its children: slot reuse, row lookup, managed kind; f1–f3 and bit `0x80` go to `0x004bce30` | CONFIRMED (structure) / OPEN (the floats' and bit `0x80`'s meaning) |
| Q14.1 `0x007161a0` = arm a tutorial entry (7–10 on unlock) | CONFIRMED — disassembly |
| Q14.2 `0x005eea30` / `0x0071ccf0` = unlock the level's phone contacts (`0x01522750` rows) | CONFIRMED / HIGH CONFIDENCE (table identity) |
| Q14.3 `0x005f1450` trigger groups (`0x0093d320`/`0x0093bde0`, hidden this = handle) and door callbacks; player `+0x209c` = current crib; `0x005f0b30` → `0x008d6ba0` | CONFIRMED (structure) / OPEN (door helpers, polarity) |
| Q14.4 `0x005f1750` = set the upgrade level (host only); `0x0084a910`/`0x0084a940` = zone swap on/off; opcode `0x45` sub-type 3 | CONFIRMED / HIGH CONFIDENCE (swap-name roles) |
| Q14.5 `0x00b98d00` = save-table row {locked bit, level, variant} | CONFIRMED (mechanism) / HIGH CONFIDENCE (identity) |
| Q14.6 `0x005e8fb0` = opcode-`0x45` sub-type `0x1d` + `0x005e8b30` on the owned `0x03171a64` entries | CONFIRMED (structure) / OPEN (effect) |
| Q15 `0x00a79470` = vehicle + `0x100`; the two type tests are a stripped assertion | CONFIRMED — disassembly |
| Q16 `0x00a281e0` has no fallback on a stale pair; `0x004dcf00` = id-pair hash on `0x024433a8` (`0x031d152c` dead); `0x0062a190` = case-insensitive registry lookup; `0x00a280c0`'s 2nd argument = found out-byte | CONFIRMED — disassembly |

Tally: 16 questions / 42 sub-items over all 55 addresses of the job (40 `func1/` roots, 3 `func2/` roots, 12 `glob/`
listings); CORRECTED 5 (Q1.2, Q6, Q3.1, Q3.2's stride, Q9); every other sub-item CONFIRMED at the mechanism level; no
address is unanswered. What remains OPEN is the undumped-callee residue in "Next dumps" — no spec entry is contradicted by an
unread body, and no entry's registered name or address was wrong.

