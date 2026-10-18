# Saints Row: The Third — AI Runtime: Per-Character Update and the Mode Machine

**Prepared by:** SPEC TEAM (cleanroom reverse-engineering process)
**Phase:** AI runtime, unit A0 of the proposed order in `review/scoping-ai.md` §4. **Unit A1** (the
decision step: gate, goal/action chooser, the 17 goal tests, the action table) was added on 2026-10-03
as §9.
**Scope:** The per-character AI update (`0x004e8d00`), the 44-entry mode table and its 8 callback
slots, the three-layer mode-switching protocol (request → apply → transition), and the completion /
hand-off path back to the decision layer. This file covers the **runtime code contract** — what a
replacement engine must reproduce to drive the same mode machine. It does not cover the `.xtbl`-driven
behavior/personality override DATA (`spec-ai-behavior-format.md`), ~~the decision step's own goal-choice
logic (A1),~~ perception (A2), or navigation/pathfinding (not opened by this unit — `.czn_pc` zone data
and navmesh/path-planning code stay out of scope throughout). **[A1: the decision step's goal-choice
logic is now covered by §9; ~~perception's write side remains A2.~~]** **[A2, 2026-10-03: perception's
write side is now covered by §10.]**
**Method:** direct Ghidra disassembly, headless, read-only, against a private disposable project copy
(deleted on completion); `func`/`range`/`ptrs`/`xref`/`calls` dump modes, plus a private push-argument
survey script (lists, for each call site of a target, the containing function and the preceding PUSH
operands). Every function named in §1–§6 was read in full in its own listing unless marked otherwise.
**Cleanroom compliance:** no decompiled code, decompiler variable names, or pasted pseudocode appear
anywhere below; no original Volition identifiers are used as identifiers — every function and field is
cited by its plain hex address or offset. Mode names (`Idle`, `Panic`, …) and sub-state names (`attack`,
`sidewalk`, …) are the engine's own strings, read from its own static tables, quoted as data — the same
convention this project applies to every other named-string table it has documented.
**Confidence key:** **CONFIRMED** (read directly in a listing, or a static table read directly this
pass); **HIGH CONFIDENCE** (follows from a dumped instruction or reference list, but the target body was
not read in full); **HYPOTHESIS** (plausible, not settled); **OPEN** (collected in §8, with a concrete
next step where one exists).
**Relationship to other specs:** `spec-ai-behavior-format.md` documents the `.xtbl` behavior/
personality-override schema (the DATA side); this file documents the executable's own mode-dispatch
machinery that data ultimately feeds (the CODE side) — the two are complementary, not overlapping.
`spec-lua-api-behaviour.md` §30.4's open question (whether NPC AI is a client of the scripted-request
pool `0x006f62b0`/`0x004f4d00`/`0x004f4b90`) is settled by §4.4 below: goal-driven decisions commit in
place and never touch that pool; only externally-ordered (Lua/mission/vehicle) actions use it.
`review/scoping-ai.md` is the scoping note this unit refines; every correction to it is flagged
**[corrects scoping]** at the point it applies.

---

## 1. The per-character AI update `0x004e8d00`, in full

### 1.1 How it is reached (once per human per frame)

CONFIRMED by listing of each link:

- The world update `0x00702a50` calls two per-frame human passes, in this order: `0x009ea630` (call at
  `0x00702b84`) and then `0x0099bba0` (call at `0x00702b90`).
- Both passes walk index lists held by the object manager whose pointer is the global `0x03171a64`. Each
  list is an array of 16-bit indices (`+0x1fc` with count `+0x204`; `+0x1f0` with count `+0x1f8`;
  `+0x1e4` with count `+0x1ec`) into the manager's pointer table at `+0x58`.
  - `0x0099bba0` first walks the `+0x1e4` list (a per-object step unrelated to AI), then calls
    `0x009d94a0`. That function **resets the AI budget once per frame** (`0x004e43b0`, §1.4), then walks
    the `+0x1fc` list. For each entry it calls `0x004f5ae0`, `0x004f6160`, `0x009d9190` and `0x00994dd0`.
    `0x009d9190` takes the human in a register.
  - `0x009d9190`: if the human's byte `+0x3b` bit 0 is clear, it runs some health and timer upkeep and
    then calls the per-human update `0x0099bcb0`. If that bit is set, it instead calls `0x00952760`,
    but only when byte `+0xe3` bit 0 is set and the human is locally owned.
  - `0x009ea630` walks the `+0x1f0` list. For each entry whose byte `+0x33` bit 3 is set, it calls
    `0x009e91c0` (a large function, body `0x009e91c0`–`0x009ea628`), which calls `0x0099bcb0`
    part-way through its body.
    HYPOTHESIS: this second list holds the player-controlled humans and the `+0x1fc` list holds the
    others — not independently checked (§8).
- `0x0099bcb0(human, dt)` returns early only if the object is unusable (`0x00853b10`: null, flag `+0x33`
  bit 2 set, or type byte `+0x34` = `0xff`) or if `+0x33` bit 3 is clear. Otherwise it calls the AI
  update `0x004e8d00(human)` **unconditionally**, once, near the end of its body (call at
  `0x0099c79b`). Nothing in between can skip it.
- A third route exists: `0x00952760` → `0x00af4150`. In one branch (its state word `+0x16d4` = 3,
  with a live linked object at handle `+0x16c0`) `0x00af4150` looks up a helper object
  (`0x004d7e00`). On that object it clears byte `+0x1da6` bit `0x10` and sets bit `0x20`, then calls the
  AI update directly. It sets those bits so that call is exempt from the throttle (§1.4). Route
  identity: HYPOTHESIS (a carried or attached-character special case).

So the AI update runs **at most once per human per frame from the normal path**. There are two
pre-conditions: the human sits on one of the two manager lists, and its `+0x33` bit 3 is set (HIGH: an
"active/simulated" flag). A human on both lists, or one also reached through the `0x00af4150` path, could
be updated twice in a frame — not checked (§8).

### 1.2 Entry checks

The single argument is the human. Order, CONFIRMED:

1. It computes a "human-typed" pointer, used later for the throttle and the tail calls. This is the
   human itself when its type descriptor (the table `0x02cc9900` indexed by type byte `+0x34`) has bit 2
   of byte `+0xa`; otherwise it is null.
2. If `0x00722000()` returns true, it returns at once. That function tests a global state counter
   (`0x0153b520` > 1) and a pointer (`0x0153b528`). HIGH: a global pause or loading condition.
3. If the human is unusable (`0x00853b10`), it returns.
4. **Network authority split:** `0x008addb0(human, 0)`. For a non-null object it calls `0x00884760` on
   the object's network record (`+0x3c`). That compares the record's owner byte with the local peer id,
   or answers true when there is no session. HIGH: "is this object locally owned".

### 1.3 Non-authority (proxy) branch

CONFIRMED by listing. This branch never chooses, never ticks a mode, and never touches the budget:

- It resolves the handle at `+0x1330` (`0x004e58a0`: a 64-bit handle resolved through `0x00458230`; the
  result must be a live object whose type has bit 0 of `+0xa`). HYPOTHESIS: the current combat target. **[A1, §9.8: raised to HIGH.]**
- If a target exists and the current mode is `Combat Movement` (5) or `Fire spray` (12), it calls
  `0x00963570` on the target's `+0x1330` sub-object. The arguments are a flag computed from the
  human's ~~vehicle~~ held item **[A1, §9.10 item 1]** (`0x00941f40` → `+0x19c` → `0x00b83a00`) and "mode is Fire spray".
- `0x00963590` on its own `+0x1330`, then `0x00b6ccb0` on the human-typed pointer (if any), then
  `0x004e8310`, the pending-mode applier of §3.2.

So a proxy's mode is driven by replication (the network read of §2.4), but a mode **request** made on
a proxy is still forwarded by the applier.

### 1.4 Authority branch, in order

CONFIRMED by listing:

1. **Bail-outs.**
   - If `0x008cc4d0` (byte `+0x3b` bit 0) is set, or `+0x33` bit 3 is clear, the update calls
     `0x00963590` on `+0x1330`, then `0x004e5d20` on the AI sub-object at `+0x2b0` with the null pair
     (the constant pair at `0x01115d50`/`0x01115d54`, zero in the file image). Then it returns;
     nothing else runs.
   - If `0x0096f4f0` holds (`+0xcc8` = 5, or `+0xe3` bit 0), it runs `0x004e89b0` and returns. That
     function first calls the `Stunned` begin `0x0050bdc0` (§3.3) and `0x004e5d20(null pair)`, then
     some animation and physics follow-up not read in detail. HIGH: a dead or incapacitated character.
2. **Target upkeep.** It resolves the `+0x1330` handle and the human's vehicle (`0x00971cf0`) **[A1, §9.10 item 1:
   `0x00971cf0` is a class test that returns the human itself; the "vehicle" below is the human]**. If there
   is a vehicle and that vehicle's `+0x13a8` timer has run past 2000 ms (or is not running), it restarts
   the timer and refreshes the vehicle's target through `0x004e4ce0`/`0x004e43f0`/`0x009659f0`. Then,
   if there is a target and byte `+0x770` ≥ 6, it calls `0x004fc6d0` on the `+0x510` sub-object with
   the target handle, and passes the result to `0x00963570` on the target's `+0x1330`. HYPOTHESIS:
   threat or engagement registration on the target. Then `0x00963590` on its own `+0x1330`.
3. **"May think" gate `0x004e8b10`.** If it returns false, the update calls the event pump in
   flag-only mode (`0x004f5b90(human, 1)`, §2.3) and returns. No budget is used, no perception, no
   decision, no mode tick. The gate returns false in these cases (CONFIRMED by listing; the identity
   of each test is HYPOTHESIS):
   - a global pause condition (`0x007220c0`/`0x00722000`);
   - `0x009a1dc0` holds and `0x0096f4f0` does not. In that case the gate itself first runs the
     pending-mode applier `0x004e8310`;
   - either of two flags filled in by `0x009b8140(human, &a, &b, -1)` is false;
   - `+0x16b4` < `+0x16b8`;
   - the human is in one of four states tested by `0x00985d80` (5, 4, 6, 8). In that case the gate
     first runs `Follow`'s tick `0x0054c100` once if the current action is 36 and that action's mode
     (`0x004f28d0`) is the current mode. It then returns true only in mode `Player` (43);
   - `0x0095d740` holds;
   - one of two object-flag tests (`0x004b4ce0` with `0x2be` / `0x408`) holds.

   One further branch can also end in false. If `0x00853b30(human, 0x22)` and `0x009b5690` hold, the
   mode is not `Return to shore` (28), and an `Idle`-sub-state-26 vehicle check fails, the gate calls
   `0x0052fa40`. That function is the `Return to shore` begin function: it enters mode 28 through the
   transition primitive (§3.3). If the switch happened, the gate returns false.
4. **AI-budget throttle** (CONFIRMED by listing; the per-frame reset is CONFIRMED in `0x004e43b0`).
   - **Globals:** `0x013576d0` is the count of characters that have thought this frame; `0x013576d4`
     is the frame's budget; `0x013576c6` is the "over half budget" latch byte.
   - **Reset:** once per frame, before the `+0x1fc` list is walked, `0x004e43b0` sets count = 0,
     budget = the dword at `0x02442954`, and latch = 0.
   - **The budget source:** `0x02442954` is never written through its absolute address. Its only other
     reader, `0x009d4b60`, uses it as the upper bound of a random pick over the human list. HIGH: the
     number of live AI humans, maintained as a field of a larger structure. Not independently traced
     (§8).
   - **Who is throttled.** A human is eligible only if all of these hold:
     - it is human-typed;
     - it is not in a vehicle that passes `0x00536300` (vehicle at `0x00941f40`, test `0x00b81790` on
       vehicle `+0x19c`); **[A1, §9.10 item 1: HIGH — `0x00941f40` is the held item, and `0x00b81790`
       tests its weapon-info flag `+0x10` bit `0x80000`]**
     - its mode is neither `Brute Melee` (3) nor `Brute Stomp` (2);
     - byte `+0x1da6` bit `0x20` (exempt) is clear.

     Ineligible humans skip the throttle and always think.
   - **The rule for an eligible human:**
     1. If the latch is already set, set its `+0x1da6` bit `0x10`.
     2. Otherwise, if count > budget / 2 (signed halving), set the latch, and do not set the bit for
        this human.
     3. Then, if bit `0x10` is set: clear it and **skip this frame**. If it is clear: set it and
        **think**.

   **[Added 2026-10-03, `spec-player-runtime.md` §5 (U3, the locomotion bridge): the throttle does not
   throttle locomotion.** A skipped human's *decision* step (this section) does not run, but its
   movement is driven by a wholly separate path — the velocity composer (`0x009989b0` →
   `spec-player-runtime.md` §5.4) and the controller step (§5.6), which walk the object manager's `+0x1e4`
   "all humans" list independently of the AI update and of this throttle. A throttled NPC keeps moving on
   its last-committed intent (`+0xa50`/`+0xa60`, set through the shared setter `0x00942110` — argument
   `-1` for speed means "use the animation's own speed", and `+0xcb0` through `0x00987360`) until its
   next decision frame changes it; it does not freeze. The per-frame bit `+0xdc` bit `0x100000` that
   gates the post-move pass (`spec-player-runtime.md` §5.7) is set by the composer's own
   `0x009a02a0` and cleared per NPC at `0x009d91a4` inside `0x009d9190` (§1.1 above) — i.e. independent
   of whether that human thought this frame. NPC intent itself is shaped by `0x009918b0`
   (`spec-player-runtime.md` §5.4.3 step 5: acceleration- and turn-rate-limited, distinct from the
   player's own shaping), and a far, off-screen NPC additionally has its controller step skipped
   outright by the NPC-tail bit `+0x1da5` bit `0x80` (`spec-player-runtime.md` §5.6.3) — a separate LOD
   mechanism from this section's own think-throttle, acting on movement rather than decisions.]**

     So bit `0x10` means "thought last time". With the latch clear, an eligible human thinks every other
     frame, alternating. Once more than half the budget has thought in a frame, every later eligible
     human skips that frame and is left with the bit clear, so it is guaranteed to think on its next
     visit.
   - **What a skipped human still does:**
     - the event pump runs in flag-only mode (`0x004f5b90(human, 1)`);
     - if it has a vehicle object (`0x00941f40` non-null; ~~HIGH: the human's vehicle, since its `+0x19c`
       feeds the vehicle tests elsewhere~~ **[A1, §9.10 item 1: the held item]**), and not in the case "`+0xac8` non-zero and vehicle `+0x1a0`
       low 2 bits clear and vehicle `+0x1a1` bit 7 clear", it runs `0x004ddb60` (calls `0x004f8a50` when
       the `+0x510` sub-object's `+0x20c` is non-null) and `0x00b6ccb0`;
     - then it returns.

     It gets **no perception tick, no decision, no pending-mode apply and no mode tick** that frame.
     Its mode keeps whatever state the last tick left.
   - The counter is incremented for **every** human that reaches the think path, eligible or not.
5. **The think path** (CONFIRMED), in this exact order:
   1. count `0x013576d0` += 1;
   2. **perception tick:** `0x00507520` with `this` = human `+0x510`;
   3. **event pump:** `0x004f5b90(human, pending)`, where `pending` = `+0x317` bit 3. Events are
      therefore delivered only when no mode switch is pending (§2.3);
   4. **decision step** `0x004f5210(human)`, which may request a mode (decision-layer material; its
      hand-off is in §4.3);
   5. **pending-mode applier** `0x004e8310(human)`, so a mode requested by the decision step is
      entered **in the same frame**, before the tick;
   6. a guard: if the dword at `[[human+0xd24]+0x10]+0x50` is zero, return now. This skips the mode
      tick, the upkeep pair of step 8 and `0x004dec10`. HIGH: an animation or rig object not yet
      ready;
   7. **mode tick** (§5);
   8. if human-typed: `0x004ddb60` on `+0x510` and `0x00b6ccb0(human)`. This is the same pair a
      skipped human runs when it has a vehicle object. HIGH: a per-frame upkeep pair; `0x00b6ccb0` is
      large and was not read;
   9. **tail `0x004dec10(human)`.** **[corrects scoping]:** this is not an AI step. It runs only for a
      human driving a vehicle that passes `0x00b81440` and is not flagged `0x00853b30(human, 0x21)`.
      It runs driving-request helpers `0x009c7cc0`/`0x009d1aa0`/`0x009d14f0`/`0x009d1520`, chosen by
      bits of `+0x13c5`, and maintains those bits. ~~HIGH: the driver-side hand-off to the vehicle
      system.~~ **[A1, §9.10 item 1: `0x00b81440` tests the held weapon's class, so the tail acts for a
      non-player holding a ranged weapon. HYPOTHESIS: the firing-request hand-off.]**

**[corrects scoping]** on `0x004f5b90`: the scoping note's "decision timer / gate" label is wrong. It is
the per-character **event (stimulus) pump** that delivers queued events to the current mode's slot
`+0x10` (§2.3). It is called three times in the update with a "flag only" argument of 1 (skip paths)
or the pending bit (think path).

**[corrects scoping]** on `0x004dec10`: not an AI step at all — see step 9 above.

**[corrects scoping]** on `0x004f3eb0`/`0x004f5440`: these are the **network write/read of the
committed action** (§2.1), not an independent idiom.

---

## 2. The 44-entry mode table `0x012e1538`

### 2.1 Layout and the role of each slot

CONFIRMED by static read: 44 rows × `0x24` bytes = `0x012e1538`…`0x012e1b67`, file-backed. Row
`+0x00` is the name pointer; `+0x04`…`+0x20` are eight function slots, each either a function or null.
Mode index `0x2d` (45) is the engine's "no mode" value: the update, the transition primitive, the event
pump and the action-to-mode lookup `0x004f28f0` all treat it as "none". The setter rejects any index
≥ `0x2c`.

**Every slot column has exactly one reader in the executable** (CONFIRMED: `xref` of each column's
first address, plus a raw displacement scan). That makes the roles below CONFIRMED from the reader
side. They were then checked against the bodies of four to six modes per slot:

| Slot | Sole reader | Call shape | Role |
|---|---|---|---|
| `+0x00` | `0x004dea00` | — | Mode name. A debug formatter prints `"<mode>: <sub-state>[ drunk]"`. The sub-state comes from the 177-entry name table `0x012e1168` (§2.5); "drunk" is `+0x2bb` bit 7. |
| `+0x04` | `0x004dd380` (transition primitive) | `enter(human)`, cdecl | **Enter.** Called after the new mode is installed (§3.3). |
| `+0x08` | `0x004dd380` | `exit(human, new_mode)`, cdecl | **Exit.** Called on the old mode before the switch, with the mode being entered. Example: the `Combat Movement` exit `0x005383d0` does its clean-up (`0x004de1f0`) only when `new_mode` ≠ 5. |
| `+0x0c` | `0x004e8d00` (AI update) | `tick(human)`, cdecl | **Per-frame tick** (§5). |
| `+0x10` | `0x004ec4d0` (called only by the event pump `0x004f5b90`) | `int on_event(human, event)` | **Event handler.** It returns a disposition code (§2.3); 0 means "not handled here, use the default handler". |
| `+0x14` | `0x004ea530` (network read) | `init_params(block)` | **Initialise the `0x90`-byte parameter block** to the mode's defaults. It is also called directly by begin functions on their local block before entering. Examples: `Idle`'s `0x0050f520` is called on a stack block by the `Idle` begin `0x00512f90` and by `0x00a34c30`; `Panic`'s `0x004ee590` is called by the `Panic` begin `0x004ef450`. The network read calls it on the live block `human+0x8d0`. |
| `+0x18` | `0x004e61b0` (network write of the human's AI state) | `net_write(human, stream)` | **Serialise mode-specific state.** Examples: `Panic` `0x004eecd0` writes fields `+0x8d0…+0x8f4` with the bit-stream writers `0x00881110`/`0x00881040`; the shared `0x00523ca0` writes byte `+0x8d0` and the handle at `+0x8d8`. |
| `+0x1c` | `0x004ea530` (network read, the counterpart of `0x004e61b0`) | `net_read(human, arg, stream)` | **Deserialise mode-specific state** after the generic fields, the mode byte, the sub-state byte and the action block have been read. |
| `+0x20` | `0x004ea9b0` (a network message handler, referenced only from the table at `0x01169604`) | `on_remote_request(stream, arg)` | **Receive a remote "enter this mode" request.** The handler reads a mode byte; if the mode is < `0x2c` and has slot `+0x20`, that slot reads the rest. Otherwise the default reads a 2-byte character id (`0x004db850`), requests the mode through the setter and, if accepted, reads the action block into `+0x450`. Example: `Panic`'s `0x004efc10` reads a character id and calls the `Panic` begin `0x004ef450`. |

The shared pair `0x004f3eb0`/`0x004f5440` used in all decision-driven modes is the **network write/read
of the committed action** (CONFIRMED by listing):

- `0x004f3eb0` writes a validity bit. If the bit is set, it also writes the reason byte `+0x444`
  (1 byte) and the committed block `+0x450` (`0x004e5900`).
- `0x004f5440` reads the same. With no stream, it falls back to `0x004f50b0(human, 0)`.

### 2.2 Shared stubs in the slot columns

CONFIRMED by listing:

- `0x00754410`: the project's known single-`RET` no-op. It fills empty `+0x08`, `+0x14`, `+0x18` and
  `+0x1c` slots, and the `Riot cop` tick. Callers treat a nop slot exactly like a filled one.
- Three constant-return stubs are used **only** in the `+0x10` column. Each is two instructions,
  "load constant, return", shared with unrelated code by identical-code folding:
  - `0x00682c50` returns 1: `Aim`, `Hostage`, `Streaking`, `Veh clear door`, `Player`;
  - `0x006147f0` returns 3: `Squirted`;
  - `0x0066ac20` returns 4: `Weapon throw`.

  These constant handlers are what pins the meaning of the event-handler return codes (§2.3).
- `0x00523ca0` is a shared `+0x18` net-write for `Cower`, `Holdup` and `Veh threat`. `0x00520cd0` is a
  shared exit for `Gunfire wary` and `Veh enter`. It sends the human's own handle through
  `0x004f6420(0x5a, …)`.
- `Veh trans` uses the same function `0x00524420` for both `+0x04` (enter) and `+0x1c` (net read). The
  body reads only its first argument. Depending on `0x009b9210`, it sets the sub-state to `entering`
  (173) or `exiting` (174), so both uses behave as "re-derive the sub-state".

### 2.3 The event pump and the event-handler return codes

CONFIRMED by listing of `0x004f5b90`, `0x004ec4d0` and the jump table at `0x004f5c38`. Where this is
called from in the update: §1.4.

- **The event list.** Each human holds a circular singly-linked list of event records, head at
  `+0x1c54`, link at record `+0x00`. Each record has a kind byte at `+0x08`; the value `0x77` means
  "consumed". It also has:
  - a timer at `+0x24`;
  - a mode filter byte at `+0x28` (`0x2d` = any mode);
  - a sub-state filter byte at `+0x29` (`0xb2` = any sub-state);
  - a flags byte at `+0x2a`.

  The pump walks the list once per call and skips records already marked `0x77`. For every live record
  it sets flag `+0x2a` bit 0, then:
  - if the human is not locally owned, the record is marked consumed;
  - if the "flag only" argument is set, or the record's timer has not expired, the record is left
    as is;
  - if the record's mode or sub-state filter does not match the human's current `+0x501`/`+0x500`, it
    is marked consumed. So an event aimed at a mode the human has already left is dropped;
  - otherwise it is delivered through `0x004ec4d0(human, record)`.
- **Delivery `0x004ec4d0`.**
  - It returns 2 at once if the human is not locally owned, has mode `0x2d`, or is unusable.
  - Otherwise it calls the current mode's slot `+0x10` (if present). A non-zero result is returned as is.
  - On zero (or no slot) it calls the default handler `0x004ebc70` (thiscall on the record, human as
    argument; a large body, `0x004ebc70`–`0x004ec4c0` region, not read in full). A zero result from the
    default handler becomes 2.
- **Disposition codes** (the pump's switch on the result):

  | Code | Effect on the record |
  |---|---|
  | 1 | marked consumed (handled) |
  | 2 | marked consumed (not applicable / rejected) |
  | 3 | **kept**, re-offered on a later pump |
  | 4 | **kept**, and its timer is re-armed to a random 250–750 ms (`0x00d9e180(250, 750)`) |
  | any other value | marked consumed |

  So `Squirted` defers every event, `Weapon throw` defers every event with a back-off, and the five
  return-1 modes swallow every event, so the default handler never runs for them.
- The default handler `0x004ebc70` calls the mode setter at least twice (requests `Idle` and
  `Veh evade` (39), per the push survey). HIGH: the default reaction table that turns a stimulus into a
  reactive mode. Not read in full (§8).

### 2.4 Network read of the mode state

CONFIRMED by listing of `0x004ea530`, reached from `0x0089e580`.

This is the only place outside the transition primitive that writes the current-mode byte `+0x501`
(the setter writes the *requested* byte `+0x504`). The read runs in this order:

1. It returns at once if `+0x503` = 1 (the "just entered" flag, §3.3).
2. It reads `+0x315` (3 bytes) and `+0x2b8`/`+0x2bc` (6 bytes), and clears the pending bit `+0x317`
   bit 3.
3. It reads the **mode** into `+0x501`, the **sub-state** into `+0x500`, then `+0x314`.
4. Some target and action fields follow, including the staged block when the ordered bit is set.
5. It re-validates handles.
6. A vehicle re-check: if the human is in a vehicle and the mode is not one of nine vehicle-compatible
   modes (`0x004dd410`, §3.4), it runs the `Passenger` begin. If it is not in a vehicle and the mode is
   `Hostage` or `Passenger`, it requests `Idle`.
7. Otherwise it calls the mode's `+0x14` on the live parameter block, then its `+0x1c`.

So a proxy changes mode **without** running exit or enter slots (HIGH: deliberate, since proxies only
mirror state).

### 2.5 The sub-state byte `+0x500`

CONFIRMED (name table read, setter listing). The sub-state is one byte shared across all modes, indexed
into a single 177-entry name table at `0x012e1168` (entries 0–176, all engine strings). Examples:
`attack`, `sidewalk`, `wander move`, `in cover`, `cower`, `follow walk`, `entering`, `munchy munchy`.

- The transition primitive sets it to `0xb2` on every mode entry ("none"; the formatter prints values
  ≥ `0xb1` as `INVALID`).
- Handlers set it through `0x004dd2d0(human, expected_mode, sub_state)`. That call does nothing (apart
  from a stripped diagnostic call to the no-op stub) unless the human's current mode equals
  `expected_mode`. On a change it calls two notifiers (`0x0094f720` unless the new value is 36
  `sleeping`; `0x0094f890` unless 37 `sitting`). It also runs `0x0094b070` when leaving `Idle`
  sub-state 26 `special animation`. Examples: the `Player` enter sets sub-state 138 `normal`; the
  `Combat Movement` enter sets 3 `move`; `Aim` enter sets 57 `aiming`.

**Seven further entries confirmed and traced to their real owning tick functions, 2026-10-09 (full detail
§20.2):** 13 `sidewalk start`, 14 `sidewalk`, 15 `sidewalk formation` (all three ticked by `0x0051bc60`,
inside `Idle`); 32 `wander pause`, 33 `wander choose dest`, 34 `wander move` (all three ticked by
`0x0051c860`, inside `Idle`); 35 `wander to sidewalk` (a one-frame transitional no-op, not separately
ticked). These, plus the already-documented 36 `sleeping`/37 `sitting` exceptions above, are the only
sub-states `Idle` dispatches specially — every other value (including 10, 11, 35-56) falls to ordinary
per-frame `Idle` bookkeeping.

---

## 3. Mode switching: request, apply, transition

**[corrects scoping]:** the scoping note described one function, `0x004e4130`, as "the mode setter". It
is in fact the first of **three layers**: a **request** (`0x004e4130`), an **apply** step that runs once
per AI update (`0x004e8310`), and the **transition primitive** that actually swaps modes and runs exit
and enter (`0x004dd380`). Between apply and transition sit per-mode **begin functions** that build a
parameter block.

### 3.1 Request: `0x004e4130(human, mode)` → bool

CONFIRMED by listing (46 instructions):

1. **Rejections**, each returning false: a null human; `mode` ≥ `0x2c`; the acceptance test
   `0x004de110` refuses (§3.4); or a switch to `Stunned` (34) is already pending (`+0x317` bit 3 set
   and `+0x504` = 34). **A pending `Stunned` request cannot be overridden by a later request.** Any other
   pending request is simply overwritten.
2. If `mode` = 0 (`Idle`): it clears `+0x4a6` bit 4 and zeroes the 8-byte handle at `+0x458`.
3. It clears the **pending target handle** at `+0x508`/`+0x50c` (= AI sub-object `+0x2b0` + `+0x258`)
   through `0x004e3830(null-handle)`. If the object is owned elsewhere, `0x004e3830` instead sends a
   `"human_ai_data"`/`"pending_target_handle"` network record to the owner. So a caller that wants
   a targeted mode must write the target handle **after** calling the setter.
4. It sets `+0x317` bit 3 (**switch pending**), clears `+0x44c` bits 0 and 1 (action "finished" and
   bit 1), and stores `mode` into `+0x504` (**requested mode**).
5. It calls `0x008addb0(human, 0)` and discards the result (dead call). It returns true.

**No mode slot is called and the current mode `+0x501` is untouched.** A request is only a request.

### 3.2 Apply: `0x004e8310(human)`

CONFIRMED by listing and by a static read of its two jump tables (`0x004e86ac` byte index,
`0x004e8670` targets).

It is called from three places: the AI update's think path (step 5.5), the AI update's proxy branch,
and the "may think" gate's `0x009a1dc0` branch.

1. If no switch is pending (`+0x317` bit 3 clear), it does nothing.
2. It re-runs the acceptance test on `+0x504`. On refusal it clears the pending bit and stops: **a
   request can be accepted at request time and still be dropped at apply time**.
3. Otherwise it clears the pending bit and sets `+0x317` bit 4 (**switch in progress**). It resolves
   the pending target handle `+0x508` (falling back to the `+0x1330` handle) into an object.
4. It dispatches on the requested mode. The 44 modes fall into three groups:

| Group | Modes | What apply does |
|---|---|---|
| **Generic** (18 modes) | 3 `Brute Melee`, 5 `Combat Movement`, 6 `Commando`, 7 `Cover popout`, 8 `Cover stay`, 12 `Fire spray`, 14 `Follow`, 15 `Grenade throw`, 18 `Hijack`, 22 `Investigate`, 23 `Melee`, 26 `Pickup item`, 27 `Reload`, 31 `Skydiving`, 35 `Taunt`, 36 `Weapon throw`, 41 `Veh trans`, 42 `Zombie eat` | If locally owned: transition primitive `0x004dd380(human, mode, no block)`. If not owned: send a remote mode request instead (message type `0x1b`, built at `0x0086f5f0(0x41)`, carrying the mode byte, the human's id and the committed action block, sent to the owner through `0x008ae020`). The receiving end is the `+0x20` handler of §2.1. |
| **Begin function** (13 modes) | 0 `Idle` → `0x00512f90(human, 0, null handle)`; 9 `Cower` → `0x004d5050(human, target, flags from +0x4a5/+0x4a6)`; 10 `Dbno` → `0x0050bd40`; 13 `Flee` → `0x004da0b0` or `0x004d91a0` (by `+0x4a6` bit 2); 17 `Gunfire wary` → `0x004e8070(human, 0)` then `0x004dbd90(human, target)`; 21 `Human shield` → `0x0050ec80`; 24 `Panic` → `0x004efaf0`; 25 `Passenger` → `0x0051ed00`; 30 `RollerBlader` → `0x00530530(human, target)`; 34 `Stunned` → `0x0050bdc0`; 39 `Veh evade` → `0x005232a0(human, +0x458 handle)`; 40 `Veh threat` → `0x00523db0(human, target, 1)` (if a target) **and then falls through into the `Dbno` begin `0x0050bd40`** (see below); 43 `Player` → `0x004f13a0` | The begin function checks its own preconditions, builds a parameter block (usually with the mode's own `+0x14`), and calls the transition primitive. It may decline, and some enter a *different* mode (§3.3). |
| **Ignored** (13 modes) | 1 `Aim`, 2 `Brute Stomp`, 4 `Bust`, 11 `Dog`, 16 `Gunfire evade`, 19 `Holdup`, 20 `Hostage`, 28 `Return to shore`, 29 `Riot cop`, 32 `Squirted`, 33 `Streaking`, 37 `Veh clear door`, 38 `Veh enter` | Nothing. The request is consumed (pending bit cleared) with **no mode change**. These modes are entered only by calling their begin functions directly (§3.3). |

5. On every path it ends by clearing `+0x317` bit 4 and clearing the pending target handle again
   (`0x004e3830(null)`).

**`Veh threat` fall-through.** The `Veh threat` case falls through into the `Dbno` case. This is
CONFIRMED in the listing: the `Veh threat` target `0x004e8529` runs straight on into `0x004e8540`,
which is also case `Dbno`'s target. A port that matches the binary must therefore run the `Dbno` begin
after the `Veh threat` begin. That begin enters `Dbno` only if the human's handle is locally owned
and `0x009715d0(human)` holds. HYPOTHESIS: a missing `break` in the source, harmless when
`0x009715d0` (likely "is down") is false. A replacement engine may treat this as deliberate chaining
or as a bug to fix; either reproduces the same observable behavior when `0x009715d0` is false.

**Requests for an "ignored" mode do occur.** The push survey found the vehicle-entry routine
`0x00afd4c0` requesting `Veh enter` (38) at `0x00afe0a9`. Apply drops that request as described above.
Why the request is still made is OPEN (§8).

### 3.3 Transition primitive: `0x004dd380(human, new_mode, block_or_null)`

CONFIRMED by listing (33 instructions; exactly 32 call sites, CONFIRMED by raw scan). This is the only
code that performs a real mode change on an owned human. In order:

1. If the current mode `+0x501` ≠ `0x2d` and it has slot `+0x08`: `exit(human, new_mode)`.
2. `+0x502` ← old `+0x501` (**previous mode**); `+0x501` ← `new_mode`; `+0x500` ← `0xb2` (sub-state
   "none"); `+0x503` ← 1 (**"just entered"**; the network read refuses to overwrite a human with this
   flag set).
3. If a parameter block was passed, copy `0x90` bytes (`0x24` dwords) into `+0x8d0`, the **live mode
   parameter block**. With a null block, the live block keeps its old contents.
4. Clear `+0x44c` bits 0 and 1.
5. If `new_mode` ≠ `0x2d` and it has slot `+0x04`: `enter(human)`.

There is **no transition validation here**: any mode can be entered from any mode. All gating sits
in the acceptance test (§3.4) and in each begin function's own checks.

**Callers by mode.** The push survey read the second argument at all 32 call sites:

| Mode | Begin function(s) |
|---|---|
| 0 `Idle` | `0x00512f90` ×2, `0x008c45e0`, `0x00997fa0` |
| 2 `Brute Stomp` | `0x00529970` |
| 4 `Bust` | `0x004d4350` |
| 9 `Cower` | `0x004d5050` |
| 10 `Dbno` | `0x0050bd40` |
| 11 `Dog` | `0x0052ec10` |
| 13 `Flee` | `0x004d91a0`, `0x004da0b0` |
| 17 `Gunfire wary` | `0x004dbd90` |
| 19 `Holdup` | `0x004dc880` |
| 20 `Hostage` | `0x004dcd50` |
| 21 `Human shield` | `0x0050ec80`, and code at `0x00a467e4` (no function defined) |
| 24 `Panic` | `0x004ef450` |
| 25 `Passenger` | `0x0051ed00` |
| 28 `Return to shore` | `0x0052fa40` |
| 29 `Riot cop` | `0x0052fe90` |
| 30 `RollerBlader` | `0x00530530` |
| 31 `Skydiving` | `0x005337e0`, and code at `0x00a5f90f` (no function) |
| 32 `Squirted` | `0x004f2340` |
| 33 `Streaking` | `0x00536d00` |
| 34 `Stunned` | `0x0050bdc0` |
| 37 `Veh clear door` | `0x00520640` |
| 39 `Veh evade` | `0x005232a0` |
| 40 `Veh threat` | `0x00523db0` |
| 43 `Player` | `0x004f13a0` |
| computed | the apply step's generic case (`0x004e8621`); `0x00a34c30` (enters `Idle` with an `Idle`-initialised block) |

**No call site enters `Aim` (1), `Gunfire evade` (16) or `Veh enter` (38).** None of the 32 sites
passes those values, and the two computed sites cannot produce them: the apply step's generic case
never sees them, and `0x00a34c30` passes 0. A raw scan for any reference to their enter and tick
functions (`0x004d2c60`/`0x004d2ca0`, `0x004db120`/`0x004db480`, `0x00521320`/`0x005213d0`) found
only their own table cells.

HIGH: on a locally owned human these three modes are never entered in this build. They can appear only
by replication (the network read of §2.4 writes `+0x501` directly). HYPOTHESIS: leftover modes. A
replacement engine may omit any local-entry path for these three without changing observable behavior,
provided the network-read path still installs them when replicated. OPEN (§8).

Begin functions observed in this pass, CONFIRMED by listing:

- **`Idle` begin `0x00512f90(human, has_point, point_handle)`.** It declines if the human is unusable
  or the acceptance test refuses `Idle`. **It enters `Player` instead** when `0x00853b30(human, 0x21)`
  holds (HIGH: "is a player"), and `Passenger` instead when the human is in a vehicle (`0x009b9160`).
  If not locally owned, it sends message `0x1b` with mode 0. Otherwise it tries vehicle re-entry
  (`0x00521640`/`0x00521700`). Failing that, it initialises a block with `Idle`'s `+0x14` and enters
  `Idle`, optionally seeding a point (`0x00512000`). A second variant also calls `0x004e8070`.
- **`Player` begin `0x004f13a0`:** enters 43 only if locally owned and `0x00853b30(human, 0x21)`.
- **`Stunned` begin `0x0050bdc0`:** enters 34 (null block) unless the human is a player or already
  stunned, and only if its own handle is locally owned (`0x008ae480`).
- **`Human shield` begin `0x0050ec80`:** it resolves a victim from `+0x458` or picks one
  (`0x009660e0`), validates it (`0x009a9830`), stores its handle in `+0x458`, and enters 21.
- **`Panic` begin `0x004efaf0`:** for the local player it calls `0x004ef450` with a saved global
  reason (`0x012e1b84`, then reset to -1). Otherwise it derives a panic reason code 0, 1, 2, 3 or 5
  from several tests, then calls `0x004ef450(human, reason)`. That function initialises `Panic`'s
  block with `+0x14` and enters 24.

### 3.4 Acceptance test `0x004de110(human, mode)`

CONFIRMED by listing. It is called by the request, the apply step, the `Idle` begin and the
`Return to shore` begin. The tests run in order:

1. **Stunned lock.** If current mode is `Stunned` (34) and the sub-state is 9 (`idle`), only `Panic`
   (24) is accepted, and only when `+0x4a4` bit 0 is set.
2. **Captured or locked character.** If `0x0060b530(human)` holds, only `Stunned` is accepted.
3. **Health/state field `+0xcc8`:**

   | `+0xcc8` | Accepted |
   |---|---|
   | 5 or 6 | only `Stunned` |
   | 7 | only `Human shield` (21), `Panic` (24) or `Stunned` (34) |

4. **In a vehicle** (`0x009b9160` or `0x009b91e0`): only the nine **vehicle-compatible modes** of
   `0x004dd410` are accepted: `Grenade throw` 15, `Hijack` 18, `Hostage` 20, `Panic` 24, `Passenger` 25,
   `Reload` 27, `Stunned` 34, `Veh trans` 41, `Player` 43.
5. **On foot:**
   - `Passenger` (25) is accepted only if `0x009b9160` holds (that is, never on foot);
   - `Reload` (27) is refused while in `Flee` (13);
   - everything else is accepted.

So the only mode-to-mode restrictions are: Stunned-lock → Panic, Flee ↛ Reload, and the vehicle
whitelist. Everything else depends on character state, not on the current mode.

### 3.5 Representative request call sites (185 sites; push survey of the mode argument)

CONFIRMED (survey). The reference manager lists 185 call sites. A raw byte scan finds 192 call
instructions; the extra 7 sit in code Ghidra never disassembled (`0x004d56f8`, `0x004d5760`,
`0x004d577b`, `0x00730041`, `0x009714f8`, `0x009717bc`, `0x009719d2`) and were not surveyed.

The literal mode argument at the 185 surveyed sites:

| Requested mode | Sites |
|---|---|
| `Idle` (0) | 95 |
| `Flee` (13) | 25 |
| `Cower` (9) | 18 |
| `Stunned` (34) | 17 |
| `Passenger` (25) | 9 |
| `Gunfire wary` (17) | 4 |
| `Veh evade` (39) | 3 |
| `Panic` (24) | 2 |
| `Skydiving` (31) | 1 |
| `Veh enter` (38) | 1 |
| the action table's category byte (row `+0x04`) | 3: decision step `0x004f5210`, commit writer `0x004f4d00`, and `0x004f3af0` |
| a register | 6: the remote-request default `0x004ea9b0`, `Veh clear door`'s tick `0x00520730`, `0x0060c8f0`, `0x009d7480`, `0x00a67000`, `0x00afbd90` |
| unresolved | 1: `0x005f41c0`, where the argument comes from an indirect call |

That is 11 distinct literal modes. No surveyed site requests any of the 13 "ignored" modes except
`Veh enter`.

Characterised examples:

- **Script/mission-driven.** `0x00a5e290` (the Lua `set_ignore_ai_flag` delegate per
  `spec-lua-api-behaviour.md`) requests `Passenger` (25) at `0x00a5e324` and `Idle` at `0x00a5e348`.
  Code in `0x006b4980`–`0x006b81a0` requests `Idle` (3 sites), `Cower` (3) and `Flee` (2).
- **Combat- or damage-triggered.** `0x009a48e0` requests `Stunned` at `0x009a4e6e`. The per-human
  update `0x0099bcb0` calls it with ten arguments, and `0x00af4150` does too. HYPOTHESIS: a hit or
  ragdoll reaction. Six more functions in the human-object region (`0x0094fa00`, `0x0095f250`,
  `0x0096b720`, `0x009720f0`, `0x009acb20`, `0x009e52e0`) also request `Stunned`; their identities
  were not read. More such sites exist (for example `0x0099b7e0`, `0x00952760`, `0x00a275e0`).
  HYPOTHESIS: knock-down entry is "request Stunned" from many places, which fits the setter's rule
  that a pending Stunned request cannot be overwritten (§3.1).
- **From inside a mode handler.**
  - The `Flee` event handler `0x004dac10` requests `Cower` and `Idle`. The `Gunfire wary` remote
    handler `0x004dc260` requests `Gunfire wary`.
  - The `Idle` tick `0x00513270` requests `Passenger` at `0x005132a2`.
  - The `Panic` tick `0x004ef090` requests `Idle`, `Flee` and `Cower`.
  - The `Investigate` event handler `0x00551150` requests `Gunfire wary`.
  - The `Veh enter` tick `0x005213d0` requests `Passenger` and `Idle`.

  So handlers chain into reactive modes **only through the request layer**, never by calling the
  transition primitive on another mode.
- **Decision layer.** The decision gate `0x004f5180` requests `Skydiving` (31), the only literal there.
  The decision step and the commit writer request the action's category mode (§4.3).

---

## 4. Completion: `0x004f4f50` and the hand-off back to the decision layer

### 4.1 The finish routine `0x004f4f50(human)`

CONFIRMED by listing (98 instructions). It takes only the human; there is no status or success
argument. In order:

1. **Cool-down roll.** Let `a` be the current action `+0x445`. If `a` < 70:
   - roll a random integer between the two words at behaviour-record `+0x22 + 6·a` and
     `+0x24 + 6·a` (`0x00dab660(lo, hi)`; record pointer `+0x31c`; the two words are the
     `ai_behavior.xtbl` behavior record's per-action repeat minimum and maximum, per
     `spec-ai-behavior-format.md`, not re-checked here) **[A1, §9.7: the schema is in
     `spec-tables-traffic-ai.md` §10; the words are `Repeat_min` and `Repeat_max`]**;
   - if the per-action repeat timer `+0x320 + 4·a` has less time remaining than the roll, restart it
     with the roll.

   **The cool-down can only be lengthened by finishing, never shortened.**
2. Set `+0x44c` bit 0 (**action finished**). Restart the timer at `+0x43c` with 0.
3. **Ordered-action completion.** This step runs only if all three hold:
   - the ordered bit `+0x44c` bit 3 is set;
   - the staged block's action id (`+0x4a8` first byte) equals `a`;
   - action `a`'s category mode equals the current mode.

   It then does the following:
   - clear bits 3 and 4 and set bit 2 (**ordered action completed**);
   - complete the request id `+0x4aa` through the request pool (`0x006f66d0(id, own handle)`);
   - reset the staged block `+0x4a8` to the `0x58`-byte template `0x012e14e0`;
   - if the human is human-typed and `+0x1da8` is non-null, find a record keyed `0x14` under
     `[+0x1da8]+0xf8` (`0x004d4b90`). If one exists, signal it with the human (`0x00e0cfb0`) and
     release or advance it (`0x00e0ce60`). HYPOTHESIS: wake a script thread waiting on the order.
4. Always: current action `+0x445` ← `0x47` (none), and the committed block `+0x450` is reset to the
   template.

The 85 call sites (CONFIRMED by survey) all pass just the human, and nearly all are inside mode slot
functions or their helpers in `0x004f04c0`–`0x00552fca`: Cover, Combat Movement, Fire spray, Follow,
Melee, Investigate, Reload, Weapon throw, the Brute set and others. The exceptions are `0x005e62a0`,
`0x005e72d0` and `0x00afbd90`.

### 4.2 Mode handlers never "return" completion

A handler reports "my action is done" **only** by calling `0x004f4f50`, typically from its tick when
its sub-state machine reaches an end. Nothing is returned through the slot interface: the tick is
`void`, and the event handler's return code is about the *event*, not the action.

### 4.3 How the decision layer picks up the finish (the hand-off)

CONFIRMED by listing of `0x004e8d00` and `0x004f5210`. This is hand-off depth only; the decision step's
own goal-choice logic is a later unit.

- **The finish suppresses the mode tick.** The AI update skips the tick while `+0x44c` bit 0 is set
  (§5). Between "finished" and the next accepted request, the human is frozen in its old mode and gets
  no ticks.
- **The decision step sees the finish.**
  1. The current action is now `0x47`, so the per-action re-decision wait is bypassed. The `+0x438`
     timer is restarted and the chooser `0x004f3750` runs. **[A1, §9.1: corrected — the gate runs
     first; this path restarts `+0x43c`, and `+0x438` is restarted only on a commit.]**
  2. If the chooser returns an action (≠ `0x47`), and it differs from the current one *or* bit 0 is
     set, the step does the following:
     - clears bit 0;
     - ~~clears the previous action~~ calls `0x004f4eb0`, which has no observable effect (A1, §9.10
       item 3);
     - **requests** the action's category mode through the setter;
     - commits in place (reason `+0x444`, action `+0x445`, timer `+0x438`, block → `+0x450`), but
       **only if** the request is pending afterwards with exactly that mode. A request refused by the
       acceptance test leaves the old action fields untouched.
  3. On a gate refusal (`0x004f5180`), bit 0 is cleared without a new choice. **That resumes ticking
     the old mode.**
- **Same-frame entry.** The apply step `0x004e8310` runs right after the decision step in the same
  update (§1.4 step 5.5). The new mode is entered (exit → install → enter), and its tick runs **in the
  same frame**, because apply cleared the pending bit and the transition cleared `+0x44c` bit 0.
- **One action re-chosen into the same mode.** The setter is still called and apply still runs the
  transition, so **exit and enter fire even when old mode = new mode** (for generic modes, the
  transition is unconditional). Mode code must therefore tolerate re-entry. `Combat Movement`'s exit
  checks `new_mode` ≠ 5 for exactly this reason (§2.1).

**This settles `spec-lua-api-behaviour.md` §30.4's open question.** Goal-driven decisions (this §4.3
path) commit in place through the setter/apply pair above and never touch the scripted-request pool.
Only the ordered-action seam below (§4.4) — externally ordered actions from Lua, mission scripts, or
vehicle code — uses that pool.

### 4.4 The ordered-action seam (kind 3) at the hand-off point

CONFIRMED by listing of `0x004f4d00`, `0x004f5360` and `0x004f4c90`. The request pool itself
(`0x006f62b0`/`0x004f4d00`/`0x004f4b90`) is specified in `spec-lua-api-behaviour.md` and was not
re-read here.

- **Commit `0x004f4d00(human, action, no_id)`.**
  1. It writes the action id into the staged block `+0x4a8`.
  2. Unless `no_id` is set, it allocates a kind-3 id (`0x006f62b0(human, 3, 0)`) into `+0x4aa` and
     sets `+0x4fe` bit 6.
  3. If the human is not locally owned, it forwards to the owner (net sub-type 0) and stops.
  4. Otherwise it evaluates the precondition flags (`0x004f3560`/`0x004f25c0`). **A failure does not
     stop the commit:** it only triggers a call to `0x0096f4f0`, whose result is discarded (CONFIRMED
     in the listing: no branch around the commit).
  5. It copies the staged block to `+0x450` and sets `+0x44c` to "bits 0, 2, 4 cleared, bit 3 set"
     (**ordered**). It restarts the timers at `+0x440` and `+0x438`, and sets reason `+0x444` = `0x12`
     and the current action `+0x445`.
  6. **It then requests the action's category mode**, ignoring whether the request is accepted.

  From there the path is identical to a goal-driven choice: the same apply step and the same
  transition. Two differences from the decision step:
  - the ordered action becomes "current" **before** the mode is entered;
  - it stays current **even if the mode request is refused**. The decision step commits only when its
    request is pending with the right mode.

  HIGH consequence: if a refused order leaves the old mode ticking, the finish routine's "category
  mode = current mode" test (§4.1) cannot pass. The order's kind-3 id then stays open until it is
  cancelled or re-decided.
- **Try-start `0x004f5360(human, action)`.**
  - If not locally owned, it forwards (net sub-type 2) and returns 1.
  - Otherwise it returns 0 unless the timer `0x00d9e1e0` check passes, the precondition flags pass,
    and the action's own routine (action table row `+0x08`) accepts a fresh block initialised by
    `0x004efea0`.
  - On success it stages that block and commits through `0x004f4d00(…, 0)`, which allocates an id.
- **Completion.** The finish routine (§4.1 step 3) completes the kind-3 id only when the finished
  action is still the ordered one *and* the human is still in its mode. HIGH consequence: an order
  whose mode was replaced by a reactive mode (say `Flee` via a damage request) is **not** completed
  by that mode's later finish. Its id stays open until something cancels it. **[A1, §9.10 item 5: and
  the chooser re-issues the staged order at each later decision until it completes or is cancelled.]**
- **Cancel `0x004f4c90(human)`.**
  - If not locally owned, it forwards (net sub-type 5).
  - Otherwise it clears `+0x44c` bits 2, 3 and 4. It sets bit 0 (finished) exactly when the staged
    order is the current action, so the update stops ticking it and the decision step re-chooses.
  - It cancels the id (`0x006f6680`), clears `+0x4a6` bit 7 and zeroes `+0x4aa`.

---

## 5. How slot `+0x0c` is reached

CONFIRMED by listing of `0x004e8d00` at `0x004e9055`–`0x004e90c0`.

- **Mechanism: a direct static-table index, not a vtable.**
  1. `mode = byte [human+0x501]`.
  2. `fn = dword [0x012e1544 + mode·0x24]`. The code computes `mode·9` and then scales by 4, so this
     is the row's `+0x0c` slot (`0x012e1538 + 0x0c`).
  3. A null slot means "no tick". No null tick exists among the 44 rows.
- **Conditions.** The tick is called only if all four hold:
  - the readiness guard of §1.4 step 5.6 passed;
  - `mode` ≠ `0x2d`;
  - no switch is pending (`+0x317` bit 3 clear);
  - the action is not finished (`+0x44c` bit 0 clear).

  A human whose request was dropped at apply time has the pending bit cleared, so it ticks normally in
  its old mode.
- **Call.** `fn(human)`, cdecl, **one argument: the human pointer**. There is no time-step or context
  argument. Handlers read frame time and timers themselves (the engine's timer helpers `0x00d9e140`,
  `0x00d9e210`, `0x00d9e250`, `0x00d9e1e0` operate on per-object timer fields).
- **A dead call before it.**
  1. The update calls `0x004f3a90(human)`. That returns true when the current action's category mode
     equals the current mode, or equals the pending requested mode.
  2. If that is true and `+0x445` equals the committed block's first byte `+0x450`, it calls
     `0x004f28f0(action)`. That is a pure lookup of the action's category byte (`0x2d` for ids ≥ 70),
     and the result is **discarded**.

  This changes nothing. HIGH: a compiled-in remnant of a stripped consistency assertion. A port can
  omit it.

---

## 6. The full 44-row mode table

CONFIRMED (static read of `0x012e1538`, 396 dwords, checked by script against the dump with 0
mismatches). Slot headings use the roles of §2.1.

- **Stub markers:** `nop` = `0x00754410`; `ret1` / `ret3` / `ret4` = the constant event-handler stubs
  `0x00682c50` / `0x006147f0` / `0x0066ac20`; `—` = null.
- **Act. net** = the shared committed-action network pair `0x004f3eb0` / `0x004f5440` (§2.1).
- **CAE** = reachable from the decision layer via action category bytes.
- **Apply** = the group the apply step puts the mode in (§3.2): **G** generic, **B** begin function,
  **I** ignored.
- Two names are shorter than four characters, so the dump tool printed only their pointers: row 1
  (`0x0111646c`) and row 11 (`0x011164d8`). They are `Aim` and `Dog` per the scoping note's string read.

| # | Name | `+04` enter | `+08` exit | `+0c` tick | `+10` event | `+14` init params | `+18` net write | `+1c` net read | `+20` remote request | CAE | Apply |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 0 | Idle | `0x00512300` | `0x005100d0` | `0x00513270` | `0x00512680` | `0x0050f520` | `0x00510790` | `0x005143e0` | `0x00514310` | yes | B |
| 1 | Aim | `0x004d2c60` | nop | `0x004d2ca0` | ret1 | `0x004d2c50` | `0x004d2db0` | `0x004d2df0` | `0x004d3090` | — | I |
| 2 | Brute Stomp | `0x00524780` | `0x005247a0` | `0x00529ca0` | `0x005247d0` | — | — | — | — | — | I |
| 3 | Brute Melee | `0x005296c0` | `0x00525c70` | `0x005297e0` | `0x005246d0` | — | — | — | — | yes | G |
| 4 | Bust | `0x004d3f20` | `0x004d3f90` | `0x004d41e0` | `0x004d3fb0` | nop | nop | `0x004d3150` | `0x004d4580` | — | I |
| 5 | Combat Movement | `0x0053f970` | `0x005383d0` | `0x0053fe30` | `0x00538d90` | — | Act. net `0x004f3eb0` | Act. net `0x004f5440` | — | yes | G |
| 6 | Commando | `0x0052cf80` | `0x0052c840` | `0x0052d330` | `0x0052c880` | nop | `0x004f3eb0` | `0x004f5440` | — | yes | G |
| 7 | Cover popout | `0x00541c90` | `0x005400b0` | `0x005412e0` | `0x00540100` | — | `0x004f3eb0` | `0x004f5440` | — | yes | G |
| 8 | Cover stay | `0x00543480` | `0x00541150` | `0x005434f0` | `0x005405f0` | — | `0x004f3eb0` | `0x004f5440` | — | yes | G |
| 9 | Cower | `0x004d5350` | `0x004d4b20` | `0x004d55b0` | `0x004d57c0` | `0x004d4990` | `0x00523ca0` | `0x004d4fe0` | `0x004d5de0` | — | B |
| 10 | Dbno | `0x0050c2b0` | nop | `0x0050c130` | `0x0050c210` | nop | nop | nop | — | — | B |
| 11 | Dog | `0x0052e680` | nop | `0x0052eca0` | `0x0052e6c0` | `0x0052e640` | nop | `0x0052e650` | — | — | I |
| 12 | Fire spray | `0x00544c40` | `0x005435f0` | `0x00544cc0` | `0x00543680` | nop | `0x004f3eb0` | `0x004f5440` | — | yes | G |
| 13 | Flee | `0x004da7c0` | `0x004d5f70` | `0x004da9f0` | `0x004dac10` | `0x004d6980` | `0x004d7a40` | `0x004d7d20` | `0x004dafd0` | — | B |
| 14 | Follow | `0x0054ba70` | `0x00545490` | `0x0054c100` | `0x005481e0` | — | `0x004f3eb0` | `0x004f5440` | — | yes | G |
| 15 | Grenade throw | `0x0054c7d0` | `0x0054c410` | `0x0054ff40` | `0x0054c510` | nop | `0x004f3eb0` | `0x004f5440` | — | yes | G |
| 16 | Gunfire evade | `0x004db120` | nop | `0x004db480` | `0x004db700` | `0x004db0f0` | `0x004db380` | `0x004db3e0` | `0x004db940` | — | I |
| 17 | Gunfire wary | `0x004dbc30` | `0x00520cd0` | `0x004dbf60` | `0x004dc0b0` | `0x004dbc00` | `0x004dbcb0` | `0x004dbcf0` | `0x004dc260` | — | B |
| 18 | Hijack | `0x0051d740` | `0x0051d2a0` | `0x0051d890` | `0x0051cbb0` | — | `0x004f3eb0` | `0x004f5440` | — | yes | G |
| 19 | Holdup | `0x004dc530` | `0x004dc380` | `0x004dc610` | `0x004dca40` | `0x004dc4c0` | `0x00523ca0` | `0x004dc4e0` | `0x004dcbc0` | — | I |
| 20 | Hostage | `0x004dcf70` | `0x004dccb0` | `0x004dd050` | ret1 | nop | nop | nop | `0x004dd1f0` | — | I |
| 21 | Human shield | `0x0050ed30` | `0x0050d2f0` | `0x0050ed90` | `0x0050ee50` | nop | nop | nop | — | yes | B |
| 22 | Investigate | `0x00551920` | `0x00550440` | `0x005519e0` | `0x00551150` | — | `0x004f3eb0` | `0x004f5440` | — | yes | G |
| 23 | Melee | `0x00552070` | `0x00551cc0` | `0x00551cf0` | `0x00551d40` | — | — | — | — | yes | G |
| 24 | Panic | `0x004eee90` | `0x004ede60` | `0x004ef090` | `0x004eedb0` | `0x004ee590` | `0x004eecd0` | `0x004efa00` | `0x004efc10` | — | B |
| 25 | Passenger | `0x0051eef0` | `0x0051dd60` | `0x0051f040` | `0x0051f5c0` | `0x0051dd50` | `0x0051e060` | `0x0051fa40` | `0x0051fab0` | yes | B |
| 26 | Pickup item | `0x004f0f40` | `0x004eff40` | `0x004f04c0` | `0x004f0700` | — | nop | `0x004f1190` | `0x004f1230` | yes | G |
| 27 | Reload | `0x00552790` | `0x005527d0` | `0x00552850` | `0x00552820` | nop | `0x004f3eb0` | `0x004f5440` | — | yes | G |
| 28 | Return to shore | `0x0052f700` | `0x0052f720` | `0x0052f990` | `0x0052f740` | `0x0052f6e0` | `0x0052f890` | `0x0052f920` | — | — | I |
| 29 | Riot cop | `0x00530040` | `0x0052fb80` | nop | `0x00530110` | `0x0052fb50` | `0x0052fcc0` | `0x0052fd10` | — | — | I |
| 30 | RollerBlader | `0x00530fa0` | `0x00531130` | `0x00532180` | `0x00531220` | `0x005304c0` | nop | `0x005304e0` | — | yes | B |
| 31 | Skydiving | `0x005342c0` | `0x005355e0` | `0x00535a20` | `0x005344f0` | nop | nop | `0x00533860` | — | — | G |
| 32 | Squirted | `0x004f2270` | nop | `0x004f23f0` | ret3 | `0x004f2290` | `0x004f22b0` | `0x004f22f0` | — | — | I |
| 33 | Streaking | `0x00536c70` | `0x00536330` | `0x00537510` | ret1 | `0x00536c00` | `0x00536ca0` | `0x00537020` | `0x00537560` | — | I |
| 34 | Stunned | `0x0050bd20` | `0x0050c300` | `0x0050c570` | `0x0050be10` | — | `0x004f3eb0` | `0x004f5440` | — | — | B |
| 35 | Taunt | `0x00552c30` | `0x00552bc0` | `0x00552d70` | `0x00552bf0` | — | `0x004f3eb0` | `0x004f5440` | — | yes | G |
| 36 | Weapon throw | `0x00553030` | `0x00552f70` | `0x00552fa0` | ret4 | — | `0x004f3eb0` | `0x004f5440` | — | yes | G |
| 37 | Veh clear door | `0x0051fd30` | nop | `0x00520730` | ret1 | `0x0051fce0` | `0x0051fd10` | `0x00520570` | — | — | I |
| 38 | Veh enter | `0x00521320` | `0x00520cd0` | `0x005213d0` | `0x005215a0` | `0x00520ca0` | `0x00521060` | `0x005210d0` | `0x00521f40` | — | I |
| 39 | Veh evade | `0x00523650` | `0x00522010` | `0x005236d0` | `0x00522d70` | `0x005224d0` | `0x00522b30` | `0x00523120` | `0x005239c0` | — | B |
| 40 | Veh threat | `0x00523c10` | `0x00523c50` | `0x005241a0` | `0x00524130` | `0x00523ba0` | `0x00523ca0` | `0x00523bc0` | `0x00524390` | — | B (+`Dbno` fall-through) |
| 41 | Veh trans | `0x00524420` | nop | `0x00524460` | `0x00524520` | nop | nop | `0x00524420` (same as enter) | — | — | G |
| 42 | Zombie eat | `0x00537c20` | `0x005375b0` | `0x00537de0` | `0x00537f60` | — | `0x004f3eb0` | `0x004f5440` | — | yes | G |
| 43 | Player | `0x004f1350` | nop | `0x004f1440` | ret1 | nop | nop | nop | — | — | B |

**Fill counts** (CONFIRMED; nop counted as filled):

| Slot | Non-null | of which nop | of which constant stubs | Real functions |
|---|---|---|---|---|
| `+04` | 44 | 0 | 0 | 44 |
| `+08` | 44 | 8 | 0 | 36 |
| `+0c` | 44 | 1 (Riot cop) | 0 | 43 |
| `+10` | 44 | 0 | 7 (5 ret1, 1 ret3, 1 ret4) | 37 |
| `+14` | 30 | 11 | 0 | 19 |
| `+18` | 41 | 10 | 0 | 31 |
| `+1c` | 41 | 4 | 0 | 37 |
| `+20` | 16 | 0 | 0 | 16 |

There are 233 distinct real slot functions, excluding the nop, the three constant stubs and nulls.
The scoping note's count of 237 probably counted the stubs.

**Pattern.**
- **14 rows carry the act-net pair:** 5, 6, 7, 8, 12, 14, 15, 18, 22, 27, 34, 35, 36, 42. These are 13
  CAE modes plus `Stunned`. Their own state is the committed action, so they need no parameter-block
  initialiser (`+14` null or nop) and no remote-request handler (`+20` null). `Stunned`'s presence fits:
  it is the most commonly forced reactive mode and keeps the current action fields.
- **The other 7 CAE modes** (`Idle`, `Brute Melee`, `Human shield`, `Melee`, `Passenger`,
  `Pickup item`, `RollerBlader`) replicate differently:
  - `Brute Melee` and `Melee` have no network slots at all;
  - `Human shield` has only nops;
  - the other four have their own.
- **The 16 rows with a `+20` handler** are the reactive and ambient modes that one peer can order on
  another's character. `Pickup item` is the only CAE mode among them besides `Idle` and `Passenger`.

The `+08` real-function count (36) agrees with the scoping note's "36 filled", which counted nop as
empty.

---

## 7. Cross-observations

1. **Three per-human mode bytes plus two flags.**
   - `+0x501`: current mode.
   - `+0x502`: previous mode.
   - `+0x503`: just-entered flag.
   - `+0x504`: requested mode.
   - `+0x500`: sub-state.
   - `+0x317`: bit 3 "switch pending", bit 4 "switch in progress".
   - `+0x8d0`…`+0x95f`: the `0x90`-byte live mode parameter block.

   None of `+0x500`…`+0x504` is reset by exit. The just-entered flag `+0x503` was seen only being set
   (to 1) by the transition; who clears it is OPEN (§8).
2. **No transition graph.** The engine has no table of allowed mode-to-mode transitions. Legality is
   the acceptance test (state-based) plus each begin function's preconditions. A port that models a
   transition graph would be stricter than the binary.
3. **Two write paths to the current mode, by design.**
   - An owned human changes mode only through `0x004dd380` (exit → install → enter).
   - A proxy changes mode only through the network read (install without exit or enter).

   Requests made on a proxy are forwarded to the owner as message `0x1b` and come back as replicated
   state.
4. **The throttle is coarse and frame-order dependent.** Which eligible humans skip on a busy frame
   depends on their order in the `+0x1fc` list. The latch is set by the first eligible human to see
   count > budget/2, and the count includes every human that thought earlier in that frame, eligible or
   not. A deterministic port must keep the list order.
5. **The event pump plus slot `+0x10` is a second input channel** alongside the decision layer, and
   probably the main one for the 24 non-CAE modes. **[A2, §10.5: the producer is `0x004f5dd0`, staging
   into `+0x1c50`; `0x004f5ae0` moves records to `+0x1c54` once per frame; perception posts 11 kinds.]**
   Its producer side (who appends records to
   `+0x1c54`) is OPEN; it is the natural interface between perception and the reactive modes.
6. **The Brute modes are throttle-exempt**, as are ~~vehicles~~ humans whose held item passes
   `0x00536300` **[A1, §9.10 item 1]**.
7. **The decision gate's forced `Skydiving` goes through the generic apply path** (31 is generic), so
   it behaves like any CAE mode request even though no action carries category 31.

---

## 8. OPEN

1. **Who clears `+0x503` (just-entered)?** HYPOTHESIS: the enter slot or the first tick; a structure-offset
   search was not run.
2. **Who appends event records to `+0x1c54`?** The record layout seen here: link `+0x00`, kind `+0x08`,
   timer `+0x24`, mode and sub-state filters `+0x28`/`+0x29`, flags `+0x2a`; the `Panic` handler also
   reads `+0x10`/`+0x14` (a handle), `+0x30`, `+0x34`. The kind values seen are 12, 46 and 81 (in
   `Panic`'s handler). **[A1: kind 108 is read by the `suppress` goal test, §9.6; see item 15.]** Their names and the producers are for a later unit. **[A2, §10.5 — partly resolved: the generic
   producer is `0x004f5dd0(kind, source, recipient, delay, mode filter, sub-state filter, payload)`, which
   appends to the staging list `+0x1c50`; `0x004f5ae0` moves staged records to `+0x1c54` once per frame and
   sets `+0x2a` bit 0. Record `+0x18`/`+0x1c` is the recipient handle, `+0x20` a creation timer, `+0x30`… an
   optional `0x20`-byte payload. Perception posts kinds 0, 2, 11, 12, 18, 71, 76, 78, 86, 87 and 100;
   kinds 46, 81 and 108 come from elsewhere and their producers remain OPEN.]** **[A3, §11.7 item 3:
   kinds 5 and 97 are consumed by `follow avoid LOF` and `follow browse`; producers OPEN.]** **[A4, §12.3:
   two further kinds found, 56 and 300, posted by the "fire in the hole" producer `0x00501d80`; kinds
   46, 81 and 108 remain OPEN.]** **[2026-10-09, §20.2: kind 81's producer is now CONFIRMED — the
   wander-destination-commit step (`Idle` sub-state 33→34 transition), posted via `0x004f5dd0`, sub-state
   filter 34, delay randomized 15-35s. §20.4 separately found a parallel family of event kinds (14, 38,
   48) also feeding `Flee`/`Cower` directly from the same `Idle`-private sink `0x005113e0` — none of the
   three is in this item's own catalogued perception-posted list, and their producers remain OPEN too,
   alongside kind 67 (a 250-750ms delayed gunfire re-check, §20.4).]**
3. **The default event handler `0x004ebc70`** (a large body) was dumped but not read beyond its setter
   calls. It is the reactive-mode dispatcher for every mode whose `+0x10` returns 0.
4. **How `Aim`, `Gunfire evade` and `Veh enter` are entered.** None has a direct caller of the transition
   primitive. Candidates: the computed-mode call in `0x00a34c30` (only seen entering `Idle`), the
   remote-request path, or the `Hijack` mode's own handlers. Also: why `0x00afd4c0` requests `Veh enter`
   (38) when apply ignores 38.
5. **The `Veh threat` → `Dbno` fall-through** (§3.2): confirm that `0x009715d0` is false for a
   vehicle-threat human in practice, or whether the binary really double-transitions.
6. **Identity of the budget source `0x02442954`** (HIGH: count of live AI humans), and of the
   eligibility and exemption bits (`+0x1da6` bits `0x10`/`0x20`): only `0x00af4150` was seen setting
   `0x20`.
7. ~~**The second human list** (`+0x1f0`, via `0x009ea630`/`0x009e91c0`) runs **before** the frame's
   budget reset. Confirm whether it holds players only; if it holds any AI human, those humans count
   against the previous frame's budget state.~~ **[Resolved 2026-10-03, `review/scoping-player.md` §1.5 +
   re-derivation here]:** it holds players only. `0x009ea630` and the co-op lookup `0x009df3d0`
   (`spec-lua-api-behaviour.md` §14.31) read the identical global/offset pair — object-manager singleton
   `0x03171a64`, array `+0x1f0`, count `+0x1f8` — so both walk the same list. `0x009df3d0` resolves each
   entry and returns the first one that is **not** the local-player singleton (`0x0262edfc`), with no
   class/type check anywhere in its body; that is only a correct "find the co-op partner" query if every
   entry already is a player object. CONFIRMED by disassembly of both functions this pass. So the second
   list does not hold AI humans, and does not count against the AI budget.
8. **Double update.** Can a human be on both manager lists, or be reached through `0x00952760` →
   `0x00af4150` in the same frame as its normal update? Not checked.
9. The "may think" gate's individual tests (`0x009a1dc0`, `0x00985d80` states 4/5/6/8, `0x0095d740`,
   object flags `0x2be`/`0x408`) are unidentified.
10. ~~The second word of each behaviour-record per-action triple (`+0x24 + 6·a`) is used here as the
    roll's upper bound. Cross-check against `spec-ai-behavior-format.md`'s field names when the next
    unit touches the decision layer.~~ **[Resolved by A1, §9.7]:** the triple is `Repeat_min`,
    `Repeat_max` and the enabled byte of the `ai_behavior.xtbl` ability entry
    (`spec-tables-traffic-ai.md` §10), so the roll is a random value between the two repeat bounds.
11. ~~**The 70 action routines** (action-table `+0x08`, addresses in §9.5) were not read. Each is the
    last feasibility test and the builder of the committed block, so what a chosen action targets and
    where it moves is decided there. This includes which behaviour-record fields they consume
    (`seperated_distance`, `abandon_cover_time`, `find_new_cover_time`, `stay_down_time`,
    `Suppress/length`, `squad_cover_pct`, `dodge_chance`, `combat_aggression` are not read by the
    decision layer itself, §9.7). Whether shipped `ai_goals.xtbl` lists ever name the two
    null-routine actions (24, 49; §9.5) should be checked against the real file.~~
    **[Resolved by A3, §11]:**
    - all 65 distinct routine bodies were read and sorted into families: §11.3;
    - the committed-block fields they write: §11.2;
    - which goals reach which family in the shipped data: §11.4;
    - behaviour-record readers: §11.5. The routines read `seperated_distance` (17, 46),
      `advance_cover_time` (10), `pop_up_time` (8) and `dodge_chance` (26).
      - `abandon_cover_time`, `find_new_cover_time`, `stay_down_time` and `Suppress/length` are read
        in mode code.
      - `squad_cover_pct` is read nowhere.
      - The record's `combat_aggression` is read only by the behaviour-assignment scorer `0x004f3470`;
    - the null routines: no shipped goal list or behaviour names 24 or 49, and 49 has no
      `combat_actions.xtbl` row, so the hazard is latent in the shipped data (§11.1);
    - 58 `avatar stomp` has a return-false stub as its routine and is never chosen by a goal (§11.3 A).

    Still open inside A3: the meaning of the `+0xccd`/`+0xcce` pose numbers, the run-time radii
    `0x024fb1c0`/`0x024fb1d0`/`0x013bad60`, and the decoding of the grenade solver `0x0054d930`.
12. ~~**Perception's write side (unit A2).** Who fills perception lists 0, 1, 2, 3, 6, 7, 13–16; the
    meaning of entry flags `0x1`, `0x8`, `0x10`, `0x40`, `0x100`, `0x400`, `0x800`, `0x02000000`,
    `0x04000000`; entry kinds (33 in list 1, `0xff`); the accumulator period `0x004f8cd0`; timer
    `+0x244` and handle `+0x250`; the query `0x004fcce0` (and whether it uses the latched
    `Investigate/seen_in_last` of §9.6 as a maximum age). The read-side layout is in §9.8.~~
    **[Resolved by A2, §10]:**
    - producers for all 22 lists: §10.3;
    - list meanings: §10.3 — 0 known hostiles, 1 last sightings, 2 line of fire, 3 shots at me,
      6 hearsay, 7 bodies, 13 witnessed attacks, 14 gunfire heard, 15 loud events, 16 damage memory;
    - flag meanings: §10.4;
    - kinds: the entity's own type byte, and no live path writes `0xff` (§10.1);
    - `0x004f8cd0`: the list lifetime, a linear 30 s fade for list 16 (§10.1–§10.3);
    - `+0x244`: time since the last clear line of fire to the target; `+0x250`: an ally link (§10.6);
    - `0x004fcce0`: uses `Investigate/seen_in_last` as a maximum age — CONFIRMED (§10.7).

    ~~Still open inside A2: who sets list-0 flags `0x2` and `0x4` (no setter in the perception range),
    the tunables block `0x013b7fd8`…`0x013b8450`, the team-relationship table `0x009b7100`, and lists
    12, 17–21 in detail.~~ **[Resolved by A4, §12]:** the tunables block is the `tweak_table.xtbl`
    registry, real names and values given (§12.1); the team-relationship table's write side is already
    specified in `spec-lua-api-behaviour.md` §20.6/§30.6, and its read accessor is a 12-instruction
    lookup with an id-9 guard (§12.7); list 12 is decoded — a `Pain_sound_radius`-gated distress notice
    (§12.3). Still open: who sets list-0 flags `0x2`/`0x4`, and lists 17–21 in detail (cover-memory
    lists, outside this unit's sensing-primitive scope).
13. **Unidentified helpers the decision layer calls:**
    - in the gate: ~~`0x009aa360` and the class bit tested by `0x00971cf0`~~ **[A3, §11.6: "held in
      a grab" (HIGH), and the player class, is-a bit `0x21`]**;
    - in the step: the weapon upkeep `0x00600720` (still OPEN; no action routine calls it);
    - in the goal tests: ~~`0x00540a70` (cover and suppress)~~, `0x004dd640` (the `+0xccd`/`+0xcce`
      states), ~~`0x004fe390`, `0x004df250`, `0x004e68e0`, `0x0050cdb0`, `0x004fdac0`,
      `0x0097e280`, the rest of `0x009c05b0`, and the vehicle-geometry tail of `advance`~~.
      **[A3, §11.6: every struck item is resolved there.]** `0x004dd640` is HIGH: the cover posture
      family. Its pose numbers stay OPEN;
    - also: the flag `+0x76f` bit 3, ~~the owners of timers `+0x968`/`+0x96c`~~ **[A3, §11.6:
      `0x00541600` arms the find-new-cover `+0x964`, stay-down `+0x968` and time-in-cover `+0x96c`
      timers]**, and the writer of the `kill near player` radius `0x0135bfc4` (only a PUSH of its
      address in non-function code at `0x00fec830` was found). The `+0x76f` bit and `0x0135bfc4` stay
      OPEN: no action routine touches them.
14. **Initial arming of the 70 repeat timers `+0x320 + 4·a`.** An unarmed (negative) timer blocks
    its action in the chooser (§9.7); the spawn-time initialiser was not traced. **[A3, §11.6: the
    per-action setter is `0x004f4ae0(human, action, ms)`; its spawn-time caller is still not
    traced.]**
15. **Event kind 108** (`0x6c`), which the `suppress` goal test looks for in the event lists
    `+0x1c54`/`+0x1c50`, joins kinds 12, 46 and 81 of item 2; its producer is unknown. **[A2, §10.5: not
    posted from the perception range; kind 12 is perception's "attacked" event.]**
16. **Weapon-info fields.** `+0x124`/`+0x128` (HYPOTHESIS: minimum and maximum engagement
    range), `+0x1c` (compared with 3 in `0x00b81850` and 12 in `0x0052a2e0`; the riot-shield
    reading of `0x009BA300`, §9.10 item 7, depends on it) and flag `+0x10` bit `0x80000` (throttle
    exemption) are not identified. **[A3, §11.6: the band test `0x004df080` uses `+0x124`/`+0x128` as
    the inner and outer edges of the engagement band, which raises that reading to HIGH.]**
17. **Indeterminate reason byte** (§9.10 item 4): confirm on a live run what `+0x444` holds after an
    `ignore_ai` or re-asserted-order commit; a port should not depend on it.
18. **The FOV cone/frustum geometry itself** (§12.2 step 4): `0x004fdf70`, `0x004fdc90`, `0x0075d7e0`
    and `0x00525d30` were traced only as far as their call shape, not read instruction-by-instruction.
    The actual inside/outside arithmetic (dot products, the min/max blend of two scaled bounding
    offsets) is OPEN.
19. ~~**The underlying LOS raycast execution** (§12.4): `0x004ecfc0`, `0x004ecf40` and `0x004ed320` were
    not traced past the request-queue/answer-decode layer. This is a physics-domain primitive and may
    belong with `spec-physics-runtime.md` rather than here.~~ **CLOSED 2026-10-08.** Those three are
    confirmed pure cache/queue code (no raycast inside them at all); the real resolver is a sibling,
    `0x004eda70`, which calls `spec-physics-runtime.md` §8.8's shared ray wrapper directly — full
    chain in §12.4's own updated text above and `spec-physics-runtime.md` §8.11.
20. **Three unnamed is-a/class bits and one state pair used by the FOV test** (§12.2): the
    "carried/witnessed" bit (descriptor `+0x6` bit 6), the gating bit tested before any narrowing runs
    at all (`+0xa` bit 0, via `0x00853b30(target, 0x20)`), and perception's own sub-state values 2 and 6
    (`+0x292`) that, together with a target/own-focus handle match, trigger the full bypass. None was
    identified beyond its bit position or numeric value.
21. **The ally-link cylinder radii's full derivation** (§12.8): `0x013ba290`/`0x013ba2b0` are themselves
    built at start-up from further zero-fill values (`0x013ba260`/`0x013ba270`/`0x013ba280`/`0x013ba2a0`),
    traced one level, not to a final metres value.
22. **The exact field mapping of the two new event kinds** (§12.3, kinds 56 and 300 posted by
    `0x00501d80`): the push-order-to-parameter mapping onto `0x004f5dd0`'s seven-field signature was
    inferred from the dword count, not decompiler-verified; and their consumers (which mode/handler
    reacts to kind 56 or 300) are unknown, joining item 2's list.
23. **`Player_friendly_fire_threshold`** (§12.1): shares its registration pattern and memory
    neighbourhood with the `Ai_fov_*` block but belongs to a different subsystem (`AI:Follower`); no
    reader was found in the perception range. Its real consumer belongs to whichever future unit covers
    the player's AI-follower/companion logic, not this one.
24. **`0x0097e9b0`'s own argument shape**, called inside `Cower`'s begin (§13.1.1) while packing the
    final flags byte — the call site's stack-offset arithmetic does not resolve cleanly to a simple
    cdecl/thiscall signature by hand; only the result (one bit of a packed flags byte) is established,
    not its source (A5).
25. **`Veh evade`'s own `rep movsd` directly into `[human+0x8d0]`** immediately before calling the
    transition primitive with a block pointer that appears to alias the same bytes (§13.1.6) — whether
    this is harmless belt-and-suspenders double-writing or the manual copy does work the primitive's own
    copy would not otherwise reach is not confirmed (A5).
26. **The virtual-dispatch gate inside `Veh threat`'s own call** (`0x00523db0`, §13.1.7): a 4-link chain
    ending in two vtable calls (slots `0x68`/`0x70`), declining if the second result equals the human.
    HYPOTHESIS "don't flag vehicle-threat on whatever the camera/possession system currently focuses" is
    a guess from the chain's shape, not confirmed against any already-named concept; the vtable's owning
    class was not identified (worth checking against `scratchpad/rtti_classes.json` before a fresh RTTI
    walk) (A5).
27. **Whether `0x0054c630`'s second output buffer (`outB`) is genuinely dead** at the `throw weapon` call
    site or consumed by some other, unexamined caller of the shared trajectory-solver core (§13.2.1) — a
    raw call-site scan for `0x0054c630` across the whole binary would close this cheaply (A5).
28. **The sign/axis convention of `0x0054c630`'s vertical output component** (§13.2.1): increasing `arc`
    makes it more negative and increasing `speed` makes it less negative — backwards from a naive
    "more arc ⇒ aim higher" intuition. Two explanations remain open: a gravity-down-positive local frame
    re-flipped by some caller, or `arc` is not a gravity constant at all but a lob-fraction/undershoot
    term with the real upward arc living in the (possibly-unused, per item 27) `outB` branch (A5).
29. **`0x0054d930`'s own body past its first ~280 instructions** (§13.2.2) — the post-solve geometry pass
    and the full field layout of the large working struct it receives by `ECX` beyond the offsets already
    confirmed. Deliberately not given a second synthetic Unicorn harness, since its trajectory math is
    confirmed-by-listing to delegate to the already-oracle-tested `0x0054c630` (A5).
30. **`0x00853b30(thrower, 0x22)` inside `0x0054c770`** (§13.2.1) — structurally identical to the
    confirmed "is a player" test at bit `0x21` used throughout this file, but at a different bit index;
    not claimed to mean the same thing, not found named elsewhere in this spec (A5).
31. **`0x004e8070`'s dead-looking double call to `0x008addb0(human, 0)`** (§13.1.3 step 1) — confirmed
    byte-identical with no intervening side effect on the one call path this unit needed (apply's
    `Gunfire wary` dispatch, `flag=0`); most likely a compiler-folded shared tail from two originally
    separate source conditions, live only on some other caller's path. Does not change observable
    behaviour on the path this unit traced (A5).

---

## 9. The decision step (unit A1)

**Scope.** This section opens what §1–§8 treated as a black box: the decision step `0x004f5210`
(§1.4 step 5.4), its gate `0x004f5180`, the goal/action chooser `0x004f3750`, the 17 goal-test
routines it calls, and the action table it reads. It stops at each action's own routine (the
feasibility test that also builds the action block) and at perception's write side (unit A2).
**Method.** As for A0: a private disposable Ghidra project copy (deleted on completion), `func`/`range`/
`ptrs`/`xref`/`calls` dumps, plus a private whole-program instruction-text scan used to find every writer
of a few flag bytes. Every function cited as CONFIRMED below was read in full in its own listing. Field
names of the behaviour record and of the action table are the `.xtbl` element names already established
on the data side (`spec-tables-traffic-ai.md` §10, `spec-tables-weapons-combat.md` §11.1); they are
cited, not re-derived. Goal and action names are the engine's own strings (goal table `0x012e22f0`,
action table `0x012e1c60`), quoted as data.

### 9.1 Where the step sits, and what triggers a choice

CONFIRMED by listing of `0x004f5210`, `0x004f5180` and `0x004f28a0`.

- **Order inside the step:** gate (§9.3) → weapon upkeep → re-decision wait → chooser (§9.4) → commit.
  The gate runs **first**, before the chooser is ever called, so a gate refusal costs no chooser work.
  **[corrects §4.3]:** §4.3 describes the gate refusal as an alternative outcome after the finish; it is
  in fact the first test of every call.
- **The step only chooses on request.** Apart from the refusal cases of §9.3, the gate returns true only
  when `+0x44c` bit 0 (**action finished**, set by the finish routine §4.1) or `+0x44c` bit 1 is set. So
  a human whose action is running and unfinished is not re-evaluated at all unless bit 1 is set.
- **Bit 1 is the "re-evaluate" request, set by mode code.** A whole-program scan of the instruction text
  found two stores that can set it: the network read `0x004ea530` (copying the replicated byte) and the
  helper `0x004f28a0(human)`. No OR-immediate sets it. The request layer, the transition primitive and
  the decision step clear it (§3.1, §3.3, §9.2). A whole-byte store of a computed value elsewhere was not
  searched for, so this list is HIGH, not exhaustive. The helper sets bit 1
  to "the staged order's action id (`+0x4a8` first byte) differs from the current action `+0x445`" and
  clears it otherwise. A raw call scan finds **48** call sites, all in mode-slot code and its helpers
  (for example the `Idle` tick `0x00513270`, the `Passenger` tick `0x0051f040`, the `Pickup item` tick
  `0x004f04c0`, the `Human shield` event handler `0x0050ee50`, and many sites in the `Combat Movement`,
  `Cover` and `Follow` ranges `0x00538000`–`0x0054c0ff`). HIGH consequences:
  - when no order is staged, the staged id is the template's `0x47` (§4.1), so any goal-driven action
    makes the helper **set** bit 1. Each mode therefore decides, by where it calls the helper, at which
    points its action may be re-evaluated;
  - while an externally ordered action is current (staged id = current id) the helper **clears** bit 1,
    so an order is never re-evaluated by the goal layer; it ends only through the finish (§4.1) or a
    cancel (§4.4).
- **The no-interrupt wait.** When the current action `a0` is a real action (< 70) and has not finished,
  the step returns without choosing while the time since the last commit (timer `+0x438`, elapsed form
  `0x00d9e250`) is less than the action table's `No_interrupt_ms` for `a0` (row `+0x10`). Bit 1 is left
  set, so the request is honoured on the first frame after the wait ends. When `a0` is "none" (≥ 70) the
  step instead restarts the timer `+0x43c` with 0 and goes on to choose.
  **[corrects §4.3]:** §4.3 says the `+0x438` timer is restarted when the chooser runs after a finish. In
  the listing, `+0x438` is restarted **only on a commit**; the timer restarted on the "no current action"
  path is `+0x43c` (the same timer the finish routine restarts, §4.1 step 2).

### 9.2 The decision step `0x004f5210(human)`, in order

CONFIRMED by listing (95 instructions):

1. Remember the current action `a0` = `+0x445`.
2. **Gate** `0x004f5180` (§9.3). On false: clear `+0x44c` bit 0 and return. (A finished action that is
   refused here leaves the human in its old mode, which resumes ticking with current action `0x47`, as
   §4.3 already notes.)
3. **Weapon upkeep.** If the human's primary held item is absent (`0x00941f40` returns null; §9.10 item 1)
   and the `idle` goal test `0x004f4720` (§9.6) fails, call `0x00600720` on the inventory sub-object
   `+0x1b78` with the arguments (13, 1, 0, 0). HYPOTHESIS: "draw a weapon when alerted and empty-handed";
   `0x00600720` (892 instructions) was not read.
4. **Re-decision wait** (§9.1): if `a0` < 70, bit 0 is clear and the elapsed time on `+0x438` is below
   `No_interrupt_ms(a0)`, return. If `a0` ≥ 70, restart `+0x43c` with 0.
5. **Initialise a local action block** (`0x58` bytes) with `0x004ddb80`: zero-filled; three 3-float
   vectors at block `+0x18`, `+0x24`, `+0x30` set to the bit pattern `0x7fffffff` (the dword at
   `0x01115d60`, a NaN — HIGH: "position not set"); byte `+0x00` = `0x47`. (Two stores of -1 to block
   `+0x10`/`+0x14` precede the zero fill and are overwritten by it.)
6. **Chooser** `0x004f3750(human, &reason, &block)` (§9.4). Then clear `+0x44c` bit 1 unconditionally:
   one re-evaluation request is consumed per chooser call, whether or not anything changes.
7. **Commit**, only if the result `r` ≠ `0x47` and (`r` ≠ `a0` or bit 0 is set):
   1. clear `+0x44c` bit 0; write `r` into block byte `+0x00`;
   2. call `0x004f4eb0` (no observable effect, §9.10 item 3);
   3. request the action's category mode, read straight from action-table row `+0x04` (low byte), through
      the request layer `0x004e4130` (§3.1);
   4. only if a switch is still pending (`+0x317` bit 3) **and** the requested mode `+0x504` equals that
      category: reason → `+0x444`, `r` → `+0x445`, restart `+0x438` with 0, copy the block to `+0x450`.

   A refused request therefore leaves `+0x444`/`+0x445`/`+0x450` untouched (as §4.3 says), **but bit 0
   has already been cleared**: a finished action whose replacement is refused resumes its old mode's
   tick with current action `0x47`, exactly like a gate refusal.

### 9.3 The decision gate `0x004f5180`

CONFIRMED by listing (47 instructions; the human is passed in a register). It returns false — no decision
this frame — in each of these cases, tested in this order:

1. current mode `Stunned` (34) with sub-state 9 `idle` (the same stunned lock as the acceptance test,
   §3.4 step 1);
2. force flag `always_cower` set (`+0x2b9` bit 7; flag names in §9.9);
3. a mode switch is pending (`+0x317` bit 3) and the requested mode `+0x504` is `Stunned` (34) or
   `Panic` (24): the decision layer never overrides a pending forced reaction;
4. `0x0096f540`: `+0xcc8` is 5 or 6, or `+0xe3` bit 0 is set (HIGH: down or dead; the acceptance test
   treats `+0xcc8` 5/6 the same way, §3.4);
5. `0x009aa360(human, 0)` holds: a test on a linked-object handle at `+0x1898`, a second object found
   through `0x009aa130` whose `+0x1890` handle equals the human's own (`+0x08`), the bytes `+0x18d4` /
   `+0x18dc` / `+0x18dd`, and local ownership. HYPOTHESIS: "being held or carried by another object";
   not identified;
6. `0x00971cf0(human)` is non-null: the human's type descriptor (table `0x02cc9900` by type byte `+0x34`)
   has bit 1 of `+0xa`. HYPOTHESIS: the player-character class (compare "human-typed" = bit 2, §1.2);
7. `+0xcc8` = 14 and the current mode is not `Skydiving` (31): **request `Skydiving`** through the
   request layer, then return false. This is the gate's only mode request (§3.5, §7 item 7). With
   `+0xcc8` = 14 and the mode already `Skydiving`, the gate falls through to the final rule.
   HYPOTHESIS: `+0xcc8` = 14 is a free-fall state.

Otherwise it returns true exactly when `+0x44c` bit 0 or bit 1 is set (§9.1).

### 9.4 The chooser `0x004f3750(human, reason_out, block_out)` → action id

CONFIRMED by listing (`0x004f3750`–`0x004f39dd`, read in full). `reason_out` is one byte; `block_out` is
the `0x58`-byte block prepared in §9.2 step 5. In order:

1. **Clear a 70-entry "already tried" set** (one byte per action id, on the stack).
2. **Build the state mask** with `0x004f3560(human)` (CONFIRMED). Its five bits are, in order, the five
   precondition literals of the action table (`spec-tables-weapons-combat.md` §11.1):
   - bit 0 `on foot`: not in a vehicle (neither `0x009b9160`, `+0x16d4` = 3, nor `0x009b91e0`,
     `+0x16d4` = 1 or 2) **and** `+0xcc8` is 0 or 7;
   - bit 1 `in vehicle`: either vehicle test holds;
   - bit 2 `in water`: `+0xcc8` = 3;
   - bit 3 `must have target`: `0x004e4e50` returns a live target (§9.8);
   - bit 4 `can do in cover`: force flag `in_scripted_cover` (`+0x2bc` bit 0, §9.9).

   So a human whose `+0xcc8` is outside {0, 3, 7} and who is not in a vehicle has **no** medium bit set,
   and no action can pass the precondition test below.
3. **Ordered re-assert** — only when `+0x44c` bit 3 (an externally ordered action is staged, §4.4) is
   set **and** the timer `+0x440` has expired (`0x00d9e1e0`: armed and passed):
   1. copy the staged block `+0x4a8` (`0x58` bytes) into `block_out` and set block `+0x56` bit 6;
   2. if `+0x44c` bit 4 is set, also set block `+0x56` bit 7 and, when the staged action has a routine
      (action-table row `+0x08`), call it as `(human, block_out)`. If it returns false: reset `block_out`
      to the template `0x012e14e0`, arm `+0x440` for 250 ms (`0x00d9e140(250)`), and fall through to
      step 4;
   3. otherwise return the staged action id. `reason_out` is **not written** on this path.
4. **`ignore_ai`** (force flag, `+0x2bc` bit 1): return 14 `hold position`. `reason_out` is not written.
5. **Mark the current action as tried** when all hold: bit 0 (finished) clear, current action ≠ `0x47`,
   and that action's row byte `+0x14` is 0. Only `cover stay down` (11) has `+0x14` = 1 in the image,
   and the `combat_actions.xtbl` loader does not write that column (`spec-tables-weapons-combat.md`
   §11.1), so in practice every current action except `cover stay down` is pre-marked.
6. **Goal loop**, goal id `g` = 0, 1, …, 16, written into `reason_out` as it advances. A goal is
   evaluated only if its enabled byte in the character's behaviour record (record pointer `+0x31c`,
   byte `+0x1c6 + g`) is non-zero **and** its goal-table routine (row `+0x04`, §9.5) is non-null **and**
   returns true for the human. For an evaluated goal, walk its 24-byte action list (goal-table row
   `+0x08`…`+0x1f`, filled from `ai_goals.xtbl` `order_of_actions`), stopping at the first `0x47`. For
   each listed action `a`:
   - **accept** `a` — return it, with `reason_out` = `g` — when all hold: `a` is not yet tried; its
     ability entry is enabled in the record (byte `+0x26 + 6·a`); the precondition test
     `0x004f25c0(pre_flags(a), state)` passes (`pre_flags` = row `+0x15`); the per-action repeat timer
     `+0x320 + 4·a` has expired (`0x00d9e1e0`); and the action's routine (row `+0x08`), called as
     `(human, block_out)`, returns true;
   - otherwise mark `a` tried; then, if bit 0 is clear and the committed reason `+0x444` = `g` and the
     current action `+0x445` = `a`, **return `0x47`** ("keep the current action").
7. **No goal produced an action:** `reason_out` is left at 17, and the result is 48 `vehicle passenger`
   if either vehicle test holds (`0x0051db40`, which is also that action's own routine), else 14
   `hold position`.

**The precondition test `0x004f25c0(p, s)`** (CONFIRMED, 33 instructions) passes exactly when all three
hold: `p` and `s` share at least one of bits 0–2 (a common medium); `p` bit 3 is clear or `s` bit 3 is set
(an action needing a target needs one); and `s` bit 4 is clear or `p` bit 4 is set (while in scripted
cover, only actions marked `can do in cover`). All 70 masks are 0 in the file image, so an action is
choosable only after `combat_actions.xtbl` has given it at least one medium bit.

**What the rules add up to** (HIGH, each a direct consequence of the listing):

- **Priority** is fixed goal-id order 0…16 (code), then list order within a goal (data). The first
  acceptable action wins; there is no scoring.
- **Pre-emption.** On a re-evaluation, any acceptable action of a higher-priority goal replaces the
  current one. Reaching the current (goal, action) pair first yields `0x47`, which the step treats as
  "no change". If the current action's goal no longer passes its test, the current action — already
  marked tried — cannot be chosen again under any other goal in the same pass.
- **"Tried" spans goals.** An action that failed under one goal is not retried under a later goal in the
  same pass.
- **The action routine is both the last feasibility test and the block builder:** it receives
  `block_out` and the chooser commits whatever it left there (§9.2 step 7). The 70 routines were not read
  (§8).
- **Nothing finished, nothing chosen is impossible.** With bit 0 set the "keep" return cannot fire, and
  every other exit returns a real action, so a finished human always gets a new action proposal (which
  the request layer may still refuse).

### 9.5 The two static tables the chooser reads

**Goal table `0x012e22f0`** (17 rows × `0x20` bytes; CONFIRMED by static read). Row `+0x00` is the name
pointer, `+0x04` the **goal-test routine** (`int test(human)`), `+0x08`…`+0x1f` the 24-byte ordered
action list from `ai_goals.xtbl` (`spec-tables-traffic-ai.md` §9). All 17 test slots are filled. The
record byte column is the behaviour-record goal-enabled byte the chooser checks first (§9.4 step 6).

| Id | Goal | Test routine | Record byte |
|---|---|---|---|
| 0 | `change mode` | `0x00db60a0` (shared "return 1" stub) | `+0x1c6` (always 1) |
| 1 | `avoid brute` | `0x004f54c0` | `+0x1c7` |
| 2 | `kill near player` | `0x004f3f20` | `+0x1c8` |
| 3 | `survive` | `0x004f3bb0` | `+0x1c9` |
| 4 | `retreat` | `0x004f4020` | `+0x1ca` |
| 5 | `group` | `0x004f3c10` | `+0x1cb` |
| 6 | `suppress` | `0x004f3c70` | `+0x1cc` |
| 7 | `cover` | `0x004f4120` | `+0x1cd` |
| 8 | `taking_damage` | `0x004f4400` | `+0x1ce` |
| 9 | `advance` | `0x004f5590` | `+0x1cf` |
| 10 | `reload` | `0x004f2910` | `+0x1d0` |
| 11 | `kill enemy` | `0x004f3d70` | `+0x1d1` |
| 12 | `flush out` | `0x004f4490` | `+0x1d2` |
| 13 | `frustrated` | `0x004f3cf0` | `+0x1d3` (always 1) |
| 14 | `acquire target` | `0x004f45a0` | `+0x1d4` |
| 15 | `investigate` | `0x004f3de0` | `+0x1d5` |
| 16 | `idle` | `0x004f4720` | `+0x1d6` (always 1) |

"Always 1" is the `ai_behavior.xtbl` loader's rule (`spec-tables-traffic-ai.md` §10). The stub
`0x00db60a0` is two instructions (load 1, return), shared with unrelated code by identical-code folding,
like the stubs of §2.2.

**Action table `0x012e1c60`** (70 rows × `0x18` bytes; CONFIRMED by static read). The columns the
decision layer uses are `+0x04` category (the mode the action runs in), `+0x08` action routine
(`bool routine(human, block)`), `+0x10` `No_interrupt_ms`, `+0x14` the keep-current exemption byte and
`+0x15` `pre_flags` (column names from `spec-tables-weapons-combat.md` §11.1). The category and routine
columns are file data, never written by a loader:

| Mode (category) | Actions (id, name, routine) |
|---|---|
| 0 `Idle` (1) | 69 `idle` `0x0050eff0` |
| 3 `Brute Melee` (11) | 51 `brute throw prop` `0x00525600`; 52 `brute car flip` `0x00526240`; 53 `brute bull rush` `0x005275e0`; 54 `brute qte player` `0x00525db0`; 55 `brute attack brute` `0x00524ed0`; 56 `brute gun finisher` `0x0052a1b0`; 57 `avatar shockwave` `0x00525060`; 58 `avatar stomp` `0x006948c0`; 60 `avatar fireballs` `0x00525060`; 61 `avatar teleport stomp` `0x00525140`; 62 `avatar bullrush` `0x00524670` |
| 5 `Combat Movement` (18) | 14 `hold position` `0x00db60a0`; 15 `melee stand back` `0x0053d140`; 16 `move retreat` `0x0053d540`; 17 `move regroup` `0x00538700`; 18 `move advance` `0x0053a520`; 19 `move chase` `0x0053a7f0`; 20 `avatar chase` `0x0053adc0`; 21 `move surround` `0x0053c8e0`; 22 `move rush` `0x00539d90`; 23 `move rush to melee` `0x0053de30`; 24 `move scripted` **null**; 25 `move to navmesh` `0x00538270`; 26 `move sidestep` `0x0053e870`; 27 `move to post combat` `0x0053b080`; 28 `move to post idle` `0x0053b210`; 29 `move close in` `0x0053f520`; 66 `killbane chase` `0x00538ca0`; 67 `killbane strafe` `0x00538d40` |
| 6 `Commando` (1) | 63 `quick kill` `0x0052cbe0` |
| 7 `Cover popout` (1) | 13 `cover popout` `0x00541230` |
| 8 `Cover stay` (5) | 8 `cover take def` `0x00540b90`; 9 `cover take off` `0x00db60a0`; 10 `cover advance` `0x00540080`; 11 `cover stay down` `0x00543180`; 12 `cover fire` `0x00540f30` |
| 12 `Fire spray` (4) | 0 `fire` `0x005436f0`; 1 `fire sweep` `0x00543a20`; 2 `fire suppress` `0x005439b0`; 3 `fire chaos` `0x005435b0` |
| 14 `Follow` (10) | 30 `follow make way` `0x00545f20`; 31 `follow avoid LOF` `0x005453c0`; 32 `follow formation` `0x00547890`; 33 `follow acquire` `0x005479e0`; 34 `follow player` `0x00547f20`; 35 `follow teleport` `0x00548020`; 36 `follow lemming` `0x00548180`; 37 `follow to car` `0x005487e0`; 38 `follow other car` `0x00548e80`; 40 `follow browse` `0x00545450` |
| 15 `Grenade throw` (1) | 4 `throw grenade` `0x0054dce0` |
| 18 `Hijack` (3) | 39 `follow carjack` `0x00549080`; 49 `vehicle enter scpt` **null**; 50 `vehicle extract` `0x0051d2f0` |
| 21 `Human shield` (1) | 46 `human shield grab` `0x0050dfe0` |
| 22 `Investigate` (3) | 41 `investigate move` `0x00550990`; 42 `investigate look` `0x00550c40`; 43 `investigate peek` `0x00551280` |
| 23 `Melee` (2) | 6 `melee` `0x00551e20`; 7 `melee vehicle` `0x005525c0` |
| 25 `Passenger` (1) | 48 `vehicle passenger` `0x0051db40` |
| 26 `Pickup item` (1) | 45 `pickup weapon` `0x004f0b70` |
| 27 `Reload` (1) | 44 `reload` `0x00552730` |
| 30 `RollerBlader` (1) | 68 `roller blader change` `0x005303c0` |
| 35 `Taunt` (2) | 47 `taunt` `0x00552b90`; 59 `avatar taunt` `0x00524640` |
| 36 `Weapon throw` (1) | 5 `throw weapon` `0x005531a0` |
| 42 `Zombie eat` (2) | 64 `zombie eat` `0x00537b90`; 65 `zombie explode` `0x00537650` |

- **20 distinct categories**, exactly the 20 rows marked CAE in §6. The two sets were compared entry by
  entry: no CAE row lacks an action, and no action names a non-CAE mode. `Stunned` carries the
  committed-action network pair (§6) but no action has category 34.
- Two actions share the "return 1" stub as their routine: 9 `cover take off` and 14 `hold position`
  always accept and leave the block as initialised. `avatar shockwave` and `avatar fireballs` share
  `0x00525060`. `vehicle passenger`'s routine `0x0051db40` is just the in-vehicle test.
- **Two routines are null:** 24 `move scripted` and 49 `vehicle enter scpt`. The goal loop calls the
  routine **without a null check** (only the ordered re-assert path of §9.4 step 3 checks). HIGH: these
  two actions are reachable only as external orders; a goal list that names either of them, with the
  ability enabled and the precondition met, would call address 0. A port should treat them as never
  choosable by goals. **[A3, §11.1: CONFIRMED latent. No shipped goal list or behaviour names either
  action, and 49 has no `combat_actions.xtbl` row.]** **[A3, §11.3 A: 58 `avatar stomp`'s routine
  `0x006948c0` is a shared return-false stub, and 20's `0x0053adc0` jumps to 19's routine.]**

### 9.6 The 17 goal tests

All CONFIRMED by listing unless marked; each routine takes the human only. Sixteen of them re-check
their own record byte first (redundant with the chooser); `taking_damage` does not. Perception lists,
entries and the damage accumulator are described in §9.8; force-flag names in §9.9. "Seen within W" means
"the elapsed time on the entry's `+0x1c` timestamp is at most W milliseconds".

| Id | Goal | The test is true when… |
|---|---|---|
| 0 | `change mode` | always (stub). Goal 0's action list is therefore tried first on every decision. |
| 1 | `avoid brute` | some entry of perception list 0 with flag `0x8`, seen within 3000 ms, resolves (handle at entry `+0x10`) to a live, usable object of the base type whose descriptor (object `+0xf0`, byte `+0x48` bit 0; `0x0052a230`) has the bit HIGH-read as "is a brute". |
| 2 | `kill near player` | some entry of perception list 1 with kind `+0x28` = 33, seen within 3000 ms, lies within radius R of the human's position (human `+0x40`). R is the float at `0x0135bfc4`, squared once and cached (`0x01361980`, latch `0x01361990` bit 0). R lives in zero-fill memory; the only other reference to its address is a PUSH in non-function code at `0x00fec830`. HYPOTHESIS: a tunable registered at start-up; kind 33 is the player. |
| 3 | `survive` | the float `+0x1cb8` divided by the integer `+0x1cac` (0 when `+0x1cac` ≤ 0) is below `Survive/low_health_pct` (record `+0x1d8`). HYPOTHESIS: current and maximum health. |
| 4 | `retreat` | some list-0 entry with flag `0x8`, seen within 5000 ms, is closer than the human's **first** weapon range value (§9.8; 0 when unarmed, so an unarmed human never passes). |
| 5 | `group` | the human has a group leader (`0x0097e7b0(human, 0)`: resolved through `0x005083f0`/`0x004d7f30`, null when it would be the human itself) that is a player (`0x00853b30(leader, 0x21)`) or has force flag `squad_leader`; and `0x004e50e0` agrees: false for a human-typed human with `+0x1e01` bit 0, and false while the current action is 24 `move scripted`, 26 `move sidestep`, 45 `pickup weapon`, 53 `brute bull rush` or 63 `quick kill` (byte jump table `0x004e5160`), else true. **No distance is tested:** `Group/seperated_distance` is not read by any goal test (§9.7). |
| 6 | `suppress` | the human has a queued event record of kind 108 (`0x004f6310(108, null, own handle, 0)` walks the event list `+0x1c54` of §2.3, then a second list at `+0x1c50`), **and** either `+0xccd` = 9 with `+0xcce` = 17 or 19, or `0x00540a70(human, 0, 0x024fb1c0)` holds (not read; it lies in the cover-mode code range). |
| 7 | `cover` | see the ordered rule list below. |
| 8 | `taking_damage` | the human has no current cover object (§9.8) **and** the damage accumulator exceeds 0.05 (`0x01115d6c`) in every lane **and** perception list 3 is non-empty. |
| 9 | `advance` | see the ordered rule list below. |
| 10 | `reload` | `0x009c05b0(human, 0)` holds (holds a weapon whose loaded count is below capacity, plus further state tests not all read) **and** the clip fraction is at most `Reload/clip_pct` (record `+0x1e4`). The fraction is `0x00b81cb0` on the held item: item `+0x1a4` divided by weapon-info `+0x118`, with the capacity doubled when item `+0x1a1` bit 2 is set. |
| 11 | `kill enemy` | neither `cant_attack` nor `combat_disabled`, **and** either the time since the own target was last seen (`0x004fc760`, §9.8) is at most `Kill_Enemy/seen_in_last` (record `+0x1f4`), or `0x004dd640` holds (`+0xccd` = 9 with `+0xcce` = 15, 16 or 18; meaning OPEN). |
| 12 | `flush out` | the target was seen within `Flush_Out/seen_in_last` (`+0x1f6`); not `combat_disabled`; perception's handle `+0x250` does not resolve to a live object (`0x004fe910`); the target's list-2 entry, if any, lacks flag `0x800`; and **not** (damage accumulator above 0.1 in every lane while the human holds a ranged weapon and the target is not "engageable"). "Engageable" is `0x004ffc70(target)`: the target has a list-0 entry and either its list-2 entry has a flag in `0x06000000` or, with no list-2 entry, it was seen within `Kill_Enemy/seen_in_last`; never when the target is the `+0x250` object. |
| 13 | `frustrated` | timers `+0x438` and `+0x43c` are both armed, (elapsed on `+0x43c`) − (elapsed on `+0x438`) ≥ 8000 ms — the last commit came at least 8 s after the human last finished or had no action — **and** perception list 0's head entry has flag `0x100`. |
| 14 | `acquire target` | not `combat_disabled`; there is no own target or its list-0 entry has flag `0x400` (`0x004fdae0`); list 0's head entry has flag `0x8` and was seen within `Acquire_Target/seen_in_last` (`+0x1f8`). Then: true at once if the accumulator exceeds 0.1 in every lane while the human holds a ranged weapon and the target is not engageable; false if the head entry has both flags `0x10` and `0x1`; otherwise true when the target's list-2 entry has flag `0x800`, or `goals_can_process_early` is set, or the time t' is at least `Flush_Out/seen_in_last` (t' = elapsed on perception timer `+0x244` when the target has a list-2 entry, else the time since the target was last seen). |
| 15 | `investigate` | unless `goals_can_process_early` is set, the target has **not** been seen within `Flush_Out/seen_in_last`; an investigation candidate exists (`0x00550480`, below) and was seen within `Investigate/seen_in_last` (`+0x1fa`); not `combat_disabled`; and nothing is currently in view (`0x004fc7e0`: no list-0 entry with flag `0x40` and kind ≠ `0xff`). |
| 16 | `idle` | perception list 0 is empty, or `goals_can_process_early` is set, or list 0's head entry was last seen at least `Investigate/seen_in_last` ago. |

**`cover` (`0x004f4120`), in order** (CONFIRMED; first matching rule decides):

1. If force flag `in_scripted_cover` is set: true, unless `0x00540a70(human, 0, 0x024fb1d0)` holds (not
   read), in which case evaluation continues.
2. The live target T must have a list-0 entry seen within `Flush_Out/seen_in_last`; otherwise false.
3. A byte at `0x01115d10` (0 in the file image) would force true here: a compiled-out switch.
4. If the human has a current cover object, the range check runs except while `0x004dd640` holds and
   less than `Cover/advance_cover_time` (record `+0x1ec`) has elapsed on timer `+0x96c`. The check: false
   if the separation `0x00d9fc80` reports between the cover object's position (`0x004d63f0`) and T's
   exceeds (f·b)². Here b is the human's **second** weapon range value (§9.8), and f is 2.0 for a
   personality with `combat_aggression` = 2 (`0x00507ee0`, §9.8), else 1.0.
5. If `+0x76f` bit 3 is set and perception list 3 is non-empty: true.
6. If timer `+0x968` is not armed or not yet reached: true. Otherwise, if less than `Cover/pop_up_time`
   (`+0x1f0`) has elapsed on it — or 1000 ms when `+0x76f` bit 3 is set — false.
7. With no cover object: true if T holds a ranged weapon (§9.10 item 2) and the time since T was last
   seen is below `Flush_Out/seen_in_last`.
8. **`take_cover_when`** (record `+0x1ff` bits 7/6/5, per `spec-tables-traffic-ai.md` §10):
   - `damaged` (bit 7): true if the damage accumulator exceeds 0.1 in every lane;
   - `shot at` (bit 6): true if perception list 3 is non-empty;
   - `aimed at` (bit 5): true if T exists, `0x004fdac0` holds, and `0x0050cdb0(T, human, 0)` holds
     (HYPOTHESIS: "T is aiming at the human").
9. Otherwise false.

Rule 8 is the only place the decision layer reads `take_cover_when`. It ties the data-side flag names to
perception: `shot at` ↔ list 3, `damaged` ↔ the accumulator (HIGH).

**`advance` (`0x004f5590`), in order** (CONFIRMED; any failing step returns false):

1. The `reload` test (goal 10) must **fail**: a human that wants to reload does not advance.
2. A live target T, and `0x004fe390(perception, out, T, 0, 0)` true (not read; HYPOTHESIS: a
   last-known-position query). **[A3, §11.6: CONFIRMED. It is true only while T is an actively known
   hostile (flag `0x8`).]**
3. If the human holds a ranged weapon, is not `0x0052a2e0` (the brute bit together with weapon-info
   `+0x1c` = 12), and is not an aggressive personality: `0x004df250(human, T)` must be ≤ 0 (not read;
   it calls `0x004fe390` and `0x004de500` with both positions).
4. The investigation candidate (`0x00550480`) must exist and have been seen within
   `Acquire_Target/seen_in_last`.
5. If in a cover object, `0x004dd640` holds, less than `advance_cover_time` has elapsed on `+0x96c`, and
   the personality is not aggressive: false when T is within (2·b)² of the cover object.
6. `0x004e68e0(human, T, 0, 0)` must return exactly 1 (not read).
7. If `0x009422d0(human)` (descriptor `+0xf0`, byte `+0x48` bit 7) holds and T is in a vehicle
   (`+0x16d4` = 3), the result is a vector comparison involving T's vehicle (resolved from T's `+0x16c0`
   handle via `0x004dcf00`, then `0x004e44b0`, `0x004f4770`, `0x004f2970` and the constant vector
   `0x01116a20`; not decoded). Otherwise true.

**The investigation candidate `0x00550480(human)`** (CONFIRMED). It queries perception (`0x004fcce0`,
not read) with a static table of seven `0x20`-byte rows at `0x013bb248`, naming perception lists 1, 0,
15, 16, 14, 13 and 7 (only the first six when `0x0097e280(human)` holds). If the best entry is the human
itself, that entry is flagged `0x10000` and the query repeats. A list-6 entry for the same handle is
returned in preference when one exists. **Quirk (HIGH):** the table is built once per process (latch
`0x013bb328` bit 0), and every row's `+0x10` receives the `Investigate/seen_in_last` of whichever
character first calls it; later characters with other behaviour records reuse that first value.
~~Whether `0x004fcce0` uses that field as a maximum age is HYPOTHESIS.~~ **[A2, §10.7: CONFIRMED.
Each row is {list 1, 0, 15, 16, 14, 13, 7; null handle; maximum age = that latched value; exclude mask
`0x10000`}.]**

**The `seen_in_last` cascade** (HIGH, from the tests above). The four windows are forced
non-decreasing by the loader: `Kill_Enemy` ≤ `Flush_Out` ≤ `Acquire_Target` ≤ `Investigate`. As the
time since the target was last seen grows, the tests hand over in that order:

- `kill enemy` holds inside the first window;
- `flush out` holds inside the second;
- `acquire target` and `investigate` take over once the target is older than `Flush_Out`;
- `idle` holds once even the newest perceived entry is older than `Investigate`.

`goals_can_process_early` (record `+0x1ff` bit 2) is read only by `acquire target`, `investigate` and
`idle`. Each time it lets the lower-priority goal apply without waiting for the higher window to lapse,
which matches its name.

### 9.7 From an action to its mode and its per-action data

- **Action → mode.** The category is action-table `+0x04` (a 32-bit field whose low byte the code reads).
  The decision step reads that column directly. `0x004f28f0` is the bounds-checked form (`0x2d`, "no
  mode", for ids ≥ 70) used by the dead call of §5. The column has 12 readers in 9 functions
  (`0x004f28d0`, `0x004f28f0`, `0x004f39e0`, `0x004f3a90`, `0x004f3af0`, the commit writer `0x004f4d00`,
  the finish routine `0x004f4f50`, `0x004f50b0` and the decision step). CONFIRMED by `xref`.
  **Clarifies the A1 brief:** the category is row `+0x04`; row `+0x08` is the action routine.
- **Per-character, per-action state.** 70 repeat timers at `+0x320 + 4·a` (`+0x320`…`+0x437`, ending
  exactly where the commit timer `+0x438` begins). The chooser requires the timer to be armed **and**
  passed (`0x00d9e1e0`); a timer holding a negative value (never armed) blocks its action. Who first
  arms these timers at spawn was not traced (§8).
- **Behaviour-record ability entry** (record `+0x22 + 6·a`, 6 bytes). Field names are from the
  `ai_behavior.xtbl` loader (`spec-tables-traffic-ai.md` §10, CONFIRMED there by disassembly): `+0`
  `Repeat_min` (u16 ms), `+2` `Repeat_max` (u16 ms), `+4` enabled byte. The chooser reads only the
  enabled byte; the finish routine (§4.1) reads `Repeat_min`/`Repeat_max` as the bounds of its cool-down
  roll. **This settles §8 item 10:** the second word is `Repeat_max`, so the cool-down after finishing
  action `a` is a random value between `Repeat_min(a)` and `Repeat_max(a)` milliseconds. (The loader
  already raised `Repeat_min` to the action's `Min_repeat_time`, row `+0x0c`, and `Repeat_max` to at
  least `Repeat_min`.) **[corrects §4.1/§8 item 10 citation]:** the schema lives in
  `spec-tables-traffic-ai.md` §10, not in `spec-ai-behavior-format.md`, which only covers the override
  grid.
- **Action-table timing columns** (CONFIRMED by `xref` on each column's first address, including the raw
  scan): `Min_repeat_time` (`+0x0c`) is read only by the `ai_behavior.xtbl` loader `0x004f2b50`.
  `No_interrupt_ms` (`+0x10`) is read only by the decision step (§9.1). The keep-current byte (`+0x14`)
  is read only by the chooser. **`Auto_abort_ms` (`+0x0e`) and `refresh_desire_interval` (`+0x12`) are
  written by the `combat_actions.xtbl` loader `0x004f2650` and have no reader at their column
  addresses.** HIGH: unused in this build (a reader that reaches them through a row pointer plus an
  offset would not show in this search).
- **Behaviour-record fields the goal tests read:** goal bytes `+0x1c6`…`+0x1d6`; `low_health_pct`
  `+0x1d8`; `clip_pct` `+0x1e4`; `advance_cover_time` `+0x1ec`; `pop_up_time` `+0x1f0`; the four
  `seen_in_last` words `+0x1f4`…`+0x1fa`; flag byte `+0x1ff` bits 2, 5, 6, 7. **Not read by the
  chooser, the gate, the step or any goal test** (CONFIRMED for those bodies): `seperated_distance`(`_interior`)
  `+0x1dc`/`+0x1e0`; `abandon_cover_time`, `find_new_cover_time`, `stay_down_time`
  (`+0x1e8`/`+0x1ea`/`+0x1ee`); `Suppress/length` `+0x1f2`; `squad_cover_pct` `+0x1fc`; `dodge_chance`
  `+0x1fd`; `combat_aggression` `+0x1fe`; flag bits 0, 1, 3, 4. HYPOTHESIS: the action routines and mode
  ticks read these (§8). **[A3, §11.5: partly. The routines read `seperated_distance` and
  `dodge_chance`. Mode code reads the cover timing fields and `Suppress/length`. `squad_cover_pct` has
  no reader. `combat_aggression` is read only by the behaviour scorer `0x004f3470`.]**

### 9.8 Inputs read across the perception boundary (read side only; write side is A2)

The perception tick `0x00507520` runs on the sub-object at human `+0x510` immediately before the event
pump and the decision step (§1.4 step 5.2). The decision layer reads the following structures, which
perception visibly owns. Their **layout as read** is CONFIRMED by the listings named; who fills them, and
what most flag bits mean, is OPEN for unit A2 (§8). **[A2: now in §10 — 22 lists (§10.1), producers
(§10.3), flags (§10.4).]**

- **Back-pointer.** Perception `+0x20c` points back at the human (CONFIRMED: `0x004fc760`, `0x004fdae0` and
  `0x004ffc70` read the human's target handle and behaviour record through it).
- **Perceived-entity lists.** Heads at perception `+0x2f4 + 8·k`, that is human `+0x804 + 8·k`.
  - Each list is circular and singly linked through entry `+0x2c`. Entry `+0x24` holds the entry's own
    list number, which the "next" helper `0x004fadc0` uses to stop at the wrap.
  - Accessors: `0x004fadf0(k)` returns the head; `0x004fae00(k, handle)` finds the entry for a handle.
  - Lists read by A1: 0 (`+0x804`), 1 (`+0x80c`), 2 (`+0x814`), 3 (`+0x81c`) and 16 (`+0x884`), plus the
    investigation query's set {0, 1, 6, 7, 13, 14, 15, 16}.
- **Entry fields read:**
  - `+0x00` position (a 16-byte vector);
  - `+0x10`/`+0x14` the entity's handle;
  - `+0x18` a flag dword;
  - `+0x1c` a timestamp, whose elapsed time is the entry's age;
  - `+0x20` a float (accumulator input);
  - `+0x24` the list number;
  - `+0x28` a kind dword (33 tested in list 1; `0xff` skipped);
  - `+0x2c` the next link.

  Flag bits tested: `0x1`, `0x8`, `0x10`, `0x40`, `0x100`, `0x400`, `0x800`, `0x02000000`/`0x04000000`
  and `0x10000`. The decision layer itself **writes** `0x10000`, through the investigation candidate
  query (§9.6).
- **Readings that follow from the read side** (HIGH):
  - **List 0 is "currently perceived".** **[A2, §10.8 item 2: list 0 is the set of known hostiles;
    "currently perceived" is flag `0x40` inside it.]** Its flag `0x40` makes the "time since seen"
    helper return 0,
    and `0x004fc7e0` uses the same flag as "something is in view".
  - **List 1 is a memory of last sightings.** `0x004fc760(handle)` (a null handle means the human's own
    target) returns 0 when the list-0 entry has `0x40`, else the age of the list-1 entry, else
    `0x7fffffff`.
  - **List 3 is "shot at" events** (`take_cover_when` `shot at`, §9.6 `cover` rule 8).
- **The damage accumulator** `0x004fc8b0(out)` sums a 4-lane term over list 16. Each entry's term is
  built from its age, a period from `0x004f8cd0(entry)`, entry `+0x20`, and the constant vectors
  `0x0137fa60`/`0x01249140`. The goal tests compare every lane with 0.05 or 0.1. HIGH: a recent-damage
  measure that fades with time (it gates `taking_damage` and the `damaged` cover trigger). **[A2,
  §10.3: CONFIRMED. The "period" is list 16's 30000 ms lifetime. Each term is (fraction of maximum health
  that attacker removed) × (1 − age/30000), so 0.05 and 0.1 mean 5% and 10% of maximum health.]**
- **Other perception fields read:** timer `+0x244` (human `+0x754`), elapsed via `0x004fa330`; and a
  handle at `+0x250` (human `+0x760`), resolved by `0x004fe910`. ~~Their meanings are OPEN.~~ **[A2,
  §10.6: `+0x244` is the time since the last clear line of fire to the target; `+0x250` is an ally link
  set by the friend scan.]**
- **The human's target.** `+0x1330` holds the target handle. `0x004e4e50(human)` returns the target
  object only when:
  - force flag `cant_attack` is clear;
  - the handle resolves to a usable object of the base type;
  - that object is not down (`0x0096f4f0`), not `+0x3b` bit 0 (`0x008cc4d0`), and active (`+0x33`
    bit 3).

  This raises §1.3's HYPOTHESIS "current combat target" to HIGH.
- **Non-perception human fields the tests read:**
  - the first 8 bytes of the AI sub-object `+0x2b0`: the handle of the **current cover object**
    (resolved by `0x004e5e60`, which requires type-descriptor `+0x6` bit 4; its position comes from
    `0x004d63f0`; HIGH);
  - the timers `+0x968`/`+0x96c`, which cover-mode code writes (for example `0x005403c0` stores -1 into
    `+0x96c`, and `0x00540b90` — the `cover take def` routine — `0x00540c90` and `0x00541600` use
    `+0x968`). HYPOTHESIS: pop-up and advance timers; **[A3, §11.6: CONFIRMED owners. `+0x968` is
    the stay-down timer and `+0x96c` the time-in-cover clock, both armed by `0x00541600`.]**
  - `+0x76f` bit 3;
  - the byte pair `+0xccd`/`+0xcce`;
  - the health pair `+0x1cb8`/`+0x1cac`;
  - the vehicle-state word `+0x16d4`.
- **Weapon ranges** `0x004dd530(&a, &b, human, flag)` (CONFIRMED):
  - unarmed: a = 0, and b = 1.5 (`0x01115d14`), or 2.4 (`0x01115d18`) when the brute bit is set;
  - with a held item: a and b are its weapon-info `+0x124` and `+0x128`;
  - when `flag` is set or `0x00b83a00(info)` holds instead: a = 0, and b is the float at `+0x20` of the
    record pointed to by info `+0x440`.

  HYPOTHESIS: minimum and maximum engagement range. `retreat` uses a; `cover` and `advance` use b.
- **Personality** (CONFIRMED against `spec-tables-traffic-ai.md` §11). `+0x314` is the character's
  personality index. `0x00507ee0` reads entry `+0x04` (`combat_aggression`, clamped to ≤ 2 by the loader)
  of the record array `0x013b84d0` (stride `0x20`; an index at or past the count `0x013b84c8` uses
  entry 0) and tests for 2. This is the only personality value the decision layer reads.

### 9.9 The force-flag bytes `+0x2b8`…`+0x2bd`

CONFIRMED by listing of the 41 single-flag setters `0x004df990`…`0x004e3530`. These are the run of small
routines that `spec-ai-behavior-format.md` §1 found passing `"human_ai_data"` and one `"ai_force_flags…"`
literal to the tag hasher. They account for 41 of the 53 uses of the `"human_ai_data"` string; the
other uses tag non-flag fields, such as the pending target handle sent by `0x004e3830` (§3.1). Each
setter:

- on the remote path, sends a network record tagged with those two strings;
- on the local path, stores exactly one bit at offset `+0x8`…`+0xd` of its `this` object.

The apply step calls the `cant_flee` setter with `this` = human `+0x2b0` (`0x004e840a`, `0x004e846b`),
which fixes the base. So the AI sub-object at `+0x2b0` carries the per-character force flags at human
`+0x2b8`…`+0x2bd`. This settles that half of the open question in `spec-ai-behavior-format.md` §1: the
literals are network tags for replicated per-character flags, not `.xtbl` fields. Names below are the
literals' suffixes after `ai_force_flags`, quoted as engine data.

| Byte | bit 0 | bit 1 | bit 2 | bit 3 | bit 4 | bit 5 | bit 6 | bit 7 |
|---|---|---|---|---|---|---|---|---|
| `+0x2b8` | `sidewalking` | `wandering` | `sidewalk_leashed` | `attack_pedestrians_on_sight` | `combat_disabled` | `flee_after_attack_NOT_WORKING` | `stay_in_players_view_NOT_WORKING` | `always_sees_player` |
| `+0x2b9` | `attack_player_on_sight` | `attack_non_player_enemies_on_sight` | `dont_react_to_player` | `ignore_attacks` | `deaf` | `dont_observe` | `never_turn_on_player` | `always_cower` |
| `+0x2ba` | `always_follow_in_alternate_vehicle` | `can_leave_behind` | `cant_attack` | `cant_cower` | `cant_enter_vehicle` | `cant_flee` | `cant_grab` | `cant_pickup_weapons` |
| `+0x2bb` | `disallow_persona_lines` | `dont_accelerate_when_parachuting` | `dont_auto_equip_or_unequip_weapons` | `dont_flee_post_vehicle_exit` | `dont_teleport_to_follow_vehicle` | `dont_use_sidewalk` | `driver` | — |
| `+0x2bc` | `in_scripted_cover` | `ignore_ai` | `ignore_notoriety` | `squad_leader` | `use_lethal_weapons` | `move_always_walk` | `move_always_run` | `move_always_sprint` |
| `+0x2bd` | `streaker_shock` | `vehicle_enter_group_started` | — | — | — | — | — | — |

A 42nd routine in the same run, `0x004e36b0`, uses the same tag mechanism to write the **personality
index** `+0x314` (sub-object `+0x64`), not a flag.

**Real consumer/caller census for `sidewalking`/`wandering`/`sidewalk_leashed`/`dont_use_sidewalk`,
2026-10-09 (`spec-ai-runtime.md` §20.1 has the full detail) — all four previously listed with no known
reader.** An exhaustive `CALL rel32` opcode census found: every confirmed caller operates on an
already-EXISTING human (session/handle resolution), never a fresh spawn — a named/tracked-character task
dispatcher, a "release back to civilian defaults" batch resetter (sets both `wandering`/`sidewalking`
together), the inverse "leaving sidewalk behavior on entering combat/alert states" (7 near-identical case
blocks, cross-referenced to §20.4's gunfire-reaction chain), and the generic Lua/network by-name
force-flag dispatcher `0x008b14c0`. **Surprising negative, directly checked: the near-field pedestrian
spawn routine (§20.1's `0x0090b0a0`) never calls any of these 4 setters** — `sidewalking` is most
plausibly the AI sub-object's own default state at construction rather than something the spawner sets
explicitly; not independently confirmed which.

**Flags the decision layer reads** (CONFIRMED at the cited sites):

| Flag | Where it is read | Effect |
|---|---|---|
| `always_cower` | gate (§9.3) | No decisions at all. |
| `ignore_ai` | chooser (§9.4 step 4) | Always proposes 14 `hold position`. |
| `in_scripted_cover` | state mask (§9.4 step 2); `cover` rule 1 | Sets the `can do in cover` state bit. |
| `squad_leader` | `group` | Lets a non-player leader qualify. |
| `cant_attack` | `kill enemy`; the live-target helper `0x004e4e50` | No live target, so the `must have target` state bit stays clear and every target-needing action is blocked. |
| `combat_disabled` | `kill enemy`, `flush out`, `acquire target`, `investigate` | Those goals are refused. |

A side observation from the same scan: the apply step's `Cower` case (§3.2) clears `cant_flee` before
calling the `Cower` begin `0x004d5050` and sets it again afterwards, both only when `+0x4a6` bit 1 is
set (CONFIRMED by a `range` read of `0x004e83fe`–`0x004e8476`).

### 9.10 Corrections to earlier sections, and port notes

1. **[corrects §1.3, §1.4, §7 item 6] `0x00941f40` is the human's held item, not its vehicle.** HIGH.
   - It resolves the handle at `+0x90` of the inventory sub-object `+0x1b78` to an object whose
     type descriptor has `+0x9` bit 7. (The same helper with argument 1 reads `+0x98`, HIGH: the other
     hand.)
   - `reload` reads that object's loaded count and the clip capacity of its info record (`+0x19c`)
     (§9.6). `0x004dd530` reads weapon ranges from the same info record.
   - So the "vehicle" tests that A0 hung off `0x00941f40` → `+0x19c` are weapon-info tests:
     - `0x00536300` (throttle exemption) holds when the held item's info flags `+0x10` have bit
       `0x80000`;
     - `0x00b81440` (the tail `0x004dec10`, §1.4 step 9) is the ranged-weapon class test of item 2
       below.

     `0x004dec10` returns at once for a player (`0x00853b30(human, 0x21)`) and otherwise acts only when
     the held weapon is ranged. HYPOTHESIS: the firing-request hand-off, replacing A0's "driver-side
     hand-off to the vehicle system". Its helpers `0x009c7cc0`/`0x009d1aa0` were not re-read.
   - Separately, `0x00971cf0` (§1.4 step 2, and gate test 6 in §9.3) returns **its own argument** when
     that object's type descriptor has `+0xa` bit 1, else null. CONFIRMED. It is a class test on the
     human, not a vehicle lookup, so the `+0x13a8` timer of §1.4 step 2 is the human's own.
2. **`0x009ba7e0(human)` is "holds a ranged weapon".** CONFIRMED. It is true when the held item's info
   `+0x4c` — an index into the 23 weapon-class names of `spec-tables-traffic-ai.md` §1.5 — selects a
   "true" target in the byte jump table `0x00b8146c`/`0x00b81464` used by `0x00b81440`. The set is:
   `pistol`, `smg`, `rifle`, `shotgun`, `launcher`, `stungun`, `flamethrower`, `minigun`,
   `pepper spray`, `waterspray`, `vehicle`.
3. **[corrects §4.3] `0x004f4eb0` has no observable effect.** It acts only when an order is staged and
   current. It then resolves the handle at `+0x4b0`/`+0x4b4` through `0x004e4b60`, a pure lookup, and
   discards the result. HIGH: a stripped remnant, like the dead call of §5. "Clears the previous action"
   overstates it; a port can omit the call.
4. **The committed reason can be indeterminate.** HIGH. The chooser's ordered re-assert path and its
   `ignore_ai` path return without writing `reason_out` (§9.4 steps 3–4). The decision step's reason
   byte is a stack slot that the block initialiser does not cover, so a commit from those paths stores
   whatever that byte held into `+0x444`. A port should store a defined value: 17 ("no goal"), the
   chooser's own fallback value, is the natural choice.
5. **[refines §4.4] An uncompleted order is re-issued, not just left open.** HIGH.
   - If an ordered action is finished while its mode is no longer current, the finish routine's
     completion step (§4.1 step 3) does not run. `+0x44c` bit 3 and the staged block `+0x4a8` therefore
     survive.
   - At the next decision the chooser's re-assert path returns the staged action again once `+0x440` has
     expired. The step then re-commits it and re-requests its category mode.
   - An order therefore keeps being re-issued until it completes, is cancelled (`0x004f4c90` clears
     bit 3), or its own routine refuses it. On a refusal, `+0x44c` bit 4 must be set; the chooser then
     backs off 250 ms and runs the goals instead.
6. **[corrects the A1 brief, not the spec] CAE count.** §6 marks **20** modes CAE (13 with the
   committed-action network pair plus the 7 listed under "Pattern"), and the action table confirms
   exactly those 20 (§9.5). It is not 27.
7. **`0x009BA300`** (the behaviour-assignment gate of `spec-ai-behavior-format.md` §3.1) was read on the
   way. It is "`0x00941f60(human)` is non-null": the inventory (`+0x1b78`) item for slot or category 10
   (`0x005ff110(10)`), accepted only when its info `+0x1c` = 3 (`0x00b81850`). The riot-shield reading
   stays HYPOTHESIS; the enum behind info `+0x1c` was not identified. The same field compared with 12
   appears in `0x0052a2e0` (§9.6 `advance`).

---

## 10. Perception's write side (unit A2)

**Scope.** This section covers the perception sub-object at human `+0x510`: its storage, the perception
tick `0x00507520`, the producers that fill each list, the entry flags, the damage memory behind
`0x004fc8b0`, the timer `+0x244` and handle `+0x250`, and perception's public query `0x004fcce0`. It
answers §8 item 12 and part of item 2. It does not trace the vision line-of-sight service
(`0x004ed1e0`/`0x004ed550`), the 3-D maths helpers, or the zero-fill perception tunables block
(`0x013b7fd8`…`0x013b8450`), which is filled at run time from a source not traced here. **[A4, §12.1/
§12.4: the tunables block is the `tweak_table.xtbl` registry (real names and shipped values given); the
LOS service's answer-decode and request-queue halves are read in full, leaving the underlying raycast
execution itself as a physics-domain boundary.]**
**Method.** As for A1: a private disposable Ghidra project copy (`tools\gp_aiA2`, deleted after the
dumps); `func`/`range`/`ptrs`/`xref`/`calls` dumps; a private function-list script for the range
`0x004f8000`–`0x0050b000` (252 functions); a byte-level read of float constants with
`tools/harnesses/exe_va.py`. Offsets below are **perception-relative** unless marked "human"
(human offset = perception offset + `0x510`).

### 10.1 Storage: 22 lists, a global entry pool, and a static per-list table

CONFIRMED by listing of `0x00507520`, the allocator `0x004f8cf0`, the linker `0x004fba10`, the updater
`0x004fbcb0`, the generic adder `0x004fec80`, `0x004fdde0`, `0x004fdf10`, the remover `0x004fcbd0`, the
pool helpers `0x004f8c20`/`0x004f8c50`, the column readers `0x004f8ca0`/`0x004f8cc0`/`0x004f8cd0`/
`0x004f8ce0`, the reset `0x004fc4a0` and the constructor `0x004fd930`.

- **22 lists (0…21), not 17.** Each has an 8-byte header at `+0x2f0 + 8·k`: a count dword, then the head
  pointer (`+0x2f4 + 8·k`, as §9.8). The 22 headers end at `+0x3a0`, which is a separate
  **deferred-free list**: evicted or removed entries are parked there, and the tick returns them to the
  pool on its next call. **[corrects §9.8]** entries are doubly linked: `+0x2c` next and `+0x30`
  previous, so an entry is `0x34` bytes.
- **The entry pool** is global: free-list head `0x0137fa10`, built by `0x004f8ad0`. The allocator
  `0x004f8cf0(list, kind, handle, flags, &position, &score)` fills `+0x00` position, `+0x10` handle,
  `+0x18` flags, `+0x20` score (first lane of the score vector), `+0x24` list number, `+0x28` kind, and
  restarts the timestamp `+0x1c` at "now".
- **Static per-list table** `0x012e2510`: 12-byte rows {capacity, lifetime in ms, order}, read by
  `0x004f8ce0`/`0x004f8ca0` (capacity), `0x004f8cd0` (lifetime) and `0x004f8cc0` (order). The table is
  file data; no writer was found by a raw scan of the three column addresses.

  | List | Capacity | Lifetime (ms) | Order | List | Capacity | Lifetime (ms) | Order |
  |---|---|---|---|---|---|---|---|
  | 0 | 15 | never (−1) | 0 | 11 | 4 | 1000 | 0 |
  | 1 | 15 | 40000 | 0 | 12 | 4 | 10000 | 0 |
  | 2 | 4 | 4000 | 0 | 13 | 6 | 60000 | 1 |
  | 3 | 3 | 5000 | 0 | 14 | 6 | 60000 | 0 |
  | 4 | 15 | 30000 | 0 | 15 | 4 | 60000 | 1 |
  | 5 | 8 | 20000 | 0 | 16 | 6 | 30000 | 0 |
  | 6 | 4 | 60000 | 0 | 17 | 5 | 3000 | 1 |
  | 7 | 1 | 30000 | 0 | 18 | 15 | 4000 | 1 |
  | 8 | 1 | 3000 | 0 | 19 | 15 | 30000 | 1 |
  | 9 | 15 | 1000 | 0 | 20 | 15 | 12000 | 1 |
  | 10 | 4 | 15000 | 0 | 21 | 5 | 6000 | 0 |

- **Insertion** (`0x004fba10`). An entry with a negative score is refused.
  - **Order 0:** the list is kept sorted by score, **ascending** (lowest score at the head). When the
    list is full, a newcomer is refused unless its score is below the tail's; otherwise the tail is
    evicted.
  - **Order 1:** most recent first. The newcomer becomes the head; when the list is full, the tail
    (oldest) is evicted.
  - Capacity 0 would mean "unbounded" here and route the generic adder to a single-slot path
    (`0x004fdf10`), but no list has capacity 0.
- **Stimulus records.** Producers do not write entries directly. They build a `0x38`-byte record:
  `+0x00` position, `+0x10` handle, `+0x20` score vector, `+0x30` flags, `+0x34` update mode. The
  builders are `0x004fa7a0`, `0x004fa7e0`, `0x004fcff0` (position taken from the object) and `0x004fa760`
  (copy of an existing entry). The generic adder `0x004fec80(list, record)` either updates the entity's
  existing entry in that list or creates one through `0x004fdde0`.
  - **Update** (`0x004fbcb0`): with mode 2, refused unless the new score is lower than the entry's.
    Otherwise the entry is unlinked, its position, score and flags are overwritten, its timestamp is
    restarted, and it is re-inserted. **Flag bits `0x1`, `0x2` and `0x4` survive** an update; every
    other bit is replaced by the record's flags.
  - **Create** (`0x004fdde0`): only on the locally owned human. The handle must resolve to a usable
    object whose type descriptor has is-a bit 0 (`+0x6` bit 0). A player is refused while
    `0x00614d00()` returns 3. A human may hold an entry about **itself** only in lists 10 and 15.
- **Kind `+0x28` is the entity's exact type byte** (object `+0x34`). HIGH: the type descriptor's bytes
  `+0x6`… are an "is-a" bitset indexed by type number. `0x00853b30(obj, n)` tests bit `n` of it (bit
  `0x21` = `+0xa` bit 1 is the player class of §9.3, bit `0x22` = `+0xa` bit 2 the "human-typed" bit of
  §1.2). So the `kind == 33` test of `kill near player` (§9.6) asks for an entity whose own type is 33,
  the player type. **No live producer writes kind `0xff`**: every creation path stores a resolved object's
  type byte after rejecting unusable objects, whose type byte is `0xff` (`0x00853b10`). The `0xff`
  checks in the readers (§9.6 `investigate`; list 11 in 10.2) are defensive. The one function that could
  store another kind, `0x004fe570` (a list-11 creator), has no references and is dead code.
- **Entry removal.** `0x004fcbd0(entry)` moves an entry to the deferred-free list. `0x004fdb20(handle)`
  removes a handle from all 22 lists; `0x004fdb60(handle, keep0)` removes it from lists 0 (unless
  `keep0`), 4, 14 and 16. `0x004fc4a0` (called from outside, for example by `0x004e61b0` and
  `0x009d9080`) is the full reset: it empties every list, clears `+0x248`/`+0x250`, zeroes `+0x260`, and
  re-arms all the pass timers to fire at once.

### 10.2 The perception tick `0x00507520`, and expiry

CONFIRMED by listing (306 instructions). `this` is the perception sub-object; `+0x20c` is the
back-pointer to the human (§9.8). With no human it returns at once. In order:

1. **Cover-abandon check.** `0x0050a000(human)` tests whether the human should leave its current cover
   object (cover timers `+0x964`/`+0x968`/`+0x96c`, `abandon_cover_time` and `advance_cover_time` from
   the behaviour record, and the target's list-16 entry). If so, the cover handle is cleared through
   `0x004e5d20` on the AI sub-object `+0x2b0`. This step belongs to cover logic and only reads
   perception.
2. **Expiry pass**, when timer `+0x230` has passed; it re-arms for a random 400–500 ms. Every entry of
   every list is tested by `0x004fdd10`, and expired entries are unlinked and returned to the pool. The
   test, by list (byte jump table `0x004fddc8`):
   - lists 0, 4, 6: expire if the handle no longer resolves (`0x004d7f30`) or the object is down or
     dead (`0x0096f4f0`);
   - list 1: expire also if the entity has **no list-0 entry** (a "last seen" record never outlives the
     awareness it belongs to);
   - list 11: expire if the kind is not `0xff` and the handle no longer resolves (`0x004e4b60`);
   - then, for every list: expire when the lifetime is positive, flag `0x4` is clear, and the age
     exceeds the lifetime.

   **This answers the "period" question of §9.8:** `0x004f8cd0` returns the list's **lifetime**. For
   list 16 that is 30000 ms.
3. **Return the deferred-free list** `+0x3a0` to the pool (`0x004f8c50`).
4. **Target-refresh pass** `0x00506660`, when the staggered timer `+0x23c` fires; it re-arms for
   300–500 ms (10.3, list 0 and list 2).
5. **Sense pass**, when the staggered timer `+0x210` fires:
   1. if the human's team byte `+0x1ca8` is not 0, 4 or 7, every player that has a list-0 entry is
      reported to `0x00608740` with the team index from `0x006051c0` (HYPOTHESIS: notoriety or wanted
      bookkeeping, not read);
   2. clear `+0x25f` bit 1; call `0x0091f310(2)` on the human; recompute the **activity level** `+0x260`
      with `0x00502b00` (below);
   3. keep the "provocation" meter at `+0x200` (float), `+0x204` (enabled byte) and `+0x208` (timer)
      in step with force flag `never_turn_on_player` (`+0x2b9` bit 6; `0x004fa8a0`), and zero the meter
      when its timer runs out;
   4. drop the ally link `+0x250` when it no longer qualifies (10.6);
   5. for a follower (`0x0097e280`), set `+0x25f` bit 5 from `0x004fb3b0` (not read);
   6. the **main sense pass** `0x00506c20` (10.3) and the **search pass** `0x00503090` (10.4, flags
      `0x10`/`0x1`);
   7. re-arm `+0x210`: 650–800 ms, or 350–450 ms when the activity level is below 6 and `0x00943ea0`
      fails.
6. **Slow pass** `0x00502f10` when timer `+0x21c` fires (every 1500–2000 ms; not read). **[A4, §12.6:
   read — a world-wide (not proximity-gated) down/dead sweep of every live human, feeding list 7.]**
7. **Projectile pass** `0x00507290` every 120–200 ms (`+0x220`) and **vehicle pass** `0x00507440`
   every 200–400 ms (`+0x228`) (lists 10 and 9, 10.3).
8. Clear `+0x25f` bit 0.

**Staggered timers.** `+0x210`, `+0x220`, `+0x228`, `+0x234` and `+0x23c` are 8-byte objects: a
deadline and a byte pair, used through `0x004fa0f0` (has it fired?) and `0x004fa820` (re-arm). On top of
the plain deadline they spread work across humans with a global slot counter (`0x013b7fc0`…`0x013b7fd4`)
and a performance-counter sample (`0x013b8018`). HIGH: frame-load balancing; a port may use plain
timers. The plain timers `+0x218`, `+0x21c` and `+0x230` use the ordinary helpers `0x00d9e140`/
`0x00d9e1e0`.

**The activity level `+0x260`** (`0x00502b00`, CONFIRMED by listing; the meaning of each value is HIGH):

| Value | When |
|---|---|
| 0 | not locally owned (or no human) |
| 1 | `0x004de300` fails or the human is down or dead (`0x0096f540`), and human `+0x1da4` bit 7 is clear (inactive) |
| 3 | certain current modes (byte jump table at `0x00502e1b`) or a vehicle state, read but not decoded |
| 6 | in combat: `0x00943a20` or `0x004fd720` holds |
| 5 | `0x0094f690`, a team rule (`0x00605220`), or a player within the 125-unit test |
| 4 / 2 | a player within the 250-unit test, else 2 |

The producers gate on it: the main sense pass needs more than 2, damage and gunfire need more than 1,
and the reckless-vehicle report needs more than 4.

### 10.3 The producers, list by list

All CONFIRMED by listing unless marked. "Score" is the record's score; for distance-type scores, see
the score scales below.

**List 0 — the known hostiles (awareness).**
- **Sensing.** The main sense pass `0x00506c20` walks the object manager's `+0x1e4` list. For each other
  live human it computes the attitude `0x004fe0f0` (below).
  - Attitude 0, or the human's own target: the **vision test** `0x005055a0`.
  - Other attitudes are handled only on the once-a-second sub-pass (`+0x218`) or for players:
    attitude 4 calls the friend scan `0x00505210` (list 5); 3 calls `0x004ff760` (list 5); 2 is ignored;
    1 calls `0x00502fa0` under a further condition (not read). When that call succeeds and the object
    is being carried, the carrier is passed to the witness routine (list 13).
  - With force flag `always_sees_player` (`+0x2b8` bit 7), every player is re-noticed on every pass.
- **The vision test** `0x005055a0(obj)`:
  - an unusable or hidden object (`+0x3b` bit 0) is "lost" (`0x004fd840`);
  - an object within the 25.0 proximity test (`0x004defd0`) is noticed without sight (`0x00503900`);
  - the view-cone test `0x004fef80` must pass;
  - the asynchronous line-of-sight answer `0x004ed1e0` decides: 2 = visible, which calls the **see**
    routine `0x005013b0`; 3 = blocked, which calls the "lost" routine `0x004fd840` and adds flag
    `0x02000000`;
  - finally a new line-of-sight request is queued (`0x004ed550`). Its re-check interval is 700–4000 ms,
    chosen from player/follower status, the activity level and posture bits, with +500 ms when the last
    answer was "visible".
- **See** `0x005013b0(obj)`:
  1. **notice** `0x00500f70(obj, obj position)`, which creates or refreshes the list-0 entry;
  2. on that entry, set `0x8`, `0x10`, `0x40`, `0x100`, `0x400` and clear `0x200`, `0x10000`,
     `0x02000000`;
  3. add a **list-1** record: the object's current position, the list-0 entry's score, flags 0;
  4. set `+0x25f` bit 2.
- **Notice** `0x00500f70(obj, position)`, the only creator of sensed list-0 entries:
  - It refuses entities whose attitude is 4 or more. A player-class entity is accepted only while force
    flag `never_turn_on_player` is clear, and, at attitude 4 or more, only while the provocation meter
    `+0x200` is at its threshold (`0x004fa220`).
  - It removes the entity's list-6 entry (`0x004d63d0(6, obj)`).
  - It creates the entry with score 0, or refreshes position and timestamp in place.
  - It sets `0x8` and clears `0x1`, `0x10`, `0x10000`.
  - If the entity is the human's target, it updates the target memory (`0x004fd1b0`).
  - On a refresh it calls `0x004f6420(71, entity, human)`, which walks the human's live event list for
    kind-71 records from that entity. HIGH: it cancels the pending "sighting lost" event (10.5).
- **Notice without sight** `0x00503210(obj, position, radius)`, wrapped by `0x00503900`, which then adds
  flags `0x280` unless `0x40` is set:
  - It returns an in-view entry unchanged, and promotes a list-6 entry to list 0.
  - It then **blurs the position**: a random offset up to min(radius, 0.5 × distance), times the
    tunable `0x013b8450`, refined toward the last known position by the entry's age.
  - It calls `0x00500f70` with the blurred position.
  - Callers: proximity (above); damage and friendly fire (list 16 below); gunfire (list 3); witnessed
    attacks (list 13); the target-refresh pass for list-0 entries carrying flag `0x2`.
- **Direct awareness** `0x005023f0(obj)`, with 12 external callers (for example `0x004dedb0`,
  `0x004e7b00`, `0x0052dcd0`, `0x00ab2480`), returns an existing entry, else promotes a list-6 entry,
  else creates one with the **maximum score** (`0x0137fa90`, about 3.06e38) and flags 0.
- **Order.** Sensed entries are created with score 0, and the external path and the dismiss pass
  (`0x004fe980`, which sets the score to the maximum) are the only other score writers found. So among
  sensed hostiles **list 0's head is the most recently created one**. HIGH; this is the "head entry" that
  `frustrated`, `acquire target` and `idle` read (§9.6).
- **New-entry hook** `0x004fa950`: a new list-0 entry calls `0x004e4fa0(human, handle)` (not read).
- **Target-refresh pass** `0x00506660` (every 300–500 ms):
  1. run the line-of-fire probe for the target (list 2);
  2. for every list-0 entry whose object is still a usable, live human: if the entry has flag `0x2`,
     re-notice it at the object's true position; then run the vision test and `0x00503940`;
  3. for objects that are gone or dead, remove the entity from lists 0, 4, 14 and 16;
  4. choose the target (`0x004fc100`), and post event 78 to the human when the target handle changed.
     **[A4, §12.5: corrected — `0x004fc100` RE-VALIDATES the current target handle (actively known,
     non-dismissed), it does not pick a new one by priority. Event 78 fires when validation fails and
     the handle is cleared, not when a new target replaces the old.]**

**The attitude `0x004fe0f0(obj)`** returns 0…4+, lower meaning more hostile. Its base is the
team-relationship table `0x009b7100(team, team)` on the team bytes `+0x1ca8`. It is overridden by the
provocation meter (players), by force flags `attack_player_on_sight` (`+0x2b9` bit 0),
`attack_non_player_enemies_on_sight` (bit 1) and `attack_pedestrians_on_sight` (`+0x2b8` bit 3), and by
brute and follower rules. HIGH as a whole; the table itself was not decoded.

**List 1 — last confirmed sightings.** Written only by "see" (above): position = where the entity was
when last seen. It expires after 40 s, or as soon as the entity leaves list 0. The dismiss pass
`0x004fe980` empties it. This raises §9.8's reading to CONFIRMED.

**List 2 — line of fire to the target** (`0x004ffd30`, from the target-refresh pass; HIGH for the probe
geometry, which calls the ray helper `0x0075d7e0` and the last-known-position query `0x004fe390`).
- **Preconditions:** the target has a list-0 entry with `0x100`, and with `0x40` if it has `0x10`; the
  human holds a weapon.
- **Throttle:** the previous list-2 entry is at least 300 ms old — 500 ms if it carried `0x01000000`,
  1500 ms if it carried `0x02000000`/`0x04000000`.
- **Probe:** from the weapon along the aim to the target's last known position, limited by the weapon's
  range (`+0x120`).
- **Result, written as a list-2 record with score 0:**
  - no obstruction: flags `0x04000000`, or `0x0a000000` on the alternate probe, and **timer `+0x244` is
    restarted** (10.6);
  - obstruction: flags `0x08000000` or `0x01000000`, the position is the hit point, and `0x800` is added
    when the hit lies within the first 0.2 of the probe.

  Bit `0x08000000` alternates between the two probe variants on successive probes.

**List 3 — shots at me, and list 14 — gunfire heard** (`0x005046e0`, called from the firing code
`0x009d0500` through `0x004df300`):
- It needs an activity level above 1, and returns at once while the shooter's list-3 entry is younger
  than 500 ms.
- It measures how close the shot's path passes to the human (near-miss radius = the larger of weapon
  info `+0xd8` and 6.0) and how close the impact points land (tunables `0x013b8280`/`0x013b8290`).
- **Non-hostile shooter** (attitude 3 or more, and the human's team is not 7): the human notices the
  **shooter's target** instead (`0x00503210`), which for a player-class shooter is its target handle
  `+0x1e30`.
- **Hostile shooter**, either of two cases:
  - the human's `0x00507e30` record has `+0x6` bit 2 (HYPOTHESIS: a nervous personality): write a
    **list-3** record (update mode 2), say "sounds like trouble" (`0x00754410`), and notice the shooter
    (`0x00503900`);
  - the shot landed close: write a **list-3** record. For a player-class shooter it also sets human
    `+0x316` bit 1 and may call `0x009daad0` (HYPOTHESIS: a crime report).
- **Near miss, or a shot heard:** notice the shooter (`0x00503210` when close, else hearsay
  `0x005034e0`) and write a **list-14** record (update mode 2).
- Event 76 is posted to the human unless one is already queued (`0x004f6310(76, …)`).

This confirms §9.8's "list 3 = shot at", and the `cover` rule's `shot at` trigger (§9.6).

**List 6 — suspected hostiles (hearsay).** `0x005034e0(obj, position, radius)`:
- it hands over to `0x00503210` if the entity is already in list 0;
- otherwise it applies the same attitude gate and writes a **list-6** entry: blurred position, distance
  score from `0x004fa9f0`, flags 0, and `0x8` on refresh.

Sources:
- a friend's current threat (friend scan, list 5 below);
- the target of a player-class friend;
- gunfire from a shooter not yet known;
- witnessed attacks outside line of sight;
- reckless drivers: `0x00506980`, reached from `0x0055a0a0` through `0x004e9920`, which reports a
  driver to every idle AI human in range; hostile drivers go to list 6, others to list 5.

Further list-6 writers (`0x004ff9a0`, `0x00501ef0`, `0x00502350`) have no references. A list-6 entry is
removed when the entity enters list 0, and the investigation candidate prefers it (§9.6). Flag `0x800`
is set when the score is at most 100 (score scales below).

**List 7 — a body found** (`0x004ff890`, from `0x004ffa50` and `0x00505540`): a down or dead human
(`0x0096f4f0`), capacity 1, 30 s. The new-entry hook posts **event 0** to the human (500 ms delay)
unless human `+0x1e01` bit 2 is set.

**List 13 — attacks witnessed but not seen** (`0x00503a80(attacker, victim, weapon info, radius, …)`,
the witness routine; callers include `0x004de700`, which has 12 external callers, the sense pass for
carried humans, and the noise routine below):
- If the human is itself the victim: **event 12** with a random 1–2 s delay and the weapon class as
  payload.
- If a player-class victim is followed by this human: notice the attacker; **event 11**.
- Otherwise, with the team-relationship tests `0x004f8d90`/`0x004fa040`: if the attacker is visible,
  run "see". Else, if within the radius, write a **list-13** record: the attacker's position, score 100,
  flags `0x80`, update mode 1. Else call `0x00502130` (list 12).

**List 15 — loud events** (`0x00503f70(source, position, info)`; `0x004e6f50` broadcasts it to every
live AI human, called from `0x005929c0`; also called from `0x0096b720`). HYPOTHESIS: explosions and
similar noises.
- Within the radius `info+0x38` of the human, write a **list-15** record: the source handle, the event
  position, a distance score.
- Flag `0x800` when close (tunable `0x013b82b0`); flag `0x100` when `info+0x24` = 3.
- Then the witness routine runs for the source.
- Self-entries are allowed (one's own explosion).

**List 16 — damage memory, and list 8 — friendly fire** (`0x00504130(attacker, damage, flag)`, called
from the damage path `0x0096fa20`, from `0x004e69e0` and from `0x0052adc0`):
1. It needs an activity level above 1, a positive damage value, and an attacker other than itself.
2. A player attacker with `flag` set feeds the provocation meter (`0x004fa180`). The human says a line:
   "ouch" for a new attacker, "stop it" for a repeat (`0x00754410`); a player-specific line also goes
   through `0x0070dcf0`.
3. It clears human `+0x317` bit 2.
4. Let K = 100.0 (`0x0137fa60`), H = human `+0x1cac` (maximum health, §9.6), and R = what the existing
   list-16 entry for this attacker still remembers: 1 − decayed/K, where decayed is `0x004fa6b0`'s value
   below (R = 0 for a new attacker). The new score is **K·(1 − damage/H − R)**, clamped at 0.
5. It writes a **list-16** record: position = the **victim's own** position, handle = the attacker,
   flags `0x2`, update mode 0. List 16 is sorted ascending, so the attacker that has done the most
   remembered damage is at the head.
6. Attitude below 3: notice the attacker (`0x00503900`). Attitude 4 or more: a **list-8** record (the
   attacker, score × 2, update mode 2), i.e. friendly fire, capacity 1.

**The fade, and what `0x004fc8b0` sums** (CONFIRMED by listing of `0x004fc8b0`, `0x004fc960` and
`0x004fa6b0`). For an entry with score s and age t on a list with lifetime L (30000 ms for list 16),
`0x004fa6b0` gives the decayed value s + (K − s)·t/L. The accumulator adds 1 − decayed/K over every
list-16 entry, which is **(1 − s/K)·(1 − t/L)**: the fraction of maximum health that attacker took
off, fading linearly to zero over 30 s. `0x01249140` is the all-ones vector, and all four lanes are
equal, so the lane-by-lane comparisons of §9.6 reduce to one number.
- **0.05** = 5% of maximum health, recently taken (`taking_damage`);
- **0.1** = 10% (`flush out`, `acquire target`, the `damaged` cover trigger).

`0x004fc960(handle)` is the same sum for one attacker.

**Score scales.** K = 100 is the common pivot: a distance score is 100 at a reference radius. **[A4,
§12.9: K (`0x0137fa60`) and the 2500 below (`0x0137fa80`) are compiled literals, each set by its own
one-instruction startup stub — not `tweak_table.xtbl` entries.]**
- `0x004fa9f0` gives 100·d²/r², with r the tunable `0x013b8010`. **[A4, §12.1/§12.2: that tunable is
  `Ai_fov_length_low` (35 m) — named for the field-of-view system but, in this build, read only by this
  distance scorer, never by the FOV test itself.]**
- `0x004fbdb0` is piecewise: 0…100 inside an inner radius, then 100…2500 out to an outer radius
  (`0x0137fa80` = 2500).

So flag **`0x800`** ("score ≤ 100") on lists 5, 6, 7, 9 and 15 means **within the inner radius**. HIGH;
the radii are run-time tunables.

**Other lists** (producers CONFIRMED; meanings HIGH or HYPOTHESIS as marked):
- **4:** no producer found in the perception range; only removals. HIGH: unused in this build.
- **5:** neutral and friendly humans in range: `0x004ff760` (attitude 3) and `0x00505210` (attitude 4,
  the friend scan, which also does information sharing and the ally link of 10.6).
- **9:** nearby vehicles: the vehicle pass `0x00507440` walks the manager's `+0xb8` list and calls
  `0x005058e0`; flags `0x2000`/`0x800`. List 11 is also fed from here.
- **10:** a snapshot list rebuilt every 120–200 ms by `0x00507290`/`0x00505f10` over the manager's
  `+0xac` list; flags `0x1000`/`0x8000`/`0x800`; self-entries allowed. HYPOTHESIS: projectiles and
  thrown objects. Also fed by `0x00501d80` (from `0x004ec660` and `0x009286a0`). **[A4, §12.3: read in
  full — a "fire in the hole" shout producer gated by `Grenade_shout_radius` (25 m), fixed score 4.0,
  posting new event kinds 56/300.]**
- **11:** imminent threats, 1 s: `0x004fecf0` (from the list-9/10 passes) and `0x004fee30`, called by
  the sense pass for objects with `+0xe8` bit `0x100` within 20 units. The pass also posts **event 18**,
  and the new-entry hook posts **event 2**.
- **12:** `0x00502130` (from the witness routine and `0x004e7010`); not decoded. **[A4, §12.3: decoded
  — gated by `Pain_sound_radius` (8 m); "a nearby human is in distress"; says "call a doctor" when not
  in combat; forwards a now-dead source to list 7.]**
- **17, 18, 20:** written by cover and movement code, not by sensing: `0x005027a0` (from the
  cover-abandon check `0x0050a000` and the cover and combat-movement routines) writes 18 and 20;
  `0x00502650`/`0x00502710` (cover pop-out) write 20; `0x005028f0` writes 17. HYPOTHESIS: memory of
  recently used or abandoned cover objects.
- **19:** no producer found.
- **21:** `0x00500400`/`0x005004a0`, from `Follow` code (`0x00544200`, `0x00544350`, `0x005446b0`);
  flags `0x01000000`/`0x06000000`.

### 10.4 The entry flags

Flags are **per list**. The table gives the list-0 meanings unless another list is named, with every
setter and clearer found in the perception range (`0x004f8000`–`0x0050b000`). Bits `0x1`, `0x2` and
`0x4` survive an update (10.1).

| Bit | Set by | Cleared by | Meaning (confidence) |
|---|---|---|---|
| `0x1` | search pass `0x00503090` (when `+0x10` is also set and human `+0xb40` ≠ 0); dismiss `0x004fe980` | notice `0x00500f70` | **written off**: searched and given up, or dismissed (HIGH). `acquire target` refuses a head entry with `0x10`+`0x1`. |
| `0x2` | list 16: the damage record. List 0: no setter found in the range | — | list 0: keep re-noticing at the object's true position every 300–500 ms (reader CONFIRMED; setter external, OPEN) |
| `0x4` | no setter found in the range | — | **never expires** (expiry, 10.2; CONFIRMED reader) |
| `0x8` | notice `0x00500f70`; hearsay refresh `0x005034e0` | dismiss `0x004fe980` | **actively known hostile** (HIGH); `avoid brute`, `retreat`, `acquire target` require it |
| `0x10` | see `0x005013b0`; search pass `0x00503090` | notice; lost `0x004fd840`; dismiss; LOS reset `0x004fc820` | **the stored position has been verified**: either seen there, or looked at and found empty (HIGH). The list-2 probe skips an entry with `0x10` but without `0x40`. |
| `0x40` | see | lost; dismiss; LOS reset; `0x005029a0` | **in view now** (CONFIRMED) |
| `0x80` | non-visual notice (`0x00503900` and its callers); list-13 records | — (sticky) | **has been noticed by a non-visual stimulus** (HIGH). Part of the mask `0x182` tested by the view-cone test `0x004fef80`. |
| `0x100` | see; list 15 (`info+0x24` = 3) | — | **has been visually confirmed at least once** (HIGH); `frustrated` and the list-2 probe require it |
| `0x200` | non-visual notice | see | **current knowledge is non-visual** (HIGH) |
| `0x400` | see; lost | LOS reset `0x004fc820` | **line-of-sight state is valid**: answered since the last reset (HIGH). `acquire target` reads it (§9.6), and the list-2 probe requires it on the target's entry. |
| `0x800` | lists 5, 6, 7, 9, 15: score ≤ 100. List 2: obstruction within 0.2 of the probe. List 10: from `0x00505f10` | — | **close** (inner radius); list 2: **blocked right in front** (HIGH) |
| `0x10000` | the investigation candidate (§9.6) | notice; see | **excluded from the investigation query** (CONFIRMED: it is the query rows' exclude mask, 10.7) |
| `0x01000000` | list 2: obstructed | list 2: next clear probe | list 2: **line of fire blocked** (HIGH) |
| `0x02000000` | list 0: blocked line-of-sight answer, and the lost routine. List 2: clear (alternate probe) | list 0: see | list 0: **last LOS answer was "blocked"**; list 2: **clear line of fire** |
| `0x04000000` | list 2: clear | list 2: next probe | list 2: **clear line of fire** (HIGH). "Engageable" (§9.6) is a list-2 flag in `0x06000000`. |
| `0x08000000` | list 2: alternates per probe | list 2 | list 2: which of two probe variants was used (HIGH) |

`0x10000`, `0x01000000`/`0x02000000` on list 0 and the list-2 bits are the only bits at or above
`0x10000`. The dismiss pass `0x004fe980`, reached from `0x00512300`, runs for a human whose team byte is
not 0, 4 or 7 and that `0x0094f690` does not exclude. It forgets the players (`0x004fdb60`), marks every
list-0 entry written off (`0x1`, score set to the maximum, `0x8`/`0x10`/`0x40` cleared), empties list 1,
clears `+0x280`, and calls `0x004f67e0` and `0x004f6e10(8, 3)` on `+0x270`. HYPOTHESIS: a calm-down
after combat.

### 10.5 What reaches the event lists (part of §8 item 2)

CONFIRMED by listing of `0x004f5dd0` and `0x004f5ae0`.

- **The producer** is `0x004f5dd0(kind, source, recipient, delay, mode filter, sub-state filter,
  payload)`:
  - It takes a record from the global pool `0x01361998` and fills it: `+0x08` kind; `+0x10`/`+0x14`
    source handle; `+0x18`/`+0x1c` recipient handle; `+0x20` timer started now; `+0x24` timer set to
    `delay` (a negative delay becomes a random 250–750 ms); `+0x28`/`+0x29` the mode and sub-state
    filters; `+0x2a` bit 1 when a `0x20`-byte payload is copied to `+0x30`.
  - It appends the record to the recipient's **staging list human `+0x1c50`**. Players are never
    recipients.
  - Wrappers: `0x004f5f80` (random delay in a range), `0x004f5fd0` (re-post an existing record) and
    `0x004f6010`.
- **Delivery:** `0x004f5ae0(human)` runs once per frame for every human on the manager's `+0x1fc` list,
  before its update (§1.1). It moves every staged record to the live list **`+0x1c54`** and sets `+0x2a`
  bit 0. So an event posted in one frame is seen by the pump (§2.3) on the next frame at the earliest,
  and then waits out its own `+0x24` delay.
- **Kinds posted by perception** (mode filter `0x2d`, sub-state filter `0xb2` on most):
  - 0 — body found (list 7);
  - 2 — list-11 entry;
  - 11 — attack witnessed on a followed player;
  - 12 — attacked;
  - 18 — near an object with `+0xe8` bit `0x100`;
  - 71 — sighting lost (800 ms delay), and re-noticed;
  - 76 — gunfire;
  - 78 — target changed;
  - 86 — a human in mode 13 nearby;
  - 87 — reckless driver;
  - 100 — ally link made.

  Their names are the reacting handlers' business, not read here. Kind 108 (§8 item 15) is **not**
  posted from the perception range (no push of 108 there); its producer is still unknown.

### 10.6 Timer `+0x244`, handle `+0x250`, and the other small fields

- **`+0x244` (human `+0x754`) = time since the last clear line of fire to the target.** It is
  restarted only by the list-2 probe when the probe finds no obstruction (10.3), and is set to −1
  (unarmed) by the constructor `0x004fd930`. CONFIRMED.
  - It therefore makes sense in `acquire target` (§9.6): when the target has a list-2 entry, the goal
    fires once the human has had no clear shot for at least `Flush_Out/seen_in_last`.
  - `0x004fa330` is its elapsed-time reader.
- **`+0x250` (human `+0x760`) = the ally link.** CONFIRMED for the writers; the purpose is HYPOTHESIS.
  - **Set by** the friend scan `0x00505210(friend)` when:
    - the human is not in a vehicle (`+0xccc` = 0);
    - `0x004ff450(friend)` holds;
    - the friend is not being held, unless the human's team is 5 or 6.
  - At the same time the **friend's** `+0x248` (its human `+0x758`) is set to this human, and event 100
    is posted (300 ms delay) unless already queued.
  - **`0x004ff450` requires:**
    - the friend is not down, not in state `0x009a1dc0`, and not held (when `0x00943ed0` holds for the
      human);
    - the human's `+0xcc8` is not 14;
    - the perception byte `+0x292` is 4…7;
    - the target's last known position is available (`0x004fe390`);
    - the two are not in the same vehicle;
    - `0x0050cc40(human, friend, position, 0)`, not read. HYPOTHESIS: the friend stands in or near this
      human's line of fire to the target. **[A4, §12.8: read in full — CONFIRMED as a point-to-segment
      distance test (self to target) with a cylindrical tolerance around the friend, the same cylinder
      the unrelated `0x0050cdb0` aim-cone test (§9.6 `cover` rule 8) uses.]**
  - **Cleared** by the tick when `0x004ff450` stops holding; the friend's back-link is cleared too if
    it still points here, and timer `+0x218` is restarted. Also cleared by the full reset.
  - `flush out` refuses while the link is live (§9.6).
- **`+0x248`** = the reverse link (who linked to me), read by `0x004fe8a0`.
- **Information sharing** (friend scan, CONFIRMED):
  - for a player-class friend, its target (`+0x1e30`) is put into list 6;
  - otherwise, when the friend's own perception threat handle (`+0x280`, human `+0x790`) is one this
    human has not seen within `Flush_Out/seen_in_last`, and the friend's current action is not 0, that
    threat is put into list 6.
  - Friend counts go to `+0x25c`/`+0x25d`.

### 10.7 The query `0x004fcce0`, and the investigation candidate

CONFIRMED by listing of `0x004fcce0`, `0x004fb040` and the table builder in `0x00550480`.

- `0x004fcce0(rows, row count, handle)` returns the first match of `0x004fb040` with a result limit of
  one, or null. Each `0x20`-byte row is: `+0x00` list number (≤ 22; 22 would read the `+0x3a0`
  deferred-free list's slot, a latent off-by-one that no caller exercises); `+0x08` a handle;
  `+0x10` **maximum age** in ms (ignored when negative); `+0x14` "all of" mask; `+0x18` "any of" mask
  (ignored when 0); `+0x1c` "none of" mask.
- With a non-null handle argument, or a row handle, only that entity's entry in the row's list is
  tested. Otherwise each list is walked from its head, and the first entry passing all four tests wins.
  So the rows are a priority order, and within a row the list's own order decides.
- **The investigation candidate's rows** (`0x013bb248`, built once): lists 1, 0, 15, 16, 14, 13, 7 in
  that order (the last row dropped for a follower). Each row has a null handle, maximum age =
  `Investigate/seen_in_last`, "all of" = 0, "any of" = 0, "none of" = `0x10000`.
- **This settles §9.6's open point.** `0x004fcce0` **does** use the latched `Investigate/seen_in_last` as
  a maximum age. Because the rows are built once per process (§9.6 quirk), the effective window is the
  first caller's value; the `investigate` goal then re-tests its own value, so a character effectively
  gets the smaller of the two.
- `0x004fb040` is also called by `0x004fcd20` (the sense pass's follower branch) and `0x00550740` with
  other row sets, not read.

### 10.8 Corrections and port notes

1. **[corrects §9.8]** 22 lists, not 17; entries `0x34` bytes, doubly linked (`+0x30` previous); the
   "period" from `0x004f8cd0` is the list lifetime (a linear fade, not a half-life).
2. **[corrects §9.8 "list 0 is currently perceived"]** List 0 is the **set of known hostiles**
   (awareness). It never expires by age; an entity leaves it when it vanishes, dies, or is removed.
   "Currently perceived" is flag `0x40` within it.
3. **[refines §9.6]** "Seen within W" on list 0 measures the time since the entity was last noticed by
   any channel (sight, sound, damage, hearsay), not since it was last seen. Only list 1 and flag `0x40`
   are sight-specific. The timestamp is restarted on every notice.
4. **Port note.** Every producer is gated on local ownership (`0x008addb0`), and most on the activity
   level `+0x260`. A proxy's lists are not filled at all.
5. **Port note.** The provocation meter `+0x200` and the attitude function together decide whether a
   player is a list-0 hostile at all. With force flag `never_turn_on_player` set, no player can ever
   enter list 0.

---

## 11. The action routines (unit A3)

**Scope.** This section reads the 70 action routines of the action table `0x012e1c60` (column `+0x08`,
addresses in §9.5). The chooser calls each one last, as the final feasibility test, and it also builds
the block that gets committed (§9.4 step 6). This section answers §8 item 11. It also settles the
sub-items of §8 item 13 that these routines call.
**Method.** As for A1/A2:
- a private disposable Ghidra project copy (`tools\gp_aiA3`, deleted after the dumps), with `func`
  dumps of all 65 distinct routine bodies and of 60 helpers;
- a whole-program instruction scan of the executable with the `capstone` package, used to find every
  reader of the behaviour-record fields (11.5) and every writer of the cover timers (11.6);
- byte-level reads of float constants with `tools/harnesses/exe_va.py`.

The shipped `ai_goals.xtbl`, `ai_behavior.xtbl` and `combat_actions.xtbl` were extracted from
`misc_tables.vpp_pc` with `tools/harnesses/vpp_modea.py` and read as data. A scan of all 39 `.vpp_pc`
archives in the install found each of the three tables in `misc_tables.vpp_pc` only, so that is the
copy the game loads.

**Conventions in this section.**
- **T** is the live target returned by `0x004e4e50` (§9.8).
- **P** is T's estimated position from the last-known-position query `0x004fe390` (11.6).
- "**Visible**" is `0x004fdac0` (11.6).
- "**Engageable**" is `0x004ffc70` (§9.6).
- A **back-off** is a call to `0x004f4ae0(human, action, ms)`, which arms that action's repeat timer
  `+0x320 + 4·a` (11.6), so the chooser skips the action for that long.
- Constants are quoted as the values read from the image. A radius compared against a squared distance
  is quoted as the radius.

### 11.1 The two null routines against the shipped data (§8 item 11, last sentence)

CONFIRMED by reading the shipped tables.

- **No goal list names 24 `move scripted` or 49 `vehicle enter scpt`.** The 17 `ai_goal` rows of the
  shipped `ai_goals.xtbl` name 142 actions in all (list lengths 1 to 24, as `spec-tables-traffic-ai.md`
  §26.3 already counted). Every name matches the engine's action-name table, and neither of the two
  null-routine actions appears. Neither string occurs anywhere in the file.
- **No behaviour enables them either.** None of the 44 `Behavior` rows of `ai_behavior.xtbl` lists
  either action in its `Actions`. So the ability-enabled byte (record `+0x26 + 6·a`) is 0 for both in
  every record, and the chooser would refuse them before it reached the routine call even if a list
  named them.
- **`combat_actions.xtbl` has no row for 49** (nor for 48 `vehicle passenger` and 62 `avatar bullrush`).
  Its 67 rows inside `<Table>` are exactly the other 67 action names (as `spec-tables-weapons-combat.md`
  counted). So action 49's `pre_flags` stays 0, and the precondition test (§9.4) refuses it in every
  state. Action 24 does have a row (`on foot`, `in water`), so its safety rests only on the two data
  facts above.

So the hazard of §9.5 is **latent**: the shipped data never lets the goal loop reach either null
routine. A port that loads modified tables should still treat 24 and 49 as unchoosable by goals (§9.5).
For action 24, one goal-list entry plus one `ability_elm` would be enough to make the goal loop call
address 0.

Two side results from the same data read:

- **Actions that no goal list names:** 5 `throw weapon`, 24, 48, 49 and 62.
  - 5 is enabled in some behaviours but can be reached only as an order.
  - 48 is reached only through the chooser's own fallback (§9.4 step 7), which bypasses the
    precondition test, so its missing `combat_actions.xtbl` row does not matter.
  - 62 has no row, no goal and no behaviour, so it is reachable only as an order.
- A 68th `actions` element, named `rappel`, sits in the file's `<TableTemplates>` editor sibling after
  `</Table>`, not in the table. It names no action of this build. It is noted only so that a raw count
  of `actions` elements (68) is not mistaken for the loaded row count (67).

### 11.2 The calling contract, and the committed block as the routines write it

CONFIRMED by listing of all 65 distinct bodies.

- **Signature.** Every routine is called as `(human, block)` (cdecl) and returns a byte, true for
  accept. 30 of the 65 distinct bodies write into the block (31 actions, counting 20, which shares
  19's body). The rest read only the human. They are pure gates whose mode builds its own plan when it
  starts.
- **Not pure.** Eight routines have side effects even when they refuse, so a port must call them only
  where the original does (the chooser, and the re-assert path of §9.4 step 3):
  - back-offs on refusal: 26, 45, 46 and 51;
  - 50 backs itself off by 1000 ms **before** it tests anything, so every evaluation that reaches it
    costs a 1 s cool-down whatever the outcome;
  - 61 backs off a *different* action, 58;
  - 42 and 43 write a navmesh-snapped position back into the perception entry they inspect, and clear
    that entry's flag `0x10` when the snap moved it more than 0.25 on any axis (`0x01119210`).
- **Block fields written** (block layout of §9.2 step 5; "pos" = a 3-float position):

| Offset | Written by (action ids) | Content |
|---|---|---|
| `+0x08`/`+0x0c` | 11, 16, 31, 33, 37, 38, 39, 41, 42, 43, 45, 46, 51, 52, 63, 64 | handle of the object the action is about: the cover object (11), the most threatening hostile (16), an ally (31, 33), a vehicle (37, 38, 39, 52), the investigated entity (41–43), a pickup (45), the victim (46, 63), a prop (51), a corpse (64) |
| `+0x10` | — (cover code `0x00541600` and `0x005403c0` arm it in the committed copy, human `+0x460`) | a timer (HIGH) |
| `+0x14` | 67 (armed with 0) | a timer |
| `+0x18` pos | 16, 18, 19/20, 21, 22, 26, 27, 28, 29, 41, 42 | **the destination** |
| `+0x24` pos | 5, 6, 7, 11, 15, 37, 38 | a second point: the target or strike point (5, 6, 7), a point to move away from (15), the vehicle entry point (37, 38), a cover point (11) |
| `+0x30` pos | 5, 11, 15 | a third vector: launch data (5), T's position (15), cover geometry (11) |
| `+0x3c` byte | 37, 38 (seat), 39 (cleared), 41–43 (perception list number), 67 (cleared, with `+0x3d`) | small index |
| `+0x40` dword | 11, 26 | a value from `0x005422a0` (11) or the sidestep variant 1–3 (26) |
| `+0x48`/`+0x4c` | 43 | a second handle: the cover object to peek from |
| `+0x50` | 39 (cleared) | — |
| `+0x55` bit 1 | 43 | which side to peek from |
| `+0x56` | 39 sets bit 5 | flags. Bit 6 (ordered) and bit 7 (forced re-check) are set by the chooser, §9.4 step 3; 13 reads bit 7 |

  Throw grenade (4) fills its trajectory through `0x0054d930(solution, block, point)`, not decoded
  field by field.

### 11.3 The families

Each routine is CONFIRMED by its own listing. Distances are in world units. The shared preamble of each
family is given once, followed by what each member adds.

**A. Shared stubs (4 actions).** 9 `cover take off` and 14 `hold position` always accept (§9.5).
- **58 `avatar stomp`'s routine `0x006948c0` is a shared "return 0" stub.** It is two instructions and
  has dozens of unrelated callers (identical-code folding).
  - So 58, although it is listed in `kill enemy`, is **never chosen by a goal**. It can run only as an
    external order: the re-assert path calls the routine only when `+0x44c` bit 4 is set (§9.4 step 3).
  - **[corrects §9.5]**, which listed it as an ordinary routine.
- 20 `avatar chase`'s routine `0x0053adc0` is a one-instruction jump to 19 `move chase`'s `0x0053a7f0`,
  so the two behave identically.

**B. Movement to a computed destination (`+0x18`).** All except 27 and 28 need T and a P (`0x004fe390`
with the "true position when in view" flag).
- **19/20 `move chase`** (`0x0053a7f0`): refused while T has a perception-list-18 entry younger than
  1500 ms (`0x004fae50(18, T)`); `+0x18` = P. **66 `killbane chase`** (`0x00538ca0`) is the same test
  with a 500 ms window and **writes nothing**.
- **18 `move advance`** (`0x0053a520`):
  - refused for a member of a player-led group (`0x0097e7b0`, then the player class test);
  - with E = engageable, A = T already inside the maximum weapon range (`0x004df080` ≤ 0, 11.6) and
    B = `+0xccd` = 9, it refuses when:
    - for a personality with `combat_aggression` = 0 (`0x00507ea0`): E, A or B;
    - for one with 2 (`0x00507ee0`): E and A;
    - otherwise: E and (A or B);
  - `+0x18` = P.
- **22 `move rush`** (`0x00539d90`):
  - needs no list-18 entry for T younger than 500 ms (`0x004de3f0`), T's `+0xe8` bit `0x100` clear,
    and `0x004e91c0` to succeed;
  - unless 22 is already the current action, it is refused while T is closer than the larger of the
    held weapon's range (info `+0x120`, else b of §9.8) and 10.0 (`0x01118d20`);
  - `+0x18` = P.
- **29 `move close in`** (`0x0053f520`): not for a follower (`0x0097e280`); T engageable **and already
  inside the weapon band** (`0x004df080` = 0).
  - `+0x18` = a point a random 2.0–3.0 (`0x01187ebc`/`0x01116444`) toward T.
  - For a riot-shield holder (`0x009ba300`) that finds, through list 5, another shield holder on the
    same T within 4.0 and roughly ahead: instead a point beside that holder, offset by 1.3 × its side
    vector (`+0x4c`), so the shields form a line.
- **21 `move surround`** (`0x0053c8e0`): a flanking point around T's reference position
  (`0x004e4360`), snapped by `0x00539200` into `+0x18`.
  - With a group (`0x005083f0`), the side is chosen away from the other members.
  - Without one, the side is chosen at random (51%) when `+0xd1c` ≤ 0.3, else from the sign of a cross
    product with the velocity `+0xd04`.
- **16 `move retreat`** (`0x0053d540`):
  - refused when the human's post area (`0x004e9e90`, 11.6) has a radius below 6.0;
  - walks list 0 for entries with flag `0x8` seen within 3000 ms, skipping entities on another level
    (`0x004de500`, 11.6). **It reads the behaviour record's `avoid brute` goal byte (`+0x1c7`)**: when
    that goal is enabled, only brutes (`0x0052a230`) count as threats;
  - each threat contributes when it is inside its threat radius (`0x00538200`: 10.0 for an unarmed
    brute, 15.0 for the `0x0052a2e0` brute, else its maximum weapon range b);
  - `+0x08` = the threat with the largest radius;
  - `+0x18` = a navmesh point (`0x00539200`) on the far side of the radius-weighted centroid. It must
    end at least 3.5 (`0x01118d48`) farther from the centroid than the human is now. **This snap always
    fails in the shipped game (§14); the action's own decline exit is taken every time it is evaluated,
    and the radius/centroid logic above it is unreachable in retail.**
- **26 `move sidestep`** (`0x0053e870`; the only reader of `dodge_chance`, 11.5):
  - not in a vehicle state (`+0xccc`), not `0x00985cd0`;
  - T exists; `0x004fdae0`; **not** at the human's own cover point (`0x00540a70`, 11.6);
  - T lies within 45° of the facing vector `+0x64` (cosine ≥ 0.7071, `0x01115258`);
  - if T is engageable, a percentage roll against `dodge_chance` (`0x004dd950`) must succeed, and then
    either T is aiming at the human (`0x0050cdb0`) or list 3 ("shots at me") is non-empty. Otherwise
    back-off 1000 ms;
  - then up to three lateral candidates are probed. The first clear one gives `+0x18` and `+0x40`
    (1, 2 or 3). If none is clear: back-off 5000 ms.
- **27 `move to post combat`** / **28 `move to post idle`** (`0x0053b080` / `0x0053b210`):
  - not for a member of a player-led group;
  - 27: human `+0x315` bit 6 set, or else `0x004e50e0` false (§9.6 `group`), bit 5 set and the
    `acquire target` goal test (`0x004f45a0`) true;
  - 28: bit 6 set or `0x004e50e0` false, and any of bits 4–6 set (mask `0x70`); refused while
    `in_scripted_cover`;
  - the post radius (AI sub-object `+0x40`, human `+0x2f0`) must be ≥ 0, and the post position
    (`0x004e7d10`) must be set;
  - 27 is refused when the human is already inside the post (`0x004efd60`, with the radius raised to
    1.0 when below 1.0);
  - `+0x18` = the post position.
- **41 `investigate move`** (`0x00550990`): the investigation candidate (`0x00550480`, §9.6) must not
  carry flag `0x1` (written off).
  - A follower whose player is on foot also needs that player to have a target (`0x00963540` on the
    player's `+0x1330`).
  - `+0x18` = the entry's position, `+0x08` = its handle, `+0x3c` = its list number.

**C. Approach a hostile for a strike, or move relative to one.**
- **6 `melee`** (`0x00551e20`):
  - `0x00551d90` (can melee);
  - T inside the melee band (`0x004df080` with the melee-range flag);
  - T neither in a vehicle (`0x009b9160`, `0x009b91e0`) nor `0x0095d740`, `0x009a1dc0`, `0x0098a3f0`;
  - brute cases are checked separately, and a riot-shield rule like 23's applies (`0x00971cf0`,
    `0x009ba300`, a timer test);
  - `+0x24` = the strike point.
- **7 `melee vehicle`** (`0x005525c0`): `0x00551d90`; T in a vehicle. The human must be within the melee
  range of the vehicle's bounding box, measured in the vehicle's own frame (`0x004aac40`, `0x004f4770`,
  then the point-to-box distance `0x004f2970`). `+0x24` = the vehicle's position.
- **23 `move rush to melee`** (`0x0053de30`):
  - the same preamble as 22, and T not in a vehicle;
  - the distance must lie between the melee reach (at least 3.0) and the second range;
  - riot shield: if T is a player (`0x00971cf0`) whose `+0x277c` timer is armed and running, refuse;
    refuse also when another shield holder in list 5 is already doing 23 on the same T, so there is
    **one shield rusher per target**;
  - it accepts when the point from `0x004e91c0` is a valid (non-NaN) position. **It writes nothing**:
    the mode finds the point again.
- **15 `melee stand back`** (`0x0053d140`):
  - T within a run-time radius (`0x013bad60`, latched);
  - a human holding a ranged weapon needs `+0xf4` → `+0xe8` bit `0x40`;
  - if T is in the state `0x009826e0` (`+0xcc8` = 12 `Fire spray` with `+0xcce` 20 or 21 — CONFIRMED by
    listing, §13.1.6b): `+0x24` = the position of the object linked at T's own `+0xda8`/`+0xdac` (a handle
    pair, resolved through the generic handle resolver `0x004d7f30` then its own `+0x40` position
    sub-vector; CONFIRMED, §13.1.6a), or T's position plus its facing if the link does not resolve;
    `+0x30` = T's position;
  - otherwise it walks list 5 for an ally on the same T, or a player within `0x013bad40` who is facing
    T, and uses that one's position as `+0x24`.
- **67 `killbane strafe`** (`0x00538d40`): T at least 2.5 away (6.25 squared, `0x01115350`). It arms
  block `+0x14` with 0 and clears `+0x3c`/`+0x3d`.

**D. Shooting (no block writes; the `Fire spray` mode aims for itself).**

"Can fire" is `0x0050c890(human, any)`:
- not `+0x317` bit 2;
- a held weapon whose refire timer (item `+0x1c4`) has passed, unless `any`;
- usable from the current vehicle seat (`0x00b869b0`);
- a non-zero loaded count (item `+0x1a4`).

`0x0050c930(human, ms)` is the same test, except that it allows the refire timer to have up to `ms`
left.
- **0 `fire`** (`0x005436f0`): `0x0050c930(750)`; T engageable.
- **1 `fire sweep`** (`0x00543a20`): can fire; then either:
  - `0x005e6ce0` holds (a player-class human in state `+0xcc8` 16, or 12 with `+0x15c5` bit 2, or one
    non-player character type `0x0149faa4`), and T is *not* out of range (`0x004e68e0` ≠ 1); or
  - a brute with T in range, T not in a vehicle, a clip fraction ≥ 0.9, and at least three list-0
    hostiles (flag `0x8`) inside the sweep arc.
- **2 `fire suppress`** (`0x005439b0`): can fire; at the human's own cover point (radius `0x024fb1d0`);
  no ally link (`+0x250`, §10.6); T engageable.
- **3 `fire chaos`** (`0x005435b0`): can fire; T exists and is **not** visible (blind fire).

**E. Cover (`Cover stay` / `Cover popout`).** These routines use the cover timers whose owners are given
in 11.6: `+0x964` find-new-cover, `+0x968` stay-down, `+0x96c` time-in-cover.
- **8 `cover take def`** (`0x00540b90`):
  - refused while `+0x968` is armed and less than `pop_up_time` has elapsed on it;
  - accepted once `+0x964` has passed (time to find new cover), or when there is no cover object;
  - otherwise accepted only when the human is **not** yet within `0x024fb1c0` of its cover point;
  - no block writes. The mode picks the cover.
- **10 `cover advance`** (`0x00540080`): elapsed time on `+0x96c` ≥ `advance_cover_time`.
- **11 `cover stay down`** (`0x00543180`):
  - its own ability-enabled byte (record `+0x68` = `+0x22 + 6·11 + 4`) is re-checked, redundant with
    the chooser;
  - `+0xe3c` ≤ 1.0;
  - at the cover point (radius `0x024fb1c0` when `+0xcce` = 16, else `0x024fb1d0`);
  - unless `in_scripted_cover`, the cover evaluator `0x00509e50` must approve the cover object;
  - it fills `+0x24`/`+0x30`/`+0x40` from `0x005422a0` (skipped while animation `0x1d92` plays) and
    `+0x08` = the cover handle.
- **12 `cover fire`** (`0x00540f30`):
  - `0x004fdae0`; T visible and engageable; at the cover point;
  - neither of animations `0x1d92`/`0x1d93` playing (`0x004b4ce0` on `+0xd24` → `+0x10`);
  - `0x008cd1d0(cover object, T, T's position)` = 0. HYPOTHESIS: the cover does not shield the human
    from T's current position.
- **13 `cover popout`** (`0x00541230`):
  - unless block `+0x56` bit 7 is set, refused while the `cover` goal test (`0x004f4120`) holds;
  - at the cover point; `0x0050c930(1000)`;
  - unless bit 7, refused when `cover fire`'s routine would accept (popout is the fallback when the
    human cannot fire from cover);
  - refused on `+0x76f` bit 5;
  - needs the cover object's `+0x8c` bit 1 or `0x00985cd0`.

**F. Picking an object to act on (`+0x08`).**
- **45 `pickup weapon`** (`0x004f0b70`):
  - refused by `cant_pickup_weapons` (`+0x2ba` bit 7);
  - up to eight pickups within 225 that pass `0x004f0810` (CONFIRMED clean/live — does not touch the dead registry below) are navmesh-snapped via `0x00a12180`;
  - one multi-target path query (`0x00a12a80`, path limit 22.5) chooses the reachable one → `+0x08`;
  - none: back-off 700 ms.
  - ~~**CORRECTION, 2026-10-08 (§14.7):** statically, "none" is the unconditional outcome... STATICALLY DEAD; RUNTIME CONFIRMATION PENDING.**~~ **LIVE-REFUTED, 2026-10-08 (§14.7a):** the live hook check confirmed `0x00447770` (the gate both `0x00a12180` here and `0x00a12a80` depend on) succeeds 99.25% of the time in real gameplay (87,262/87,918 calls), and `pickup_weapon_attempted` fired 90 times in one captured gunfight — the "always declines" claim does not hold. Action 45's real success rate in retail is not yet separately measured (the capture logged attempts, not per-attempt outcomes for this specific action), but the upstream "always dead" premise is refuted. See §14.7a; implement as potentially-working, not as always-declines, pending the real-writer follow-up.
- **46 `human shield grab`** (`0x0050dfe0`):
  - `0x009aa520` (can grab);
  - **a follower must be within `seperated_distance` of its player** (`_interior` when `+0x317` bit 0
    is set; the only use of that field outside `Group`/`Follow` code);
  - the threat (`0x004e6bb0`) is not in a vehicle and was seen within 10 s (`0x004fc760`);
  - it first tries the entity of its reference entry (`0x004e4360`), skipping it when the followed
    player is aiming at it (`0x0050cdb0`). Otherwise it scans the manager's `+0x1fc` human list for the
    best grabbable human in front (`0x009a9830`, `0x00950550`);
  - `+0x08` = the victim; none: back-off a random 300–500 ms.
- **51 `brute throw prop`** (`0x00525600`):
  - T exists and is not `0x0060b530`;
  - if T is held by someone (`0x009aa360`, 11.6), the human must be hostile to the holder
    (attitude < 3, `0x004fe0f0`);
  - a spatial query (`0x0075d4b0` with the filter callback `0x00525330`) finds a prop → `+0x08`. The
    routine also consults the post area (`0x004e9e90`);
  - none: back-off 1000 ms.
- **52 `brute car flip`** (`0x00526240`):
  - T in a vehicle that resolves;
  - a game-mode switch (`0x00614d00`, a byte jump table) — **resolved 2026-10-07: for game-mode values
    0, 1, 4 or 11 the routine declines outright, skipping every check below; any other mode falls
    through to the checks that follow**;
  - **the vehicle's class is none of: Helicopter or VTOL (`0x00ad31a0`, `Vehicle_Type` `+0x2c` == 3 or
    4), Motorcycle (`0x00ad3270`, `+0x2c` == 1), Watercraft (`0x00ad3290`, `+0x2c` == 5), or Tank
    (`0x00ad3140`, a separate special-type sub-field `+0x4c4` == 14)** — **corrected 2026-10-07: these
    are vehicle-*class* exclusions, not transient-state exclusions**;
  - vehicle `+0x5c` ≥ 0.1;
  - T visible (`0x004fc6d0` on the acting human's own target handle);
  - a clearance probe around the vehicle must be clear (the same vtable `+0x60` box, clamped against
    the transformed human position, then run through a sibling of `0x004f2970`'s formula,
    `0x004fa670`, against a fixed `(2,2,2,2)` constant — the exact intent of that last comparison is
    OPEN);
  - `+0x08` = the vehicle.
- **63 `quick kill`** (`0x0052cbe0`, `Commando`): the first in-view (flag `0x40`) list-0 entry that
  passes `0x004fe730` and `0x00963d40`, has no riot shield, and whose position predicted from its
  velocity (`+0xd04`) is within 10.0 (100 squared, `0x0111645c`) → `+0x08`.
- **64 `zombie eat`** (`0x00537b90`):
  - not `0x006e44f0()`;
  - health fraction (`+0x1cb8` / `+0x1cac`) ≤ 0.9;
  - the `0x0097cdd0` / `+0x1c9a` gate;
  - not `+0x1c98` bit 17;
  - a corpse from `0x00537910` → `+0x08`.
- **33 `follow acquire`** (`0x005479e0`): no leash radius set (negative `+0x2f0`; see 17). It looks in the group (`0x005083f0`, iterated
  with `0x00508150`/`0x005081f0`), then in list 5, for an ally the human perceives (`0x004fc650`) who
  is in combat (`0x00943a20`) or whose current mode `+0x501` is 12 `Fire spray`, 23 `Melee` or 34
  `Stunned` → `+0x08`.
- **31 `follow avoid LOF`** (`0x005453c0`): accepts when an event of kind 5 addressed to the human is
  queued (`0x004f6310`, §10.5), and copies the **current** committed block's handle (human `+0x458`)
  into `+0x08`.
- **37 `follow to car`** (`0x005487e0`):
  - `0x00548590`; refused by `always_follow_in_alternate_vehicle` (`+0x2ba` bit 0);
  - the group leader's vehicle within 30 (900 squared), and not more than 5.0 above the human when
    `0x00ad4810`;
  - a seat from `0x005457f0(human, vehicle, &block+0x3c)` → `+0x3c`. **CONFIRMED — full listing,
    387 insns:** `0x005457f0` takes a raw output-byte pointer as its 3rd argument and writes the
    winning seat index through it directly — it never computes a `+0x3c` offset itself, so the caller
    must already be passing `&block+0x3c` here, not `&block`. On failure (no eligible seat) it returns
    false and leaves that byte **untouched**. Selection, when more than one seat is eligible via
    `0x00b0eb90` (`spec-player-runtime.md` §13.5, called here as
    `0x00b0eb90(vehicle, seat, -1, human, 0, 1, 0, 0)` — resolving that spec's own "not checked" flag
    for this exact call site), is **closest-to-self by squared distance** (human world position vs.
    each candidate seat's `0x00b0eac0`-resolved, untransformed local offset), with two hardcoded
    priority overrides: the driver seat (0) always wins outright if eligible; the front-passenger seat
    (1) also wins outright under a narrower group/difficulty-gated condition;
  - the seat's entry point (`0x00b0eac0(vehicle, seat, -1, 0, 0, &out1, &out2)`), transformed to world
    space (`0x004e44b0`, `0x004f4770`) → `+0x24`. **CONFIRMED — full listing:** `out1` = the seat's
    local-space entry position (sourced from `vehicle_interaction_point_sets.xtbl`'s
    `Interaction_Point_Set_Element.Seat_Offset`), `out2` = a local-space 3×3 rotation matrix (from that
    same element's `Heading` field) — a position+orientation pair, vehicle-local, matching this unit's
    own cited world-space transform step exactly;
  - the vehicle → `+0x08`.
- **38 `follow other car`** (`0x00548e80`): the same, for a group member's vehicle that is not the
  leader's. A member in `Hijack` (18) offers its own committed block's handle (`+0x458`); a member in
  `Passenger` (25) or `Veh trans` (41) offers its vehicle. The vehicle must also not be `0x00ad4810`.
  **Shares `0x005457f0`/`0x00b0eac0` with action 37 verbatim — no separate seat/entry-point logic
  exists for 38.** `0x005457f0` itself (not action 38's own top-level logic) additionally walks the
  human's own group a second time internally, checking each OTHER member's mode byte (`+0x501`)
  against `0xE` (14) and, on a match, that member's own `+0x458`/`+0x45c` committed-block handle
  against the target vehicle — the same field pair this entry already names for `Hijack`, but tested
  against a different mode constant; not reconciled with 18/25/41 above, flagged open.
- **39 `follow carjack`** (`0x00549080`): `0x00548590`; resolves the GROUP LEADER (`0x0097e7b0`, the
  same leader concept 37/38 use). If the leader has a cached vehicle handle (`+0x16c0`/`+0x16c4`) that
  resolves, clears a per-vehicle disqualifying flag (`+0x33` bit `0x10`), passes the per-type carjack
  flag (`0x02cc9900[type]+6` bit `0x80`) and `0x00853b10`'s generic validity gate: that cached
  vehicle's own class (`0x00ad2f80` — the vehicle's `+0xbf4`→`+0x2c` field; `0x00ad2f80` itself is an
  8-byte getter, NOT a jump table) selects ONE of three candidate-search functions via a 6-entry (0…5)
  jump table that lives IN THIS FUNCTION at `0x00549170`: class 5 → `0x0051cc80`; class 2/3/4 →
  `0x0051cd70`; class 0/1 → `0x0051ce60`. If the leader has no such cached/valid vehicle, falls back
  instead to a simple boolean gate on the leader (`0x009b5690`, leader `+0x1858` ≠ 0) choosing between
  `0x0051cc80` (true) and `0x0051ce60` (false) — `0x0051cd70` is reachable ONLY via the class-based
  path. All three search functions walk the global vehicle list (head `0x027c2eac`, next `+0xbec`),
  independently re-filtering every candidate by the SAME class grouping and picking the nearest by
  squared distance (`0x00da1330`), subject to a shared gate (`+0x3a` bit `0x80`, `+0x3b` bit `0x1`,
  `0x00853b10`, `+0x16c0` bits `0x80`/`0x200`, `0x00a77e10(this)→+0x1564 > 0`, `+0xc6d` bit `0x2`,
  `+0x1d7b` bit `0x40`, `0x00ad5180`); `0x0051ce60` additionally runs a second pass over a second list
  (head `0x027c2eb8`, same class 0/1 filter, an extra `+0xc68` byte gate (0 or 8), and an
  ellipsoid-weighted distance), returning whichever of its two passes found the closer candidate.
  Result → `+0x08`; it also sets `+0x56` bit 5 and clears `+0x3c` and `+0x50`. **[CONFIRMED — full
  disassembly of `0x00549080`, `0x0051cc80`, `0x0051cd70`, `0x0051ce60`, `0x0097e7b0`, `0x009b5690`,
  `0x00853b10`, `0x00ad5180` (shallow on its own two sub-calls), `0x00a77e10`, `0x00da1330`; the exact
  field identities of `+0x1564` (0x00a77e10's target), `+0x1858` (0x009b5690's target) and the second
  list `0x027c2eb8`'s specific population are OPEN.]**
- **42 `investigate look`** (`0x00550c40`):
  - the candidate's position is navmesh-snapped and written back into the entry (11.2);
  - it needs the candidate's flag `0x10` (position verified);
  - unless human `+0x4a5` bit 4 is set, it writes `+0x08`, `+0x3c` and `+0x18`.
- **43 `investigate peek`** (`0x00551280`):
  - `0x0098da50`; the current action must be 41 or 42, so a peek only follows another investigate
    action;
  - snap as in 42;
  - a nearby cover object (`0x008d1d80`) → `+0x48`, and the peek side → `+0x55` bit 1;
  - `+0x08`/`+0x3c` from the candidate.

**G. Pure gates on the human, the target or the group (no block writes).**
- **69 `idle`** (`0x0050eff0`): the acceptance test accepts mode 0 `Idle` (`0x004de110`, §3.4); not a
  follower (`0x0097e280`); not in states `0x009adb00` (`+0xcc8` = 14) or `0x009a5d60` (`+0xcc8` = 18
  with `+0xcce` = 35).
- **25 `move to navmesh`** (`0x00538270`): human `+0xc38` bit 7. HIGH: "off the navmesh".
- **17 `move regroup`** (`0x00538700`; one of the two `seperated_distance` readers among the routines):
  - L = the group leader (`0x0097e7b0`);
  - when the human's leash anchor (AI sub-object `+0x28`, human `+0x2d8`) is not L and a leash radius
    (`+0x2f0`) is set, refuse;
  - limit = `seperated_distance` (or `_interior` when human `+0x317` bit 0 is set), plus 10.0 when T
    is an unarmed brute (`0x0052a240`), lowered to the leash radius when that is smaller;
  - accept when the human is farther than the limit from L; when within it, accept only if L is on
    another level (`0x004de500` ≠ 0).
- **32 `follow formation`** (`0x00547890`) and **34 `follow player`** (`0x00547f20`):
  - a leader (32) or the followed player (34);
  - not a human-typed human with `+0x1e01` bit 0;
  - the same leash test as 17;
  - 32 also needs the leader out of any vehicle and the human not `+0x76f` bit 5;
  - 32 accepts when farther than 2.5 from the leader; 34 when farther than a run-time radius
    (`0x013bae40`);
  - when closer, each accepts only while the leader is in a vehicle state (`+0xccc`).
- **35 `follow teleport`** (`0x00548020`):
  - the followed player exists; no leash;
  - the human is not held (`0x009aa360`), not `0x0095d740`, and `0x0091f310(2)` is false;
  - `0x004d7e00` resolves without `+0x1e01` bit 0;
  - accepts when beyond 100 (150 when the player is in a vehicle) **and** more than 4.0 apart in
    height;
  - refuses when within 100 and less than 3.0 apart in height;
  - otherwise accepts only when `0x009b5690` holds for the human and not for the player.
- **30 `follow make way`** (`0x00545f20`): the followed player is in a vehicle state, not `0x009a1dc0`,
  and within a run-time radius (`0x013bb1c0`).
- **36 `follow lemming`** (`0x00548180`):
  - the leader is a player; the global `0x013bb198` is non-zero;
  - human `+0x317` bit 1, or the leader is off the navmesh (`+0xc38` bit 7);
  - not `0x009ba8b0`.
- **40 `follow browse`** (`0x00545450`): no T, and an event of kind 97 addressed to the human is queued.
- **48 `vehicle passenger`** (`0x0051db40`): in a vehicle (§9.5).
- **50 `vehicle extract`** (`0x0051d2f0`):
  - after its unconditional back-off (11.2), it needs T's level from `0x00af1e50` to reach
    `0x013ba5b4`, with a linear chance ramp up to `0x013ba5b0` (HYPOTHESIS: a notoriety level);
  - T's vehicle passes flag and state checks;
  - a 3.0 door-clearance box;
  - T within 400 horizontally and 3.0 vertically (`0x004efd60`);
  - T inside the human's post area (`0x004e9e90`);
  - no group member already doing 50 on the same T.
- **44 `reload`** (`0x00552730`): `0x009c05b0(human, 0)` (11.6). Refused only when `0x0098da50` holds
  and the human is at its own cover point (radius `0x024fb1c0`).
- **47 `taunt`** (`0x00552b90`): list 6 plus list 0 hold at least one entry (`0x004faf90`).
- **59 `avatar taunt`** (`0x00524640`): T exists and is in state `0x009a1dc0` (`+0xcd2` not 0 or 6).
- **68 `roller blader change`** (`0x005303c0`): descriptor (`+0xf0`) byte `+0x48` bit 6.
- **65 `zombie explode`** (`0x00537650`): `+0x1c98` bit 17 set; neither the human nor T is
  `0x0060b530`; T within 0.75 × a run-time radius (`0x013bacc4` → `+0x28`).
- **Brute and avatar gates** (all need T):
  - 62 `avatar bullrush` (`0x00524670`): T visible;
  - 57/60 `avatar shockwave`/`fireballs` (`0x00525060`): T visible and farther than 15 — the minimum
    applies only while the global int `0x012ec720` (`0x005e5a70`) exceeds 1;
  - 61 `avatar teleport stomp` (`0x00525140`): when T is in state `0x009a1dc0` it backs off **action
    58** for 7000 ms and refuses; otherwise T must be visible and farther than 30;
  - 56 `brute gun finisher` (`0x0052a1b0`): T is player-class, visible (`0x004fc6d0`), `0x004e66b0`,
    and in state `0x009a1dc0`;
  - 55 `brute attack brute` (`0x00524ed0`): T is a brute and visible; then either a vector test on
    both humans' `+0x1cec`…`+0x1cf4` (against −12.0) followed by a distance test, or `0x005031b0`;
    `0x00963d40`; T `+0xdb0` not `0x00456940(0x011181e0)`;
  - 54 `brute qte player` (`0x00525db0`): T is player-class, visible and passes `0x005031b0`; not
    `0x0060b530`; `0x00963d40`; global `0x022cce86` bit 0 clear. For a `0x009422d0`-class human, also:
    T not `0x00942220`, the human not `0x0097cdd0`, and either the global timer `0x012e27f0` has passed
    or at least `0x013ba814` list-5 humans are within π of T;
  - 53 `brute bull rush` (`0x005275e0`):
    - T not `0x0060b530`; no list-18 entry for T younger than 500 ms;
    - T visible; T's vehicle, if any, has `+0x5c` ≥ 0.1;
    - T on the human's level (`0x004df250` = 0, 11.6);
    - T within 45° of the facing vector; clearance probes;
    - holder and riot-shield rules as for 51; the `frustrated` goal test (`0x004f3cf0`) as a
      tie-break;
    - finally a navmesh path between the two humans' cells (`0x00a12180` twice, then `0x00a11e10`)
      must exist. **This query always fails in the shipped game (§14); this final clause always
      refuses first, so the tie-break logic above it decides nothing in retail.**

**H. Thrown weapons (geometry builders).**
- **5 `throw weapon`** (`0x005531a0`):
  - the held item is throwable (`0x00b88610`) and usable from the seat;
  - T exists and is **not visible** (`0x004fc6d0` false), so a throw is a response to a hidden target;
  - P; T not in a vehicle; clearance probes;
  - a trajectory solve (`0x0054c770`) → `+0x24` = P and `+0x30`…`+0x38` = the launch data.
- **4 `throw grenade`** (`0x0054dce0`, 1822 instructions, read for structure only):
  - a grenade in either hand (info `+0x348`); `0x0050c930(750)`; a target (T, or `0x004e6bb0`);
  - it collects allies to avoid (list 5 and the manager's human lists, `0x0054ca40`);
  - it tries aim points in turn: P, T's cover object, nearby objects;
  - the first that `0x0054d930` can solve fills the block.

  It reads the time since T was seen (`0x004fc760`) and no behaviour-record field.

### 11.4 Which goals reach which family (from the shipped `ai_goals.xtbl`)

HIGH (a join of 11.1's data read with 11.3).
- **`change mode`** (always evaluated first): 68, 65, 27 — the three "switch body or state" actions.
- **`kill near player`** and **`kill enemy`**: the strike, brute and avatar gates of C, D, F and G, in
  list order. `kill enemy` begins with 63 `quick kill` and 58 `avatar stomp`. Because 58 is the
  return-false stub (11.3 A), `kill enemy`'s second entry can never be taken.
- **`suppress`**: popout, then the fire actions and `cover fire`, then grenade.
- **`cover`**, **`survive`** and **`reload`**: mostly the cover family. 46 comes first in `cover` and
  `reload`, 64 first in `survive`, and `reload` also lists 6 and, last, 44.
- **`advance`** and **`acquire target`**: 25 `move to navmesh` first, then a mix of the movement
  family B, the brute gates, 45 and the cover actions 10 and 13.
- **`investigate`**: 25, 27, then 43, 41, 42 (peek before move). Because 43 needs the current action to
  be 41 or 42, the first investigate decision always falls through to 41.
- **`group`** and **`idle`**: the follow family.

### 11.5 Behaviour-record fields: who reads them

Method: a scan of every instruction in the executable for a load of human `+0x31c` (the record pointer)
followed, within 40 instructions and while the register is not overwritten, by a memory access at a
record-field offset. CONFIRMED for the sites listed. HIGH that the list is complete for this access
pattern: a reader that reaches the record through another path (a copied pointer passed as an
argument, or an index into `0x0135bfc8`) would be missed. A second scan for byte accesses at `+0x1fc`
and `+0x1fe` in the AI range `0x004d0000`–`0x00560000`, and for absolute addresses inside the record
array, found only the one hit named below.

Field names and offsets are those of `spec-tables-traffic-ai.md` §10.

| Field (offset) | Read by an action routine | Other readers |
|---|---|---|
| `Group/seperated_distance` (`+0x1dc`) and `_interior` (`+0x1e0`), chosen by human `+0x317` bit 0 | 17 `move regroup`; 46 `human shield grab` (followers only) | perception's main sense pass `0x00506c20`; `0x00508750` (from `0x0050a690`, `0x00538890`, `0x005397d0`); `0x005397d0`; the `Human shield` tick's `0x0050ea60`; the `Combat Movement` tick's `0x0053ce90`; `Follow` code `0x0054a420` (from the `Follow` enter `0x0054ba70` and `0x0054b0d0`) |
| `Cover/abandon_cover_time` (`+0x1e8`) | none | the cover-abandon check `0x0050a000` (§10.2 step 1) only |
| `Cover/find_new_cover_time` (`+0x1ea`) | none | cover code `0x00541600` (from `0x00543320`): arms `+0x964` (11.6) |
| `Cover/advance_cover_time` (`+0x1ec`) | 10 `cover advance` | the `cover` and `advance` goal tests (§9.6); `0x0050a000` |
| `Cover/stay_down_time` (`+0x1ee`) | none | `0x00541600`: arms `+0x968` (11.6) |
| `Cover/pop_up_time` (`+0x1f0`) | 8 `cover take def` | the `cover` goal test |
| `Suppress/length` (`+0x1f2`) | none | the `Fire spray` mode: its enter `0x00544c40` (through `0x00544bd0`) and its tick `0x00544cc0` (through `0x00543c50`) |
| `Cover/squad_cover_pct` (`+0x1fc`) | none | **none found** |
| `Options/dodge_chance` (`+0x1fd`) | 26 `move sidestep` | none |
| `combat_aggression` (`+0x1fe`) | none | **only the behaviour-assignment scorer `0x004f3470`** (below) |
| flag bits 3/4 (`can_fire_while_walking`, `can_back_step_fire`) | none | `Combat Movement` helper `0x00539000` (also reads `Kill_Enemy/seen_in_last`); cover code `0x00542bc0` (bit 3; from the `Cover stay` enter `0x00543480`); `0x0095a020` (outside the AI range) |
| the `avoid brute` goal byte (`+0x1c7`) | 16 `move retreat` | the chooser |
| ability-enabled byte of 11 (`+0x68`) | 11 `cover stay down` | the chooser |

Consequences (HIGH, from the table):

- **Only five of the eight fields that §8 item 11 listed are read inside action routines at all**:
  `seperated_distance` (17, 46), `advance_cover_time` (10), `pop_up_time` (8) and `dodge_chance` (26).
  The cover timing fields `abandon_cover_time`, `find_new_cover_time` and `stay_down_time`, and
  `Suppress/length`, take effect in mode code, as 11.6 details for the cover timers.
- **`squad_cover_pct` has no reader.** HIGH: unused in this build. The loader writes it (default 100);
  nothing reads it back.
- **The behaviour record's `combat_aggression` is a behaviour-selection key, not a run-time tuning.**
  `0x004f3470` (CONFIRMED by listing; called from `0x004f3600`) scores one human-description entry
  (`0x0135b700 + 16·i`, `spec-tables-traffic-ai.md` §10) against a character:
  - **−2000** for a `Scripted_Only` record, and nothing else is scored;
  - **+80** for a weapon-class match. An entry class of −1, or a caller class of 22, is a wildcard
    that neither scores nor fails;
  - **+40** for a rank match (the character's descriptor `+0xf0` → `+0x28` against the entry's rank
    pointer). A null entry rank is a wildcard;
  - **+20** for a team match against human `+0x1ca8`. Team 9 is a wildcard;
  - **minus |record `combat_aggression` − personality `combat_aggression`|** (`0x00507e30` → `+0x04`);
  - **−1000** when any non-wildcard criterion failed. A `Riotshield_Only` bit that differs from the
    caller's flag also counts as a failure;
  - then **+10**.

  So the behaviour whose aggression is closest to the character's personality aggression wins ties.
  For `spec-ai-behavior-format.md` §3 (behaviour assignment) and `spec-tables-traffic-ai.md` §10: this
  is the consumer of `combat_aggression` and of the human-description sub-table.
- **The personality's own `combat_aggression`** (`ai_personalities.xtbl`, §9.8) is read by one action
  routine, 18 `move advance`, through `0x00507ea0` (= 0) and `0x00507ee0` (= 2). The decision layer
  itself reads only the "= 2" test (§9.8).

### 11.6 Helpers settled on the way (§8 item 13 and others)

CONFIRMED by listing unless marked.

- **`0x004fdac0` = "the target is visible now".** It is `0x004fc6d0` applied to the human's target
  handle. `0x004fc6d0(handle)` is true when the entity's list-0 entry has flag `0x40` (in view), or has
  flag `0x01000000` while the human has a cover object (AI sub-object handle via `0x004dd240`).
  **[resolves §8 item 13]**
- **`0x004fdae0`** (read in passing): true when the human has no target handle, or the target's list-0
  entry has flag `0x400`.
- **`0x004fe390(out, entity, aim, exact)` = the last-known-position query.**
  - A non-human object yields its bounding-box centre, and the call returns true.
  - For a human it uses the list-0 entry, else the list-6 entry.
  - Without flag `0x8` it copies the stored position (plus the offset `0x013b8040` unless `exact`) and
    returns **false**.
  - With `0x8` and `0x40` (in view) it returns the aim point (`aim`), the true position (`exact`), or a
    velocity-led position (`0x0094bc80` plus 0.1 × velocity).
  - With `0x8` only, it extrapolates the stored position by the entity's velocity scaled by its age
    (`0x004fd3c0`).

  So it is true exactly when the entity is an actively known hostile. **[raises §9.6 `advance` step 2
  from HYPOTHESIS; resolves §8 item 13]**
- **`0x004df080(human, &from, &to, melee, &out)` = the weapon-band test.** It returns −1 when `to` is
  inside range a, +1 when beyond range b, and 0 inside the band (ranges from `0x004dd530`, §9.8). The
  optional `out` is a point on the line inside the band. HIGH: a and b are the minimum and maximum
  engagement ranges, which raises §8 item 16's reading of weapon-info `+0x124`/`+0x128`.
- **`0x004e68e0(human, T, a, b)`.**
  - It returns 1 when the human is not a live human-typed object, or when it has no estimate P.
  - When the human is down or in state `0x009a1dc0`, it also returns 1 unless P lies within 2.0 of the
    human on every axis.
  - Otherwise it returns `0x004df080(human, position, P)`.

  So `advance` step 6 ("must return 1") means **"T is beyond the human's maximum weapon range, or there
  is no usable estimate"**. **[resolves §8 item 13]**
- **`0x004df250(human, T)` = the level difference** between the human and P, through
  `0x004de500(position, P, human navmesh cell +0xb40, −1)`; 0 when there is no T or no P.
  `0x004de500(a, b, cell a, cell b)` returns 0 when either position is not finite, when
  0.25 × (height difference) truncates to 0 (`0x00dad900`), or when the two navmesh cells
  (`0x00446c60`) lie within 256 grid units of each other or are connected (`0x00445980`). Otherwise
  it returns that truncated count, which is negative when P is higher. HIGH: "how many storeys apart".
  So `advance` step 3 (≤ 0) accepts T on the human's level or above, and bull rush (= 0) requires the
  same level. **[resolves §8 item 13]**
- **`0x0050cdb0(shooter, victim, &out)` = "the shooter is aiming at the victim"** (HIGH):
  - the shooter must hold a weapon;
  - a player-class shooter qualifies at once when its target handle (`+0x1e30`) is the victim;
  - otherwise the shooter must be in a vehicle or in combat (`0x00943a20`), and not `0x009ba8b0`;
  - the victim must lie within the weapon's range (info `+0x120`) and inside a cone around the aim ray
    (player: from the aim point `+0x1e38`; others: `0x009c45a0`). The cone's tunables
    (`0x013ba290`/`0x013ba2b0`) are zero-fill, set at run time.

  This raises §9.6 `cover` rule 8's HYPOTHESIS. **[resolves §8 item 13]**
- **`0x00540a70(human, snap, &r)` = "at my own cover point"**:
  - the current cover object (AI sub-object, `0x004e5e60`) must be claimed by this human (its
    `+0x70` handle is the human's own);
  - the horizontal distance from the human (navmesh-snapped when `snap`) to the cover point (object
    `+0x3c` when `+0x8d` bit 3 is set) must be at most r;
  - the radii `0x024fb1c0`/`0x024fb1d0` are zero-fill run-time tunables.

  **[resolves §8 item 13]**
- **`0x009c05b0(human, force)` — the rest of the `reload` test.** After "loaded below capacity"
  (`0x00b81c30`; with info flag `0x200000`, a second clip at item `+0x1b0` counts too) or `force`, it
  refuses:
  - while any of animations `0x1d6c`, `0x1d8d`, `0x1d8f`, `0x1da1`, `0x1d56` plays;
  - while `0x0095d720` or `0x00941e60` holds;
  - for a proxy, unless `force`;
  - during a grab (`0x009aa260`), unless `+0x18d4` = 1 with `+0x18dc` bit 0 clear;
  - for a weapon with info `+0x14` bit `0x80`, or one refused by `0x00922dc0` when info `+0x3ac` has
    bits `0x60`;
  - while human `+0xe8` bit `0x100` is set.

  It then needs spare ammunition (`0x009bff20` > 0). **[resolves §8 item 13]**
- **`0x0097e280(human)` = the player of the human's group.** It is null for a player. Otherwise
  `0x005088c0` reads the group slot byte `+0x994` (< 40), which indexes the 40-row table `0x013b8780`
  (stride `0xa8`). When the slot is in use (`+0x10` non-zero), `0x00508640` returns the player — from
  the manager's `+0x1f0` list, which holds players only (§8 item 7) — whose own `+0x994` matches.
  **[resolves §8 item 13]**
- **`0x009aa360(object, which)` = "held in a grab".** HIGH. It is true when the object is the held
  party of a grab link and the link's state bit is set:
  - the link is either the object's own back-link `+0x1898`, or a partner (found through `+0x18d4` and
    `0x009aa130`) whose `+0x1890` names the object;
  - the state bit is `+0x18dd` bit 0 or `+0x18dc` bit 0, chosen by `which` and by local ownership
    (`0x008addb0`).

  Routines use it on T ("is T somebody's human shield?") and on the human. **[raises §9.3 gate test 5
  to HIGH: the gate refuses decisions while the human is held; resolves §8 item 13]**
- **The class bit of `0x00971cf0`** is the player class (is-a bit `0x21`, §10.1). It is used here on T
  together with T's player-only timer `+0x277c` (23, 6). **[resolves §8 item 13]**
- **`0x004dd640`** (`+0xccd` = 9 with `+0xcce` 15, 16 or 18) is tested only by cover code and cover
  goals, and both cover timers below are re-armed when it holds. HIGH: state group 9 is the cover
  posture family. The meaning of each pose number stays OPEN.
- **The vehicle-geometry tail of `advance`** (§9.6 step 7). It takes the human's position into T's
  vehicle frame (`0x004e44b0` matrix, `0x004f4770` transform) and measures the squared distance to the
  vehicle's local bounding box. **That box is T's own vtable `+0x60` getter's result** (`0x00a7a4e0`,
  resolved 2026-10-07 — a per-instance cached box, not a direct read of the three load-time asset
  boxes; see `spec-physics-runtime.md` §8.6). `0x004f2970` is CONFIRMED (full disassembly) to be an
  exact clamp-per-axis-then-squared-distance AABB formula with no embedded tolerance of its own — a
  pure geometric primitive, no rotation handling (the point is already in the box's local frame by the
  time it runs). It returns true only when that exceeds 0.5 (`0x01116a20`) on every lane (`0x004bc680`,
  also now fully disassembled: a lane-wise less-than test between its two vector arguments via
  `CMPLTPS`/`MOVMSKPS`/`CMP …,0xF`, no constant of its own). So for a `0x009422d0`-class human
  against a T in a vehicle, `advance` holds only while the
  human is more than about 0.71 from the vehicle's body. **[resolves §8 item 13]**
- **Owners of the cover timers `+0x964`/`+0x968`/`+0x96c`** (whole-program scan of every timer arm,
  read and write at these offsets in the AI range):
  - **`0x00541600`** (cover code, reached from `0x00543320`; HIGH: run on taking a cover object):
    - arms **`+0x964` (find-new-cover)** for `find_new_cover_time` × a random 80–125%, plus a delay
      the function adds to both timers;
    - restarts **`+0x96c` (time in cover)** from 0;
    - arms **`+0x968` (stay-down)** for `stay_down_time` × a random 80–125%, plus that delay. While
      human `+0x315` bit 7 is clear (the function then sets it) and recent damage is below 20%
      (`0x004fc8b0` < 0.2), it uses 800 ms instead;
  - `0x005403c0` (cover code) re-arms `+0x968` for 10 s when the committed reason `+0x444` is 7
    (`cover`), and stops `+0x96c` (−1);
  - the cover-abandon check `0x0050a000` arms `+0x964` and `+0x968` for 500 ms while `0x004dd640`
    holds;
  - code at `0x004ebfdc` sets `+0x968` to expire at once while `0x004dd640` holds.

  This fixes the meaning of the A1 reads:
  - `cover` rule 6 and `cover take def` compare the **stay-down** timer with `pop_up_time`;
  - `advance_cover_time` is measured on the **time-in-cover** clock.

  **[resolves §8 item 13's timer owners; raises §9.8's HYPOTHESIS "pop-up and advance timers"]**
- **`0x004f4ae0(human, action, ms)`** arms repeat timer `+0x320 + 4·a` for a locally owned human, and
  sends the request through `0x004f4830(human, 3, a, ms)` for a proxy. For action ids ≥ 70 it does
  nothing. It is the per-action setter that §8 item 14 asks about; who calls it at spawn was not traced.
- **`0x004e9e90(&centre, &radius, human)`** (HIGH: the human's post or defend area): used by 16 (no
  retreat out of a post tighter than 6.0), 50 and 51.
- **`0x004de3f0(human, handle, ms)`**: the entity has a perception-list-18 entry younger than `ms`.
  HIGH: list 18 (§10.3: written by cover and movement code) remembers recent failures to reach an
  entity. Chase, rush and bull rush back off from it.
- **Not resolved here** (no action routine calls them): the weapon upkeep `0x00600720`, `+0x76f`
  bit 3 (the routines test bit 5 only), and the writer of `0x0135bfc4`.

### 11.7 Corrections and port notes

1. **[corrects §9.5]** 58 `avatar stomp` has the shared return-false stub `0x006948c0` as its routine,
   so no goal can choose it (11.3 A). 20 `avatar chase` runs 19's routine through a jump stub.
2. **[corrects §9.7's HYPOTHESIS]** The action routines read only `seperated_distance`,
   `advance_cover_time`, `pop_up_time` and `dodge_chance` of the eight listed fields.
   `abandon_cover_time`, `find_new_cover_time`, `stay_down_time` and `Suppress/length` are read in
   mode code. `squad_cover_pct` is read nowhere. `combat_aggression` is read only by the behaviour
   scorer (11.5).
3. **Event kinds 5 and 97** are consumed by `follow avoid LOF` and `follow browse` (11.3 F, G), which
   look for them in the human's queue. Their producers are unknown; they join §8 item 2's list.
4. **Port note: routines are not pure.** Back-offs and perception write-backs happen on refusal
   (11.2). A port that pre-filters actions, or calls a routine twice per decision, changes the
   behaviour.
5. **Port note: the block is not fully written.** Gates leave it as initialised (NaN positions, zero
   handles), and modes must tolerate that. 31 copies the *current* committed handle, so its block
   depends on what ran before.
6. **Port note: brute and avatar ranges.**
   - Teleport stomp needs T beyond 30.
   - Shockwave and fireballs need T beyond 15 only while the global `0x012ec720` exceeds 1.
   - Killbane strafe needs T at least 2.5 away.
   - Car flip and bull rush need T's vehicle `+0x5c` ≥ 0.1.

---

## 12. Perception's sensing primitives (unit A4)

**Scope.** `review/scoping-ai.md` proposed this unit as "the perception body — the `0x00507520`
subtree, ~10k instructions" on the theory that A2 (§10) specified only the *write-side format* (the 22
lists, their flags, the producers' call graph) while A4 would specify *how well NPCs notice things* —
the vision cone, hearing, line-of-sight and memory/decay algorithms themselves. **That framing does not
survive contact with §10.** A2's own listing of `0x00507520` (306 instructions, §10.2) already *is* the
perception body's dispatcher, and A2 already read most of what feeds it: the vision test, `notice`,
the damage-memory decay formula, the attitude function, the score-scale conventions. What A2's own scope
note (§10, opening paragraph) and its "not read"/"OPEN" asides explicitly left out is a short, specific
list of sensing primitives: the vision cone/FOV test's real geometry and tunables, the line-of-sight
*service* behind the async answer it reads, two sound-radius producers it found but did not decode
(lists 10 and 12), the perception tunables block itself, the slow pass, the team-relationship table, the
target-handle validity check, and the ally-link geometry test. **This section reads exactly that list.**
It is therefore much smaller than ~10k instructions — the real gap A2 left behind, not a re-derivation
of the subtree A2 already covered. It answers the rest of §8 item 12 and closes §10.3's and §10.6's
smaller "not read"/"not decoded" asides.
**Method.** `tools/harnesses/exe_va.py` (file-backed VA reads, no Ghidra) plus `capstone`, run through
small scratch scripts (branch-following single-function disassembly; a raw byte-pattern scan of the
whole `.text` section for a literal absolute address, used to find both readers and writers of a global
without a disassembler database; a scan for the `push addr; push name; mov ecx,slot; call` tweak-registry
idiom). Every function cited CONFIRMED below was read in full by this method. Tunable values are read
from the shipped `tools/ai_base_xtbl/tweak_table.xtbl` (the real base-game file, 601 rows) and cross-checked
against `tools/ai_tweak_registry.tsv` (the 586-name registry `spec-tables-progression.md` §10.10 already
specifies in full).

### 12.1 The perception tunables are ordinary `tweak_table.xtbl` entries

A2 flagged "the zero-fill perception tunables block `0x013b7fd8`…`0x013b8450`, filled at run time from a
source not traced" (§10, scope note). That source is the **same tweak-table mechanism
`spec-tables-progression.md` §10.10 already specifies in full**: a registration stub
`push <var addr>; push <name string>; mov ecx, <registry slot>; call 0x0071B580; ret`, one stub per
tunable, in the range `0x00fecd40`–`0x00fed55f`. `0x0071B580` is that document's own float constructor
(type 1 of 4). The real names and shipped values (base `tweak_table.xtbl`, `Entries:AI:Perception:FOV:*`
and `Entries:AI:Perception`/`Entries:AI:Follower`):

| Name | VA | Value | Unit | Category |
|---|---|---|---|---|
| `Ai_fov_length_low` | `0x013b8010` | 35 | m | FOV:Normal |
| `Ai_fov_length_high` | `0x013b800c` | 45 | m | FOV:Normal |
| `Ai_fov_width` | `0x013b8008` | 150 | deg | FOV:Normal |
| `Ai_fov_height` | `0x013b8004` | 80 | deg | FOV:Normal |
| `Ai_fov_length_low_spotted` | `0x013b8000` | 80 | m | FOV:Spotted |
| `Ai_fov_length_high_spotted` | `0x013b7ffc` | 90 | m | FOV:Spotted |
| `Ai_fov_width_spotted` | `0x013b7ff8` | 190 | deg | FOV:Spotted |
| `Ai_fov_height_spotted` | `0x013b7ff4` | 140 | deg | FOV:Spotted |
| `Ai_fov_length_zombie` | `0x013b7ff0` | 20 | m | FOV:Zombie |
| `Ai_fov_width_zombie` | `0x013b7fec` | 80 | deg | FOV:Zombie |
| `Ai_fov_height_zombie` | `0x013b7fe8` | 80 | deg | FOV:Zombie |
| `Ai_fov_length_cover` | `0x013b7fe4` | 50 | m | FOV:In Cover |
| `Ai_fov_width_cover` | `0x013b7fe0` | 220 | deg | FOV:In Cover |
| `Ai_fov_height_cover` | `0x013b7fdc` | 180 | deg | FOV:In Cover |
| `Pain_sound_radius` | `0x013b8050` (vec4) | 8.0 | m | AI:Perception |
| `Grenade_shout_radius` | `0x013b8070` (vec4) | 25.0 | m | AI:Perception |
| `Player_friendly_fire_threshold` | `0x013b7fd8` | 150 | HP | AI:Follower |

`Player_friendly_fire_threshold` sits immediately below the FOV block in memory and shares its
registration pattern, but its category is `AI:Follower`, not `AI:Perception` — a different subsystem
(the player's AI-follower turning hostile after enough damage) reusing adjacent storage, **not** a
perception input. No reader for it was found in the perception range (`0x004f8000`–`0x0050b000`); it is
flagged here only so a reader who finds it beside the FOV block does not mistake it for one. The real
degree→radian constant the FOV test uses is confirmed as plain π/180 (`0x012a30d8`, read as a double,
= 0.0174529999…).

### 12.2 The view-cone/FOV test `0x004fef80` (327 instructions, full listing)

CONFIRMED by listing. Signature `bool(this=perception, target, out-position-struct)`. A null target
returns false. Otherwise it picks a **length** (metres, used as-is) and a **width**/**height** (degrees,
multiplied by π/180) from one of five tunable sets, then builds and tests a cone/frustum against the
target:

1. **Default:** `Ai_fov_length_high` / `Ai_fov_width` / `Ai_fov_height` (45 m / 150° / 80°).
2. **Carried/witnessed override** — the target is-a type with descriptor bit `+0x6` bit 6 (looked up via
   the shared type-bit tester `0x00853b30`, the same accessor §10.1 and §9.10 already use for the player
   and human-typed bits; this bit's own identity was not named): length is replaced outright by a
   compiled constant **40.0 m** (`0x012a2e70`, broadcast to `0x013b8220`, not a named tunable), and width
   **and** height are each widened by a flat **+40°** (`0.6981317…` rad, double constant `0x011175b0`).
   HYPOTHESIS: a human who is carrying or has just witnessed something gets a wider cone around it.
3. Otherwise, gated on the target's own is-a bit `+0xa` bit 0 (`0x00853b30(target, 0x20)`; unidentified
   class — a network-proxy-adjacent or non-character object type, HIGH) — a target lacking it keeps the
   step-1 default outright, skipping everything below:
   - **full bypass:** `0x004faaa0(this=perception, target)` compares the target's own handle
     (`target+8`/`+0xc`) against perception's **own** `+0x280`/`+0x284` (the same field §10.6 names as
     "the friend's own perception threat handle", here read on the observer itself) and, if they match
     **and** the observer's own sub-state byte `+0x292` is 6 or 2, returns false. When it does, the whole
     test returns **true at once**, skipping every check below. HYPOTHESIS: "don't bother cone-testing
     the thing I am already actively engaged with."
   - otherwise, the cone narrows by context, but **only the spotted/reduced branch below is additionally
     gated on local authority** (`0x008addb0`, the same authority test §1.1 uses) — the zombie and cover
     branches run for a network proxy too:
     - **zombie:** `0x009422d0(self)` (descriptor `+0xf0` byte `+0x48` bit 7 — the same unnamed class bit
       §9.6 step 7 and §11.6 already cite as "a `0x009422d0`-class human", now additionally the selector
       for the zombie FOV set) → 20 m / 80° / 80°;
     - **in cover:** else `0x004dd640(self)` (the cover-posture state test, §11.6) → 50 m / 220° / 180°;
     - else, only while locally authoritative:
       - **spotted:** the target's own list-0 entry (looked up by `0x004fae50(this, 0, target)`, a
         by-list-and-handle sibling of §10.1's `0x004fae00`) is tested against flag mask `0x182` —
         exactly the mask §10.4 already names as "tested by the view-cone test" — and, if any of those
         bits are set (the target has been noticed before, visually or otherwise), the cone switches to
         the `_spotted` set, high or low sub-variant chosen by a team-rule lookup (`0x00605220`, §10.3's
         attitude helper), HIGH, not fully bit-traced;
       - **reduced:** else (not yet spotted), when the target is on foot (`0x009b9160`) and a further
         gate (`0x004dd640(target)` or an object flag) holds, length is halved and width cut to a third
         (constants 0.5 / 0.33, `0x012a2dc0`/`0x012a2e00`) — HIGH: a "target doing something that
         narrows its own visibility" reduction (crouch/prone-adjacent), not otherwise identified;
       - a non-authoritative observer, or one past the zombie/cover tests with neither holding, keeps
         the step-1 default.
4. **Combine:** the chosen length/width/height build a frustum via `0x004fdf70` and the actual
   inside/outside test runs through `0x004fdc90`/`0x0075d7e0`/`0x00525d30` — these geometry primitives
   were traced only as far as their call shape (a min/max blend of two scaled bounding offsets around the
   mid-point between observer and target), not read instruction-by-instruction; the cone/dot-product
   arithmetic itself is OPEN.

**`Ai_fov_length_low` (35 m) is never read here.** A raw scan of all of `.text` for its address found
exactly two references: the registration stub, and one read inside the generic distance-score helper
`0x004fa9f0` (§10.3's own "100·d²/r², with r the tunable `0x013b8010`" — now identified by name and real
value). So despite its name, `Ai_fov_length_low` is not consulted by any field-of-view test in this
build; it is reused purely as a distance-normalisation radius for list-6 hearsay scoring. **[extends
§10.3]**

### 12.3 Hearing: two sound-radius producers A2 found but did not decode

A2 already names both functions below as list producers (§10.3) but marks them "not decoded". Both are
short and were read in full.

- **List 12 — distress noticed nearby, gated by `Pain_sound_radius` (8.0 m).** `0x00502130` (174
  instructions): needs activity level `+0x260` above 4 and the source object within `Pain_sound_radius`
  of self (`0x004defd0`, the same proximity test §10.3 cites for the unsighted-notice path); on success
  writes a list-12 entry (score still via the generic `0x004fa9f0` scorer, not a `Pain_sound_radius`-based
  score — the tunable only gates whether to notice, not how urgently). It then reads the source's
  attitude (`0x004fe0f0`): if hostile-ish (≤ 2) and no matching event already queued, posts an event
  (500–1000 ms random delay via the already-cited `0x004f5f80`); if friendly and the human is **not** in
  combat (`0x004fd720`, §10.2's own activity-level helper), the human says the voice line **"call a
  doctor"** (`0x011175c0`, via the same sayer `0x00754410` that already says "ouch"/"stop it"/"sounds like
  trouble" for list 3/16, §10.3). Independently, if the source object is now down or dead (`0x0096f4f0`),
  it is forwarded to the body-found pipeline `0x004ffa50` (→ list 7) regardless of the list-12 outcome.
  **List 12 = "a nearby human is in pain/distress"; it can lead straight into the body-found report
  once that human actually dies.** **[resolves list 12 of §8 item 12 and §10.3's "not decoded"]**
- **List 10 extended — "fire in the hole", gated by `Grenade_shout_radius` (25.0 m).** `0x00501d80`,
  already named in §10.3 as a list-10 feeder, now read in full (CONFIRMED): needs activity level above 3
  and the position source within `Grenade_shout_radius`; clears self `+0x317` bit 2 (the same bit list 16
  clears on taking damage, §10.3); writes a list-10 entry with a **fixed** score of 4.0 (constant
  `0x0137fa40`, §12.9), deduplicated against an existing entry for the same handle. It then posts an event
  — kind `0x38` (56) when a specific shout source handle is known, else kind `0x12c` (300) generic — HIGH
  confidence only on the kind values themselves (the nine pushed dwords match §10.5's seven-field
  signature once the two handle fields are counted as two dwords each), not on the exact field-by-field
  mapping. **Both kinds are new — they are not in §10.5's existing list of 0, 2, 11, 12, 18, 71, 76, 78,
  86, 87, 100.** **[extends §10.3 and §10.5]**

### 12.4 The line-of-sight service: the async answer, and the request queue

A2's own scope note excluded "the vision line-of-sight service (`0x004ed1e0`/`0x004ed550`)". Both halves
were read in full.

- **`0x004ed1e0` (40 instructions) — decode the cached answer.** Looks up an existing result via
  `0x004ecfc0(args)`; if one exists, decodes byte `[result+0x1c]` bit 0 into **2 = visible** (bit clear)
  or **3 = blocked** (bit set) — exactly the two values §10.3 already quotes for this function, now with
  the decode shown. If no cached result exists yet, it falls back to `0x004ecf40(args)` and normalises
  that call's result to a plain 0/1 (HIGH: "pending"/some other immediate state, not decoded further).
- **`0x004ed550` (119 instructions) — the request queue.** Gets or creates a per-caller container via
  `0x004ed320`; walks the container's existing requests and **dedupes**: a pending request with the same
  four key dwords (two positions, or a position and a handle) and the same mode byte is left alone and
  the call returns without re-queuing. Otherwise it pulls a free node from a global free-list
  (`0x01357798`), fills the four key dwords, the mode byte, and filter bytes `+0x24 = 0x2d` /
  `+0x25 = 0xb2` — **the identical "any mode / any sub-state" sentinel pair** the event pool's default
  filters use (§10.5) — and optionally copies a 32-byte ray/segment payload (two positions) before
  linking the node into the container's doubly-linked list.
- **Scope boundary — CLOSED, 2026-10-08 (`spec-physics-runtime.md` §8.11).** `0x004ecfc0`/`0x004ecf40`/
  `0x004ed320` themselves are, by direct disassembly, confirmed pure cache/queue data-structure code —
  zero calls to Havok or any geometry primitive, zero indirect/vtable calls at all (leaf binary-tree/
  linked-list lookups over self-referencing sentinel nodes `0x01357790`/`0x0135779c`). **The real
  raycast resolver is a previously-unnamed sibling, `0x004eda70`**, reached from a scheduler,
  `0x004edc80` (which walks the pending-request tree and resolves at most the single most-overdue node
  per invocation, matching this section's own "re-check interval 700-4000 ms" framing) — not from
  `0x004ed1e0`/`0x004ed550` directly. `0x004eda70` calls `spec-physics-runtime.md` §8.8's shared ray
  wrapper `0x0075d7e0` directly with a zero-initialized input (the closest-ray-hit-collector branch),
  reaching the EXACT SAME Havok `castRay` chain already confirmed there for AI's "line of fire" probe,
  and writes its boolean result into this section's own `+0x1c` bit 0 (the byte `0x004ed1e0` above
  decodes). **So the vision/line-of-sight service and the line-of-fire probe are two different
  higher-level consumers of one identical low-level Havok path** — the entire async/dedup/scheduler
  apparatus this section documents exists purely to throttle and cache calls into that one shared
  wrapper, not to avoid a physics dependency. A port needs the dedup/async-queue behavior described
  above on top of whatever raycast primitive it implements, matching `spec-physics-runtime.md` §8.8's
  own contract. This also closes this document's own OPEN item 19 (listed earlier in §11).

### 12.5 The target-handle validity check `0x004fc100` — corrects the target-refresh framing

§10.3 labelled one step of the target-refresh pass "choose the target (`0x004fc100`)". Read in full (41
instructions), it is not a priority chooser. `0x004fc100(this=perception, list, handleLow)` calls
`0x004faf40(this, list, handleLow, 0)` — a by-list-and-handle lookup sibling of the `0x004fae00`/
`0x004fae50` family (§10.1, §12.2) — to find the **given** handle's own entry in the **given** list, and
returns that handle unchanged only if the entry carries flag `0x8` (actively known hostile, §10.4) **and**
its score is below the "dismissed/maximum" sentinel (≈3.0625×10³⁸ at `0x0137fa90`; §12.9, the same
sentinel the dismiss pass and direct-awareness use, §10.3). Otherwise it returns a null handle
(`{0,0}`, constant `0x01117488`). **So the target-refresh pass is re-validating that the human's current
target is still a live, known, non-dismissed hostile — it is not picking a new target by priority.** A
handle that fails this check becomes null, and *that* is what fires event 78 ("target handle changed",
§10.5) — not a fresh pick replacing the old one. Target *acquisition* (writing a new handle into `+0x1330`
in the first place) happens elsewhere, outside this check. **[corrects §10.3's "choose the target" step
label]**

### 12.6 The slow pass `0x00502f10` — a world-wide, not a local, sweep

A2 flagged this "not read" (§10.2 step 6). It is short (49 instructions) and simple. Gated on activity
level above 4, it walks the **object manager's own human list** (`[0x03171a64]+0x1e4`/`+0x1ec`/`+0x58`)
— the identical manager and list the main sense pass walks (§10.3) — rather than anything
proximity-filtered, i.e. **every live human in the world, not just nearby ones**. For each one, if self
is valid and active (`+0x33` bit 3) and the candidate is down or dead (`0x0096f4f0`), it is forwarded to
the same body-found pipeline (`0x004ffa50` → list 7) the two producers above also feed. This explains
the pass's own throttle (re-armed every 1500–2000 ms, §10.2): it is the one perception sub-pass whose
cost scales with total world population rather than with nearby entity count, so it is the one that
must run least often. **[resolves §10.2 step 6's "not read"]**

### 12.7 The team-relationship table `0x009b7100` — read side of an already-specified write side

§8 item 12 names this table "not decoded". The read accessor is trivial (12 instructions, CONFIRMED): if
either team byte equals 9, return 2 unconditionally; otherwise return
`byte[0x0262d940 + teamA·8 + teamB]`. That matrix's **write side is already fully specified**:
`spec-lua-api-behaviour.md` §20.6/§30.6 documents `set_team`, `team_make_allies` (writes 4), `_unfriendly`
(writes 1) and `_hostile` (writes 0) as the matrix's only writers, each writing both `[a][b]` and `[b][a]`
symmetrically through `0x009b7130`, with the already-flagged defect that an unrecognised team name
resolves to sentinel id 9 and corrupts row/column 9. **The read side defends against exactly that case**
— unlike the write side, a read with either team equal to 9 is special-cased to return 2 (neutral)
rather than reading the (possibly corrupted) row 9 — a fact the write-side document's own defect note
did not have occasion to mention. Observed relation values (0/1/4) line up with perception's own attitude
scale (§10.3: "0…4+, lower meaning more hostile"): hostile relation → low attitude, allied relation →
high attitude. **[closes §8 item 12's "team-relationship table" clause by citation, not re-derivation]**

### 12.8 The ally-link geometry test `0x0050cc40`

§10.6 flagged this "not read", with the HYPOTHESIS "the friend stands in or near this human's line of
fire to the target". Read in full (117 instructions), that HYPOTHESIS is correct and more precise than
stated: `0x0050cc40(self, friend, targetPosition, outClosestPoint)`, gated on self being in a particular
movement/vehicle sub-state (`0x009b9160`) or in combat (`0x00943a20`), computes the closest point on the
**segment from self to targetPosition** to the friend's position (a clamped projection, rejecting the
friend outright if the projection falls before the segment start or after its end), then accepts only if
the friend lies within a **cylinder** around that closest point: a horizontal radius² (`0x013ba290`) and
an independent vertical radius² (`0x013ba2b0`), not a single sphere. These are the same two "zero-fill
run-time tunables" §11.6 already names as the aim-cone radii of the unrelated "is aiming at" test
`0x0050cdb0` — both tests share the one cylinder. The two radii are themselves derived at start-up from a
short chain of further zero-fill values (traced one level: `0x013ba290 = 0x013ba280 · 0x01117a70 (0.5) +
0x013ba260`, `0x013ba2b0 = (0x013ba2a0 + 0x013ba270)²`, with `0x013ba270` itself built from a literal
0.15 at `0x00111643c`); the chain was not followed to a final metres value — left OPEN (§8 item 21), it is
not load-bearing for the mechanism, only for its exact size. **[resolves §10.6's "not read", raises its
HYPOTHESIS to CONFIRMED]**

### 12.9 The score-scale constants, resolved to their real literal sources

§10.3 asserts several "K = 100", "= 2500" style constants as facts without naming where those literals
come from. Each one turns out to have its own tiny startup stub (`0x00fec970`–`0x00feca73`, one `ret`-only
function per constant: load a scalar, broadcast to 4 lanes, store) — a different, simpler mechanism than
the tweak-table registry of §12.1: these are **compiled literals**, not named or overridable tunables.

| Address | Value | Literal source | Consumer |
|---|---|---|---|
| `0x0137fa40` | 4.0 | `0x012a2f6c` | list-10 entry score (§12.3) |
| `0x0137fa60` | 100.0 | `0x00111645c` | the damage/distance "K" pivot (§10.3) — confirms the existing citation |
| `0x0137fa80` | 2500.0 | `0x011173a4` | list-0 proximity outer-radius score (§10.3) — confirms the existing citation |
| `0x0137fa90` | ≈3.0625×10³⁸ | `0x011173a8` | the "dismissed/maximum" score sentinel (§10.3, §12.5) |
| `0x0137faa0` | 3.4028235×10³⁸ (`FLT_MAX`) | `0x012a2ea0` | a generic "no ceiling" clamp |
| `0x013b8220` | 40.0 m | `0x012a2e70` | the FOV carried/witness override length (§12.2) |

### 12.10 Corrections and port notes

1. **[corrects the scoping note, `review/scoping-ai.md` §4 item 5]** The `0x00507520` subtree is not an
   unopened ~10k-instruction body distinct from A2's "write-side format". A2 already read the dispatcher
   and most of its producers; this unit's real content was the handful of specific primitives A2's own
   text flagged as not traced. A future reader should not expect a further ~10k-instruction subtree
   still waiting under this heading.
2. **[extends §10.3]** `Ai_fov_length_low` (35 m) is dead as a field-of-view value in this build; its
   only live reader is the generic distance scorer `0x004fa9f0`, used for list-6 scoring, not for any
   cone test.
3. **[corrects §10.3's step label]** "Choose the target" (`0x004fc100`) validates the *existing* target
   handle; it does not select a new one by priority.
4. **Port note.** The FOV test's state-dependent length/width/height selection (normal / spotted /
   zombie / cover / carried-witness-bonus / reduced-visibility) is a priority chain evaluated in a fixed
   order (zombie, then cover, then spotted, then reduced), not an additive blend; a port should preserve
   the order, not just the five value sets.
5. **Port note.** `Pain_sound_radius` and `Grenade_shout_radius` only gate *whether* an entity is
   noticed; neither feeds the entry's score. A port that tries to derive urgency from these radii will
   not match the shipped behaviour.

---

## 13. Begin-function gating and thrown-weapon trajectory solvers (unit A5)

**Scope.** Closes two spec gaps Team B's AI-runtime build flagged in §3.2's "Begin function" row (13 begin
functions listed by call signature only) and in §11.3 part H (two undecoded trajectory solvers for family
H, "thrown weapons"). This unit fully specifies the internal gating logic of 7 of the 13 begin functions
(`Cower`, `Flee` ×2 bodies, `Gunfire wary` ×2 calls, `Passenger`, `RollerBlader`, `Veh evade`, `Veh threat`'s
own call `0x00523db0`) and the two trajectory solvers behind action 5 `throw weapon` and action 4
`throw grenade`.

**Method.** Ghidra-free: capstone linear disassembly over a small reader built on this project's existing
`tools/harnesses/exe_va.py` (read-only VA→file-offset mapping), reading every cited function in full from
its own listing, the same CONFIRMED-by-listing standard the landed A1–A4 units use. Field names already
established elsewhere in this file (`+0x4a5`/`+0x4a6` force-flag bytes, `+0x317`/`+0x44c`/`+0x500`–`+0x50c`
mode-machinery bytes, `+0x8d0` live mode-parameter block, `+0x450` committed block, `0x2cc9900` the per-type
descriptor table, `0x008addb0` the network-authority test, `0x00853b30` the shared is-a bit tester,
`0x0097e280` the group-leader/"is a follower" accessor, `0x00614d00` the game-mode switch) are cited, not
re-derived, and are the anchor for most of this unit's CONFIRMED/HIGH calls. For the two trajectory
solvers, per the orchestrator's direction, this unit additionally used the project's Unicorn-based
CPU-emulation harness (`tools/harnesses/unicorn_call.py`) to call the real function bytes in isolation with
controlled inputs and capture real outputs — full input/output vectors are given alongside the closed-form
reading. No decompiler output appears below; all addresses are plain hex; no Ghidra auto-names are used.

---

### 13.1 The 7 begin functions

#### 13.1.0 Shared preamble, confirmed across all 7

Every one of the 7 begins with the same two-test gate, byte-for-byte identical in each listing:

1. Null-check the human argument; decline (return false / no-op) if null.
2. **Class gate**: `type = [0x02cc9900 + [human+0x34]*4]`; decline unless `[type+0xa]` bit 2 is set. This is
   the *generic* "this type of AI slot can run a scripted mode entry" gate — it is not Cower-specific
   despite surfacing first in Cower's own listing; the identical three instructions open `Flee`'s, `Veh
   evade`'s and `Veh threat`'s listings too. HIGH (type-descriptor row layout not otherwise named here;
   the table itself is `0x02cc9900`, already load-bearing throughout §1–§12).

Six of the seven (`Cower`, `Flee`'s full evaluator `0x004da0b0`, `Gunfire wary`'s `0x004dbd90`,
`Passenger`, `Veh evade`, `Veh threat`'s `0x00523db0`) then run the **network-authority check**
`0x008addb0(human, 0)` (already named by §1 item 4) and, on failure (not locally owned), build and send
the *same* shaped network message as the apply step's own generic-mode case (§3.2): `0x0086f5f0(0x41)`
constructs a message object, offset `+0x10` of it is set to the literal `0x1b` (the network opcode, not a
mode id — matches §3.2's own "message type `0x1b`, built at `0x0086f5f0(0x41)`"), the human's own handle is
written via `0x0050f790`, the mode's own extra parameters (target handle, flag bits) are written via
`0x0086f4b0`/`0x004d46e0`, and the message is handed to `0x008ae020` then released via `0x0086f110`/
`0x0086eb20`. **This is new**: §3.2 had only shown this pattern at the apply step's own generic-mode case
and (implicitly) at the `Idle` begin. It is now confirmed as the *general* convention for every
not-locally-owned begin function that isn't in the apply step's own "Generic" group — the begin function,
not apply, carries the network-forward responsibility. `RollerBlader`'s begin is the one exception: it
declines outright, with no network forward, when not owned (§1.5 below).

---

#### 13.1.1 `Cower` begin `0x004d5050(human, target_handle, flag5, flag6, flag7, flag0)`

CONFIRMED by listing (ends `0x004d5342`, 754 bytes). Called from apply (§3.2) as the mode-9 case at
`0x004e8455`.

**Real call-site signature (push survey of `0x004e8310`'s mode-9 case).** Apply does not call this with
just "flags from `+0x4a5`/`+0x4a6`" as a single value — it unpacks 4 individual bits and passes them as 4
separate boolean arguments:

```
0x004d5050(human,
            target_lo = [human+0x508], target_hi = [human+0x50c],   // the pending target handle, raw
            flag5 = bit 5 of [human+0x4a5],
            flag6 = bit 6 of [human+0x4a5],
            flag7 = bit 7 of [human+0x4a5],
            flag0 = bit 0 of [human+0x4a6])
```

**Apply-side bracket (context, not inside `0x004d5050` itself).** When `[human+0x4a6]` bit 1 is set, apply
calls a helper `0x004e0870(&human+0x2b0, 0)` immediately *before* calling Cower's begin and
`0x004e0870(&human+0x2b0, 1)` immediately *after* it returns; when that bit is clear, neither extra call
happens. HYPOTHESIS: a pause/resume pair bracketing the begin call specifically for this case — not
resolved further, flagged OPEN.

**Body, in order:**

1. Null + class gate (shared preamble).
2. `0x008addb0(human, 0)`: if not owned, build+send the network message (flags byte `0x1b` type, target
   handle via `0x0086f4b0`, the 4 flag bits via four separate `0x004d46e0` calls) and **return false**.
3. If owned: `0x004eaa40(human)` — a second gate; false declines. HYPOTHESIS (not identified elsewhere in
   this spec): "is usable / not mid-cleanup."
4. `0x004d4c70(target_lo, target_hi, 0)`: if this resolves (nonzero) **or** force-flag `always_cower`
   (`+0x2b9` bit 7, already named by §9.3) is set, continue; otherwise decline.
5. Unless `+0x2ba` bit 5 (an ability/force-disable bit in the same byte region as `always_follow_in_
   alternate_vehicle` bit 0, §11.3 F) is set, both `0x004d8ff0(human, target_lo, target_hi, 0)` **and**
   `0x00a00560(human)` must additionally hold, else decline. `0x004d8ff0` reappears identically in `Flee`'s
   gating below (§1.2) with the 4th argument swapped for a per-mode field rather than a literal 0.
6. **If the human is already in mode `Cower` (current mode `+0x501` == 9):** the function does **not**
   call the transition primitive again. It only updates the stored target handle
   (`+0x8d8`/`+0x8dc` ← `target_lo`/`target_hi`) and returns true. If, additionally, the sub-state
   `+0x500` == `0x54`, it also calls an animation/pose helper `0x0094b070` and requests a **sub-state**
   change to `0x53` via `0x004dd2d0(human, 9, 0x53)` — a different, lighter-weight call than the real mode
   transition primitive (§3.3's `0x004dd380`). This is a genuine example of a begin function declining the
   full transition while still reporting success, because the human is already in the target mode.
7. **Otherwise** (not currently Cower): calls `0x004e51d0(human)` (a per-entry reset/prep helper reused by
   several other begins, §1.2/§1.6 below), then packs a single flags byte from: bit 0 ← the result of a
   helper call `0x0097e9b0(…)` (its own argument shape could not be pinned down with confidence — see OPEN
   item 1); bit 1 ← (`flag6` *or* `[human+0xe8]` bit `0x200`); bit 2 ← `flag5`; bit 4 ← `flag7`; bit 5 ←
   `flag0` (bit 3 is left untouched — a standard MSVC bitfield-assignment codegen pattern, confirmed by
   reading, not a hash). That byte is embedded at offset 4 of a small local block, and
   `0x004dd380(human, 9, &block)` — the real transition primitive (§3.3) — is called. Returns true.

**Net:** Cower's begin can (a) forward to the network and decline, (b) decline outright on failed gates,
(c) silently refresh an already-cowering human without ever calling the transition primitive, or (d) really
transition into mode 9 with a packed 6-bit flags byte built from the apply-unpacked `+0x4a5`/`+0x4a6` bits
plus two extra locally-computed bits.

---

#### 13.1.2 `Flee` — both bodies, selected by `[human+0x4a6]` bit 2

CONFIRMED by listing and by the apply-step selector itself (`0x004e8310`, `0x004e847b`):
`test byte [human+0x4a6], 4; je → call 0x004da0b0` (bit clear) `/ fall-through → call 0x004d91a0` (bit set).

**Bit set → `0x004d91a0(human, target_lo, target_hi, &escape_point, field_0x4a0)`.** Before this call, apply
itself computes `escape_point` via `0x00da38e0(&human+0x474, &escape_point)` — i.e. when the selector bit is
set, apply pre-computes a single escape direction from the human's own `+0x474` sub-object and hands it
straight to this function.

`0x004d91a0` (CONFIRMED by listing, ends `0x004d9383`), in order:
1. Null + class gate; on failure the human is treated as unusable for the rest of the function (a `0` is
   substituted for the "reference object", not an outright decline) — then, separately, if the *raw*
   argument pointer was null, it does decline.
2. `0x008addb0(human, 0)`: **if not owned**, this body does *not* send a network message. It instead calls
   a shared helper `0x004d7b80(target_lo, target_hi, escape_point_or_null, field_0x4a0)` and returns **true**
   unconditionally, without ever calling the transition primitive. HYPOTHESIS: a lightweight local
   replication/prediction of the flee movement for a remotely-owned human, distinct from the
   request-and-wait convention used elsewhere.
3. If owned: `0x004d8ff0(human, target_lo, target_hi, field_0x4a0)` must hold, else decline (same helper as
   Cower's gate, §1.1 step 5, with the literal `0` swapped for `field_0x4a0`).
4. Resolve a reference point: `0x004d4c70(target_lo, target_hi, 0)`; if it fails, fall back to the human's
   own position (self-reference).
5. **If current mode is already `Cower` (9):** call `0x004d5d20(human, ref.x, ref.z)`; if that holds,
   **skip straight to the success return without transitioning** — Flee is absorbed into staying in Cower.
   HYPOTHESIS for `0x004d5d20`: "still too exposed/close to break cover."
6. Otherwise, build an escape point (`0x004d6980`, `0x004d7fa0`), consult a navmesh/path query
   `0x00a12180(&point, 0x0130c230, [human+0xb40], 0)` (the same navmesh-cell-list global and query helper
   §11.3 D's `brute bull rush` and this unit's own `Flee`/`Veh evade` all independently reach — a shared
   resource, not mode-specific; **this query always fails in the shipped game, §14 — `Flee`'s own
   candidate search still has two other, non-navmesh escape-direction candidates tried earlier in this
   same step's search order, so this specific failure narrows rather than blocks the behaviour**), read
   two globals at `0x01115300`/`0x01115304`, and call
   `0x004e5d20(&human+0x2b0, …)` (the per-mode-group `+0x2b0` scratch object reused all over this spec's
   begin functions). Packs a flags byte from `field_0x4a0`'s own bits plus two locally-derived bits, and
   calls **`0x004dd380(human, 13, &block)`** — the real transition into `Flee`.

**Bit clear → `0x004da0b0(human, target_lo, target_hi, field_0x4a0)`.** CONFIRMED by listing (a much larger
function, candidate-search style). Preamble: class gate (same shared one, but structured so that a failed
gate zeroes the "reference object" used later rather than declining outright — only a null human argument
declines immediately). `0x008addb0`-not-owned path calls the **same** `0x004d7b80(...)` helper (with a
literal `0` in place of the escape-point pointer) and returns true, mirroring `0x004d91a0`'s shortcut
exactly. If owned: `0x004d8ff0(human, target_lo, target_hi, field_0x4a0)` gates, as above.

Then, **CONFIRMED, and the most interesting structural finding of this unit**: `0x004da0b0` is the
*caller* of `0x004d91a0` — it tries several candidate escape directions in turn and, for each candidate
that passes its own validity test, calls `0x004d91a0(human, target, &candidate, field_0x4a0 | extra_bits)`
to actually attempt the commit; `0x004d91a0` returning true ends the whole search successfully. In order:

1. **Already-fleeing-same-target short-circuit**: if current mode is already `Flee` (13) and (the stored
   target handle `+0x8d8`/`+0x8dc` already equals the new one, or `0x00456920` says they're equivalent) and
   `+0x8d0` bit 5 is set, jump straight to the success exit with **no re-transition**.
2. **Cower-redirect**: if current mode is `Cower` (9), call `0x004d5d20` as in §1.2's other body; if it
   holds, skip to success without transitioning. **If current mode is *not* `Cower`**, and `0x004eaa40
   (human)` holds (the same secondary gate `Cower`'s owned-path uses, §1.1 step 3), it tries entering
   `Cower` directly: `0x004d5050(human, target_lo, target_hi, 0, 0, 0, 0)` (all four flag args forced to
   0). **If that succeeds, `Flee`'s own begin returns success without ever reaching its own transition
   call** — a confirmed, concrete instance of "a begin function may … enter a different mode" (§3.2). A
   second call to `0x004d5050` immediately follows (reached by both the "already Cower" and the
   "just-redirected-into-Cower" paths); since the human's current mode is now 9, that second call takes
   `0x004d5050`'s own "already Cower" fast path (§1.1 step 6: just refresh the target handle, no re-entry).
   Only when the Cower redirect is refused does evaluation continue to the candidate search below.
3. **Candidate search** (reached only past both short-circuits): tries, in order —
   - a "near cover" candidate, gated on `[human+0x1e00]` bit 7, `[human+0x1da8] != 0`, and `field_0x4a0`
     bit 6 clear, using a geometry probe `0x00bc0800` compared against 1.0; a failing ratio here is what
     triggers the Cower-redirect check in step 2 (the near-cover probe and the Cower-redirect are the same
     branch, not two independent features);
   - a normalized-direction candidate built from `0x00bbfbe0` + `0x00dab810` + validated by `0x004d4bf0`,
     which — if valid — itself calls `0x004d91a0(human, target, &dir, field_0x4a0|0x20)` directly;
   - a longer path-search candidate (`0x004d7e20`/`0x00b71950`, elapsed-time-budgeted via `0x00dab5e0`/
     `0x00d9e250`), which, on success, **builds its own point and calls `0x004dd380(human, 13, &block)`
     directly** rather than delegating to `0x004d91a0` — so `0x004da0b0` is not a pure dispatcher, it also
     performs its own direct transition on this one path.
   - Along the way, when current mode is `Idle` (0) and `field_0x4a0` bit 3 is clear and `[human+0xe8]` bit
     `0x200` is clear, and insufficient time (`0x00dab5e0()` vs a tunable) has passed, it declines outright.
   - Every reachable success path converges on one shared tail (`0x004da792`) that packs the final flags
     byte and calls `0x004dd380(human, 13, &block)`.

**Net:** `Flee` is the richest of the 7 — a full multi-candidate escape-point search with an internal
redirect into `Cower`, a sub-routine (`0x004d91a0`) that is itself directly callable by apply for the
"already have a point" case, and two independent code paths that can call the real transition primitive
(the sub-routine, and the long path-search candidate inside the main evaluator).

---

#### 13.1.3 `Gunfire wary` — `0x004e8070(human, 0)` then `0x004dbd90(human, target)`

CONFIRMED by listing of both. **Important correction to the call-signature-only framing**: `0x004e8070`,
called first and unconditionally, **never itself transitions the human into mode 17.** The real `Gunfire
wary` entry is entirely inside `0x004dbd90`, called second and only if a target resolved.

**`0x004e8070(human, flag)`** (apply always passes `flag = 0`), in order:
1. `0x008addb0(human, 0)`: if true (owned), continue to the real branch below. If false, it checks the
   **same predicate a second time** (`0x008addb0(human, 0)` again, byte-identical call) — this second check
   is always false when the first was false (no observable side effect runs between them), so the branch it
   guards is dead code on this call path; it is reachable only from `0x004e8070`'s other callers with a
   different value already on their own stack frame, not analysed here. With `flag = 0` (apply's case) and
   not owned, the function falls through to building and sending a record via the same
   `0x004f5dd0`/`0x004f5f80`/`0x004f6420` helper family seen inside `Cower`'s and `Flee`'s own bodies
   (HYPOTHESIS: a debug/telemetry log of the attempted action, tagged with small integer codes that do not
   correspond to any mode or action id in range — not followed further) and returns, **with no mode
   change**.
2. If owned: `0x00853b30(human, 0x21)` — the already-named "is a player" test (§1 item "Idle begin" etc.).
   - **Not a player**: draws a weapon if needed (`0x00600720(13,1,0,0)`), runs several checks
     (`0x0097cdd0`+`+0x1c9a` bit 0, a float compare on `+0x9b0` against a tunable, `0x009bd730`,
     `0x00943ea0`), resolves a "threat" object (falling back to `0x004e58a0` on `+0x1330` if none was
     already supplied), and then: **if in a vehicle, requests `Passenger` (25) via the request layer
     `0x004e4130`**; else if force-flag `always_cower` (`+0x2b9` bit 7) is set, **requests `Cower` (9)** via
     `0x004e4130` and also calls `0x004dd990(human,0,0,0,0,0)`; else checks `0x004f3a90`/`0x004f50b0`. None
     of these paths call the transition primitive directly — they stage *requests* (§3.1), to be picked up
     on a later apply pass. If a target resolved, it is also forwarded to `0x00505080(&human+0x510,0,
     target)` and the per-mode `+0x2b0` scratch is updated with the target's position.
   - **Is a player**: after several decline checks mirroring the acceptance test (§3.4) almost exactly
     (`Stunned`-lock, `0x0060b530` captured check, `+0xcc8` in {5,6,7}, in-vehicle restricted to the §3.4
     whitelist via `0x004dd410(0x2b)`, and a pending-switch-to-`Stunned` guard), it **directly writes the
     pending-request fields apply itself reads**: clears the pending target handle
     (`0x004e3830(&human+0x2b0, null-handle-literal 0x01115d50)`, the exact helper+literal §3.2 cites for
     `Idle`'s own null-handle clear), sets `+0x317` bit 3 (pending switch) again, clears `+0x44c` bits 0/1,
     and **sets `+0x504` (requested mode) = `0x2b` = 43 = `Player`** — then calls `0x008addb0(human,0)` once
     more and ignores the result entirely before returning. **For a player human, `Gunfire wary`'s own entry
     hook stages a request for `Player` instead and never reaches mode 17 at all.**

**`0x004dbd90(human, target)`** (target is the already-resolved object pointer, not a raw handle),
CONFIRMED by listing — this is where mode 17 is actually entered:
1. Null/self/class checks (human null, class gate, target null, `human == target`) all decline.
2. `0x008addb0(human,0)`: not owned → build+send the `0x1b` network message (tag byte `0x11` = 17 =
   `Gunfire wary`, target handle only, no extra flag bytes) and decline.
3. Owned: decline if `0x0097e280(human)` (follower, §9's own naming) holds; decline if in a vehicle
   (`0x009b9160`); decline if force-disable bits `+0x2b9` bit 3 or `+0x2b8` bit 4 are set; if
   `0x008c01b0(human)` holds, call its paired handler `0x008c0240(human)` and still decline (the same
   paired-helper pattern reused by `Veh evade`, §1.6).
4. Otherwise: build a watch-stance point (`0x004dbc00`), read the target's position, and call
   **`0x004dd380(human, 17, &block)`** for real. Returns true.

---

#### 13.1.4 `Passenger` begin `0x0051ed00(human)`

CONFIRMED by listing (ends `0x0051eeec`). Takes only the human — no target/flags argument, matching the
call-signature-only table exactly.

1. **Action-level pre-check, ahead of the usual preamble**: if the human's *current committed action*
   `+0x445` equals `0x32` (50 decimal — `vehicle extract`, §11.3 F), **decline outright**. This is a direct,
   independently checkable cross-reference to the action table rather than to mode state.
2. `0x00853b30(human, 0x21)` ("is a player"): **if true, call `0x004f13a0(human)` — the `Player` begin
   (§3.2) — and return exactly what it returns.** `Passenger`'s begin redirects players into `Player`
   outright; it never runs its own logic for a player human.
3. Non-player: `0x008addb0(human, 0)` for ownership; if false, **also** checks a second, argument-less
   global predicate `0x00889d10()` — if *either* holds, continue to the real gate; only if *both* are false
   does it build+send the `0x1b` network message and decline. HYPOTHESIS: `0x00889d10` is a global
   "single-player game" (or similar session-mode) flag that makes the per-object ownership test moot.
4. Real gate: resolve a handle pair at `[human+0x16c0]`/`[human+0x16c4]` via `0x004dcf00(lo, hi, 0)`
   (HYPOTHESIS: "the vehicle/seat the human is entering"). **If it fails to resolve, call
   `0x004e8070(human)` — the same `Gunfire wary` pre-hook examined in §1.3 — as a side effect, then
   decline.** This is a second, independent confirmation that `0x004e8070` is a general-purpose reaction
   helper reused outside the `Gunfire wary` mode, not mode-17-specific.
5. If it resolves: sets the *current action* `+0x445 = 0x30` (48 — `vehicle passenger`, §11.3 G), resets the
   live mode-parameter area at `+0x450` from a fixed global template `0x0012e14e0` (`0x58` bytes, matching
   the template-block concept already named by §4.1), builds a one-bit flags byte from an elapsed-time
   check against tunable `0x012a2ff0`, and calls **`0x004dd380(human, 0x19, &block)`** (`0x19` = 25 =
   `Passenger`). Returns true.

---

#### 13.1.5 `RollerBlader` begin `0x00530530(human, target)`

CONFIRMED by listing (ends `0x005305d4`) — the simplest of the 7.

1. Ownership check, but via **`0x008ae480([human+8], [human+0xc], 0)`** rather than `0x008addb0` — this is
   the exact "is its own handle locally owned" helper §3.2 already cites for `Stunned`'s begin. **If false,
   it declines outright, with no network-forward fallback** — the one begin among the 7 that skips the
   `0x1b`-message convention entirely.
2. `edx = [human+0xf0]` (the AI sub-object, already a named concept per §11.3 B/E's "AI sub-object"
   references); decline unless bit 6 of `[edx+0x48]` is set. **This is the exact same gate byte §11.3 G's
   action 68 `roller blader change` (`0x005303c0`) already documents** — a confirmed cross-check, not a
   coincidence.
3. Calls `0x009659f0(human, target)` purely for its side effect (the return value is explicitly discarded).
   HYPOTHESIS: "snap to/attach to a skate path."
4. Builds a parameter block of **four `-1` sentinel dwords and a zero word** — no target handle, no flag
   byte, unlike every other begin in this unit — and calls **`0x004dd380(human, 0x1e, &block)`** (`0x1e` =
   30 = `RollerBlader`). Always returns true past the two gates above.

---

#### 13.1.6 `Veh evade` begin `0x005232a0(human, [human+0x458], [human+0x45c])`

CONFIRMED by listing (ends `0x00523642`, the longest and most heavily-gated of the 7). The two stack
arguments are the low/high dwords of a handle stored at the human's own `+0x458`/`+0x45c` — the same field
pair `Human shield`'s begin (§3.2) stores its victim handle in, confirming `+0x458` is a shared
general-purpose "linked object" slot, not mode-specific.

1. Preamble + `0x008addb0`-not-owned → network-forward (tag byte `0x27` = 39 = `Veh evade`, target handle
   only) and decline.
2. Owned: a long decline chain, each term independently gating —
   - target resolve `0x004dcf00(lo, hi, 0)` fails → decline;
   - in a vehicle, either variant (`0x009b9160` or `0x009b91e0`) → decline;
   - `0x00a00560(human)` (the same helper `Cower`'s gate uses, §1.1 step 5) → decline;
   - `0x009b9210(human)` (the "entering a vehicle" sub-state selector §2.5 already names) → decline;
   - `[human+0xe8]` bit `0x1000` set → decline;
   - `0x009422d0(human)` (the specific brute-subtype test §11.3 G's `brute qte player` also reads) →
     decline;
   - `0x008c01b0(human)` holds → run its paired handler `0x008c0240(human)` (the same pair `Gunfire wary`'s
     begin uses, §1.3) → decline;
   - `0x00614d00()` (the already-named game-mode switch) == 2 **and** `0x0097e9b0(human)` holds → decline.
3. Build an escape point via `0x00da38e0(&human+0x40, &outbuf)`, the same per-object `+0x40` sub-vector and
   helper `Flee`'s own tail uses (§1.2); read the FOV/arc tunable `0x01187ecc` (the same "random 2.0–3.0"
   tunable §11.3 B's `move close in` and `Flee` both already use) and `[human+0xb40]` (the same field
   `Flee` reads too); run the navmesh query `0x00a12180(&outbuf, 0x0130c230, [human+0xb40], …)` (the same
   query `Flee`, §11.3 D's `brute bull rush`, and family D all independently reach) — fails → decline.
   **This query always fails in the shipped game (§14); unlike `Flee`, `Veh evade` has no other escape-
   point candidate at this step, so this clause's "decline" is the behaviour's actual, permanent,
   retail-shipped outcome, not a narrowing.**
4. A current-mode/substate gate: if current mode is `Idle` (0) and substate `+0x500 == 0x26`, decline; if
   current mode is `Idle` (0) and `[human+0xe8]` bit `0x200` is set, call `0x00511010(human, 1)` and
   decline; if current mode is `Idle` (0) and an elapsed-time check against tunable `0x0114a1560` has **not**
   yet passed, arm a randomized back-off (`0x00d9e180`, 3000 ms range / 1500 ms jitter) on the human and
   decline.
5. Otherwise: a second elapsed-time check (tunable `0x0114a1564`) and two vehicle-state tests on the target
   object (`[target+0x163c]` bit 5, `[target+0x163d]` bit 0, else `0x00ad31a0(target)` — the same vehicle
   state-byte family §11.3 F's `brute car flip` already documents) pack a 2-bit flags byte; a lookup
   `0x00ab5070(0, target, &vec, handle_lo, handle_hi)` can override the position fields if it resolves.
6. **Uniquely among the 7**: before calling the transition primitive, the function performs its own
   explicit `0x24`-dword (`0x90`-byte — the *exact* size §3.3 names for the live mode-parameter-block copy)
   `rep movsd` directly into `[human+0x8d0]`, and only then calls **`0x004dd380(human, 0x27, &block)`**
   (`0x27` = 39 = `Veh evade`) with a block pointer that appears to alias the same bytes just copied. HIGH,
   not fully resolved (OPEN item 2 below): whether this is a harmless duplicate write of identical data
   reaching `+0x8d0` by two routes, or the manual copy does work the primitive's own §3.3-step-3 copy would
   not otherwise do.

---

#### 13.1.6a A second "linked object" slot: `+0xda8`/`+0xdac`, the interaction-partner handle (CONFIRMED by listing, 2026-10-07)

`+0xda8`/`+0xdac` is a second, independent handle-pair "linked object" slot on the character object
— structurally identical to, but distinct from, `+0x458`/`+0x45c` (§13.1.6): `0x0052adc0` tests a
candidate object's handle against *both* pairs in the same function, for two different purposes,
proving they are tracked separately.

**Writers (3, all CONFIRMED by listing):**
- `0x00954340`, the character base constructor (`spec-lua-api-behaviour.md` §34.1), default/null-
  initializes `+0xda8`/`+0xdac` together with five sibling handle-pair fields (`+0xd60`/`+0xd64`,
  `+0xd78`/`+0xd7c`, `+0xd80`/`+0xd84`, `+0xd88`/`+0xd8c`, `+0xdb0`/`+0xdb4`) from one fixed sentinel
  pair, and `+0xda0`/`+0xda2` to `0xffff`.
- `0x0095d970` (one argument): the clear/release routine — resets `+0xda8`/`+0xdac` (from a second,
  different sentinel pair) and `+0xda0`, after touching the object's own animation-controller
  sub-object (`+0xd24` → `+0x10`). Called twice in a row, once per side of a pairing, by `0x0095f250` —
  the generic mutual-unlink entry point (17 callers across 14 functions).
- `0x0095fc00` (two object arguments plus a small interaction-type index): the mutual link-setter.
  After releasing any existing grab on either side (`0x00971cf0`/`0x009da5f0`, §11.6's "held in a grab"
  pair) and checking the two are not already linked (`0x00456920`), it writes each side's own handle
  into the other's `+0xda8`/`+0xdac`, and a role byte (0 / 1) into each side's own `+0xdb8`. Called from
  8 otherwise-unidentified sites — a shared, generic utility, not feature-specific.

**Readers**, besides action 15 (§11.3 C): `0x0052adc0` (a "is this candidate already my partner"
check, called only from `0x0096dcd0`), and `0x0096dcd0`/`0x009a02a0` directly (a small
evaluate/maintain-partner chain, independent of action 15's own code path). Resolution always goes
through the generic handle resolver already used for other handle-pair fields in this spec
(`0x004d7f30` here), and the resolved object's own `+0x40` position sub-vector (§13.1.6) is what gets
used — never the handle or the object's identity directly.

**What it links:** every write stores the *other character's own handle* (its own `+8`/`+0xc` pair,
§13.1.5's "own handle" convention) — i.e. `+0xda8` holds the handle of **the other participant in a
two-character grab/hold interaction**, not a prop, weapon, or cover node. Applied back to action 15
(§11.3 C): when T is in the `Fire spray`-tagged pose 20/21 (`0x009826e0`, §13.3 below), the ally
backs off toward the position of whoever T is currently linked to, not toward T itself — "if the
target is in the middle of a two-person grab/hold interaction, don't crowd the other participant
either." HYPOTHESIS, not fully closed: the exact gameplay label for this interaction (hostage hold,
takedown, execution, shield grab, etc.) and the precise meaning of the `+0xdb8` role byte (0/1) and
the sibling pair `+0xdb0`/`+0xdb4` are open. See §13.1.6b, immediately below, for the `0x009826e0`
pose test action 15 gates this on.

#### 13.1.6b The `0x009826e0` state test (`+0xcc8`/`+0xcce`), used by action 15 (CONFIRMED by listing, 2026-10-07)

Full disassembly (12 instructions): null-check the argument; compare the dword at `+0xcc8` against
`0xc` (12); if equal, read the byte at `+0xcce` and return true iff it is `0x14` (20) or `0x15` (21).
**`+0xcc8` is the character's current AI mode id — §6's own 44-row table, row 12 is `Fire spray`**
(re-confirmed against that table with 0 mismatches). **`+0xcce` is a genuinely independent byte from
`+0xcc8`**, confirmed by `0x004dd640` (the already-flagged "cover posture family" open item), which
reads the *adjacent* byte `+0xccd` (not a slice of `+0xcc8`) against `9`, then `+0xcce` against
`0xf`/`0x10`/`0x12` (15/16/18) for a *different* purpose — the two bytes are a true independent pair
read together by some code and separately by others. A sibling function, `0x00982650`, tests
`+0xcce`∈{20,21} alone (no `+0xcc8` check), then indexes a per-object record (`+0x15cc`, sentinel
`-1`) into a runtime-populated table (base `0x0262ba4c`, stride `0x88`) — independent evidence pose
20/21 is a recognized pair consumed by more than one system, keyed off a per-object "which
interaction record is active" index, not an incidental Fire-Spray-only animation flag; the table's
own row contents could not be recovered statically (zero-filled in the image). Callers of
`0x009826e0`: action 15 itself, mode 23 `Melee`'s own exit/tick functions (`0x00551cc0`/`0x00551cf0`,
§6 row 23 — a second, independent confirmation this state matters in melee/interaction contexts),
plus four unidentified call sites not traced further.

---

#### 13.1.7 `Veh threat`'s own call `0x00523db0(human, target_lo, target_hi, 1)`

CONFIRMED by listing (ends `0x00524039`). Note: §3.2 already documents, and this unit did not need to
re-derive, that `Veh threat`'s case in apply **always** falls through into the `Dbno` begin `0x0050bd40`
afterward regardless of whether this call runs at all; this entry covers only `0x00523db0` itself. The
4th argument (`[ebp+0x14]` inside the function) is a literal `1` at this one call site; the function
itself also supports a `0` value, exercised only from other, unexamined call sites.

1. Preamble + `0x008addb0`-not-owned → network-forward (tag byte `0x28` = 40 = `Veh threat`, target handle
   only) and decline.
2. Owned: decline if `0x009b9210(human)` ("entering a vehicle") holds.
3. A conditional virtual-dispatch gate that is **skipped entirely** (treated as non-blocking) unless every
   link resolves: a global lookup `0x00614cf0()` → non-null, **and** `0x00680db0()` → true, **and**
   `[result+0x1c]` → non-null, **and** `[that+0x40]` → non-null. Only then are two virtual calls made
   through that object's own vtable (slots `0x68` and `0x70`); the first gates whether the second even
   runs, and if the second's return value equals the human itself, **decline**. HYPOTHESIS, not resolved:
   plausibly a "don't flag vehicle-threat on whatever the camera/possession system currently focuses,"
   flagged OPEN (item 3).
4. Resolve the target handle via `0x004e4ce0(target_lo, target_hi, 1)`; fail → decline.
5. `0x004e7200(human, target)` → a relevance predicate (its result is used differently depending on the
   4th argument, below).
6. With the 4th argument nonzero (apply's own call, always `1`): decline unless step 5's predicate holds;
   then decline unless `0x004e9110(human)` holds; then decline unless `0x009d8ca0(human, target)` holds
   (thiscall). Passing all three sets an internal flag `bl = 1`.
   *(With the 4th argument zero — not exercised by apply's call, but present in the listing: if step 5's
   predicate holds, an elapsed-time check against tunable `0x0111643c` can set `bl = 1` directly; otherwise
   a fallback predicate `0x00523a50()` must hold or the call declines, with `bl` left `0`.)*
7. Packs a 2-bit flags byte from `(arg4 != 0)` and `(bl != 0)`, writes the target handle into the block, and
   calls **`0x004dd380(human, 0x28, &block)`** (`0x28` = 40 = `Veh threat`). Returns true.

---

### 13.2 The two thrown-weapon trajectory solvers

**Reuse finding (the headline result of this part).** `0x0054c770` (action 5 `throw weapon`'s solver) and
`0x0054d930` (action 4 `throw grenade`'s solver) are **not two independent trajectory solvers**. Both reach
the *same* core ballistic-angle routine, `0x0054c630` — confirmed by reading `0x0054d930`'s own listing,
which contains a direct `call 0x0054c630` (at `0x0054da4d`) built from the same six-argument shape
`0x0054c770` itself tail-calls into. `0x0054c770` is a thin, 42-byte shim: it reads the thrower's class via
the already-named `0x00853b30(human, 0x22)` test (a *different* bit than the already-confirmed "is a
player" bit `0x21` used elsewhere in this file — close, but not provably the same test; flagged OPEN) to
choose one of two speed tunables (`[weapon_info+0x350]` vs `[weapon_info+0x34c]`) out of the held weapon's
info struct (`[weapon+0x19c]`, the same held-item sub-struct offset this unit's `Gunfire wary` write-up
also reads), reads a third tunable (`[weapon_info+0x35c]`, an "arc" value) from the same struct, and
forwards all of it straight into `0x0054c630`.

#### 13.2.1 The shared core `0x0054c630(outA, outB, originPoint, targetPoint, arc, speed)` — cdecl, 6 args

CONFIRMED by listing (ends `0x0054c760`, no frame, pure `esp`-relative). `originPoint`/`targetPoint` are
pointers to 3-float vectors; `arc` and `speed` are plain floats passed **by value** (one stack dword each,
not pointers); `outA`/`outB` are pointers to 3-float output buffers.

**Closed-form reading (static, CONFIRMED structure / HYPOTHESIS on exact trig identity):**
1. `delta = targetPoint − originPoint` (component-wise, computed in double precision then rounded back to
   float for each of the 3 components).
2. `0x00da0730(&delta)` — reads `delta.x² + delta.z²` (the **horizontal-plane** length-squared; `delta.y`
   is not part of this test) and, **only if that is ~zero** (checked via a shared "is approximately zero"
   helper `0x00dad830`), overwrites `delta` with a fixed default `(const, 0, 0)` direction. In every other
   case `delta` is left exactly as computed in step 1 — this is a degenerate-input guard, not a true
   vector-normalize.
3. `speed` and `arc` are handed, together with a cross-product-based local basis built by
   `0x0054c1d0`/`0x00da5aa0` (the latter a Vector3 cross-product utility with its own degenerate-case
   fallback `0x00da57b0` — a reusable math-library routine, not specific to this solver) against a 3-float
   global reference `0x013213e4`, to solve for a launch elevation angle. The actual angle value is run
   through two CRT transcendental calls, `0x00ea2470` and `0x00ea3a70` (both exhibit the standard
   SSE2-vs-x87 CRT dispatch preamble; which specific trig pair they resolve to was not pinned down
   statically — see the oracle data below, which settles the *geometric effect* precisely even without the
   exact function names).
4. A matrix/basis transform `0x00d9fb90` rotates the resulting local (elevation-angle) 2-vector into the
   horizontal basis defined by `delta`, writing a full 3-float result into **both** `outA` and `outB` via
   two structurally identical passes over two different local structures.
5. Returns a bool in `AL` (propagated from the very first sub-call, `0x0054c1d0`) — `0x00540c760`'s own
   upper 24 bits of `EAX` are not meaningfully set (compiler contract: callers only read `AL`).

**Empirical confirmation — Unicorn oracle, `conv=cdecl`, cold full-image emulation, zero friction.** All 11
of the following ran to completion against the real function bytes with no stub needed (no OS/import call,
no runtime-populated global, lies on the path at all — confirmed by `Emu.va_is_file_backed` on every global
address the function reads: `0x1187ebc`-style tunables *and* `0x013213e4` are inside their section's raw
file bytes, not the zero-fill tail). `AL == 1` ("solved") on every case below; all vectors are the literal
floats read back from the emulated output buffers.

| Case | origin | target | arc | speed | outA | outB |
|---|---|---|---|---|---|---|
| flat, same height | (0,0,0) | (10,0,0) | 1.0 | 20.0 | (0.999922, −0.012502, 0.0) | (0.012501, −0.999922, 0.0) |
| target higher (+2) | (0,0,0) | (10,2,0) | 1.0 | 20.0 | (0.982950, 0.183874, 0.0) | (0.012470, −0.999922, 0.0) |
| target lower (−2) | (0,0,0) | (10,−2,0) | 1.0 | 20.0 | (0.978046, −0.208390, 0.0) | (0.012532, −0.999921, 0.0) |
| closer (5 m) | (0,0,0) | (5,0,0) | 1.0 | 20.0 | (0.999980, −0.006250, 0.0) | (0.006250, −0.999980, 0.0) |
| farther (20 m) | (0,0,0) | (20,0,0) | 1.0 | 20.0 | (0.999687, −0.025007, 0.0) | (0.025008, −0.999687, 0.0) |
| slower speed (10) | (0,0,0) | (10,0,0) | 1.0 | 10.0 | (0.998746, −0.050063, 0.0) | (0.050063, −0.998746, 0.0) |
| faster speed (40) | (0,0,0) | (10,0,0) | 1.0 | 40.0 | (0.999995, −0.003125, 0.0) | (0.003125, −0.999995, 0.0) |
| arc = 9.8 | (0,0,0) | (10,0,0) | 9.8 | 20.0 | (0.992352, −0.123444, 0.0) | (0.123444, −0.992352, 0.0) |
| arc = 0.0 | (0,0,0) | (10,0,0) | 0.0 | 20.0 | (1.0, 0.0, 0.0) | (1.0, 0.0, 0.0) |
| target along Z not X | (0,0,0) | (0,0,10) | 1.0 | 20.0 | (0.0, −0.012502, 0.999922) | (0.0, −0.999922, 0.012501) |
| non-origin start | (5,1,5) | (15,1,9) | 1.0 | 20.0 | (0.928393, −0.013464, 0.371357) | (0.012501, −0.999909, 0.005000) |

**What the numbers confirm, past the closed-form reading above:**
- `outA` and `outB` are always unit-length 3-vectors.
- `outA`'s horizontal plane (X,Z) component is always exactly proportional to the horizontal direction from
  origin to target — e.g. the "non-origin start" row's horizontal delta is (10, 4), magnitude 10.77, and
  `outA.x/outA.z` = 0.9284/0.3714 = the exact unit vector of (10,4). Confirmed to full float precision in
  every row.
- `outB`'s (X,Z) pair is `outA`'s (Z,X) pair, sign-adjusted — i.e. `outB ≈ (outA.y·(horizontal-unit), −outA.
  "elevation-cosine"·…)`; empirically, `outB`'s first and third components equal `outA`'s second component
  scaled onto the horizontal unit direction, and `outB.y = −(outA`'s own horizontal magnitude`)`. This is
  the exact numeric signature of two complementary ballistic-angle roots (θ and 90°−θ) for a flat-ground
  range equation. **Only `outA`'s buffer is copied into the committed action block** by the real `throw
  weapon` caller (`0x00553459`'s own code copies `[esp+0x50..0x58]`, matching `outA`'s stack slot, into the
  committed block's `+0x24`/`+0x30`/`+0x38` fields exactly as §11.3 H already documents) — `outB` is
  computed every call but, at this call site, is never read back. HYPOTHESIS (OPEN item 4): whether `outB`
  is dead weight here or consumed by some other caller of `0x0054c630` not examined in this unit.
- Increasing `arc` increases the magnitude of `outA`'s vertical (Y) component monotonically (confirmed
  0.0/9.8 → 0.0/−0.123 at fixed distance+speed), and increasing `speed` at fixed distance/arc *decreases*
  the vertical component toward 0 (confirmed 10/20/40 → −0.0501/−0.0125/−0.0031) — both exactly the
  qualitative behaviour a real ballistic-range solve should have (a faster throw needs less arc; a bigger
  gravity/arc constant needs more). The counter-intuitive *sign* (more arc → more **negative** Y) is read
  plainly as recorded; whether this frame's Y axis is gravity-up or gravity-down, or whether the caller
  re-flips it before use, was not traced past this function and is left OPEN (item 5).

**Direct oracle run on `0x0054c770` itself (the exact address the task and §11.3 H cite).** The function's
own class-check call, `0x00853b30`, dereferences a per-type descriptor table at `0x02cc9900` that is
**confirmed NOT file-backed** (`Emu.va_is_file_backed` returns `False` for every row checked, and for the
table's own address) — the same WALLS.md-class runtime-populated-table issue already on record for a
different function, now independently reconfirmed for the table this unit's entire Part 1 class-gate also
depends on. Calling `0x0054c770` cold with a synthetic human object faults cleanly inside `0x00853b30`
(`EmuFault` at `0x00853b58`, dereferencing a null type-descriptor row) rather than silently returning a
wrong answer — an honest failure, not a misleading one. **Resolved by stubbing the one call that touches
that table** (`emu.stub(0x00853b30, eax=1 or 0)`), disclosed here as a stub, not a result: with the stub
forced to "is player" `(eax=1)` and the weapon's player-speed tunable set to `999.0` vs non-player tunable
`20.0`, `0x0054c770` returns `outA=(1.0, 0.0, 0.0)` (so close to flat-trajectory that the 999-speed throw
needs no visible arc); forced to "not player" `(eax=0)`, it returns `outA=(0.999922, −0.012502, 0.0)` —
**the exact same vector**, to full float precision, as the `0x0054c630` direct-call "flat, same height"
case above (speed 20.0, arc 1.0). This proves end-to-end, with only the external class-table dependency
stubbed and disclosed, that `0x0054c770` really is nothing more than "pick a speed tunable by class, then
call `0x0054c630`."

#### 13.2.2 `0x0054d930` — the grenade solver, same core plus obstacle screening

CONFIRMED by listing (partial — the function is large; read through its first ~280 instructions, enough to
confirm the call into the shared core and the block-write tail; the remainder, further geometry after the
solve succeeds, was not fully read — see OPEN item 6). Called thiscall-style from `0x0054dce0` (`throw
grenade`) at `0x0054ec58`, with `ECX` = a large working struct the caller has been building across its own
1822-instruction body (matching §11.3 H's "it collects allies to avoid … it tries aim points in turn"), and
three stack arguments: the committed action block pointer, a value read from the working struct, and a
second local buffer.

1. Calls `0x0054d180` first (a precondition this unit did not read further into); false declines
   (jumps to the same decline target the rest of the function shares, `0x0054dcb0`).
2. **Obstacle/ally screening, confirmed by listing**: walks a list inside the working struct — base
   `[this+0x20]`, stride `0x10` bytes, count `[this+0x28]` — and for each entry, computes a squared
   horizontal distance between the candidate aim point and the entry's position, SIMD-compares all three
   axes against a per-call radius `[this+0x1d0]`, and **declines the whole candidate** (same shared target,
   `0x0054dcb0`) if a within-radius hit is found on *any* list entry (`movmskps == 0xf`, i.e. the compare
   was true on every one of the 4 packed lanes used). This is the concrete mechanism behind §11.3 H's prose
   "it collects allies to avoid" — now specified as a per-candidate SIMD proximity reject, not previously
   broken out.
3. Calls the **shared core `0x0054c630`**, with one input point read from the working struct's own `+0`
   offset (CONFIRMED: `[this+0]`/`[this+4]`/`[this+8]` are read directly as a 3-float vector right before
   the call) and the other from the caller-supplied candidate buffer (`EBX` at entry, i.e. the current
   candidate aim point — `P`, the cover object's position, or a nearby object's position, exactly matching
   §11.3 H's "it tries aim points in turn" list); the `arc` tunable is read from `[weapon_info+0x35c]`
   (`weapon_info` itself from `[this+0x1f8]`) — the **same offset** `0x0054c770` reads for the thrown
   weapon's arc, confirming both solvers pull the arc constant from the same weapon-class sub-struct
   convention; the `speed` tunable is read from `[this+0x1fc]` directly (no player/non-player branch here —
   grenades apparently use one fixed throw speed regardless of thrower class, unlike the weapon-throw path).
   `0x0054c630` failing (`AL == 0`) declines the candidate (`0x0054dcb0`).
4. On success: a further geometry pass (`0x0054d5b0`, `0x00da38e0`) refines the result, and the function
   writes the final launch data into the **committed action block** at `+0x30`/`+0x38` (matching §11.3 H's
   own "`+0x30`…`+0x38` = the launch data" exactly) and, separately, into `+0x24`/`+0x2c` (matching the
   `+0x24 = P` convention the weapon-throw case also uses) before returning true.

**Oracle note for this solver.** Because `0x0054d930` is reached only via a richly-populated working struct
(ally/obstacle list, cached origin position, weapon-info pointer, per-call radius — none of it a single
self-contained argument block), synthesizing a faithful Unicorn call for `0x0054d930` itself would mean
hand-building that entire struct with made-up list contents rather than exercising real game state — a much
weaker oracle than directly exercising the shared core. Given `0x0054d930` is confirmed (by listing, not
inference) to delegate its actual trajectory math to the *same* `0x0054c630` already oracle-tested in full
in §2.1, this unit treats §2.1's 11-case oracle run as covering grenade's trajectory math as well, and
limits itself to static confirmation of grenade's own extra logic (obstacle screening, block-write offsets)
rather than building a second synthetic harness around it. This is a deliberate scoping choice, not a
friction report — flagged for the record in the companion open-items file (item 6) in case the orchestrator
wants a direct `0x0054d930` run from real captured game state instead.

---

## 14. The real navmesh/path-point query layer, and why it is dead in every shipped file (2026-10-07)

**🚨 LIVE-REFUTED, 2026-10-08 — do not implement this section's "always dead" conclusion as written; see §14.7a.** A live Frida capture against the real running game (not a static trace) found the registry this section calls dead is actually populated and functioning in real gameplay: of 87,918 real calls to `0x00447770` during one captured street gunfight, only 656 (0.75%) returned the `-1` failure sentinel this section's entire argument rests on — the other 87,262 (99.25%) returned a real, non-sentinel value, and the final path query (`0x00a12a80`, §14.7) returned success 10 of 13 times, closely time-correlated with real cover-selection attempts. **The "exhaustive whole-binary call-site census" below (§14.3) that concluded "no second, alternate... writer... anywhere in the binary" was wrong or incomplete — a real writer exists that it did not find.** The static disassembly below is kept, struck through where superseded, not deleted, per project practice — it correctly describes the registry's SHAPE and the reader's own logic, just not its real population state in retail. §14.7a has the live evidence; the search for the real writer is IN PROGRESS, not yet concluded.

**Scope.** A direct follow-up to `spec-zone-data-format.md` §12 (the `.czn_pc` Navmesh section), which
proved that section's load gate reads `0` in all 1,647 real occurrences across the full 2,971-file
shipped population — real wiring to the runtime navmesh-cell API (`0x00446c60`/`0x00445980`), but
switched off in every shipped file. This section traces where the shipped game's NPC
pathfinding/destination-point data actually comes from, given that the obvious file-backed candidate
is dead.

### 14.1 One previously-suspected entry point is not a navmesh query at all

`0x004e91c0` — read in full (203 bytes, `ret`+`int3`-confirmed boundary) — is a plain "is point P
within my own post radius (`+0x2f0`) of my own post position (`0x004e7d10`)" boolean gate, reused by
unrelated action preconditions (actions 22/23, §11.3 B/C). It performs no cell/connectivity lookup,
consults no per-zone registry, and produces no destination point — it only tests a point the caller
already has. All 6 real call sites project-wide push the identical 3-argument boolean-test signature.
**This is a dead end, cleanly refuted by direct disassembly** — not a navmesh/path query, and not
something that leads into cell/connectivity data when traced backward.

### 14.2 The real cross-cutting query: `0x00a12180` and its "snap" sibling `0x00539200`

`0x00a12180` (90 real call sites project-wide — the core AI module, the melee/cover/reload cluster, a
large cluster overlapping the untraced "human navigator chain" `0x00993e90`/`0x00993630`, and a
vehicle/traffic-adjacent cluster) builds a packed cell key (`zoneId<<16 | cellIndex`, the same
convention `0x00446c60` consumes) and calls `0x00447770` (§14.3). On success it resolves an actual
output position via `0x00444f80`; **on failure, it silently echoes the caller's own original input
position back unchanged** — a soft, non-fatal fallback. Its thin sibling `0x00539200` ("the navmesh
point snap," already cited by this document for `Flee`/`Veh evade`) calls `0x00a12180` internally but
**on failure returns false with no output written at all** — a hard, silent "nothing happened" to its
own caller, unlike `0x00a12180`'s own soft fallback.

### 14.3 Both bottom out in the exact registry `spec-zone-data-format.md` §12 already proved is dead

`0x00447770` (926 bytes, read in full) runs three internal strategies in order — a direct cell-key
lookup, a "neighbor of this cell" walk, and a full fallback scan over every registered zone — all
three independently reading the identical per-zone registry (`0x3171a58` count / `0x3171a5c` array,
16-byte stride, mode-0 presence bit at `+2`, mode-0 data pointer at `+4`) that `0x00447160`
(`spec-zone-data-format.md` §12.3) writes. ~~Every strategy fails identically because the registered-zone
count is always zero; the function's own shared failure exit executes a literal `-1` sentinel.~~ **LIVE-REFUTED, 2026-10-08 (§14.7a): the registered-zone count is NOT always zero in real gameplay — the live capture shows `0x00447770` succeeding 99.25% of the time. This static reading of the function's own logic (three strategies, shared `-1` failure exit) stands; the claim that the gate is always zero does not.**

~~**Exhaustive whole-binary call-site census, closing `spec-zone-data-format.md` §12.7 open item 2
outright:** `0x00447160` has exactly ONE caller in the entire executable (the already-known dead
`.czn_pc` Navmesh gate, `0x008642a0`), and its own slot-allocator `0x00447050` has exactly ONE caller
in the entire executable (`0x00447160` itself). **There is no second, alternate, runtime-generated,
or collision-geometry-fed writer into this registry anywhere in the binary** — this specific form of
"maybe it's built from collision geometry at runtime" is refuted by an exhaustive census, not merely
unobserved.~~ **LIVE-REFUTED, 2026-10-08 (§14.7a): this census was wrong or incomplete — a real writer
exists somewhere that it did not find (most likely blind spot: the census appears to have been scoped
to "callers of `0x00447160`/`0x00447050`" by name, not an independent raw data-xref search for the two
registry addresses themselves, which would also catch a bulk-copy-shaped writer that never references
the field by its own scalar offset). The real writer search is IN PROGRESS, not yet concluded — do not
cite "no second writer anywhere in the binary" as settled.** This generalises well beyond the two functions `spec-zone-data-format.md` §12 named:
`0x00446c60` (cell-resolve) has 44 real callers project-wide, while `0x00445980` (cell-connectivity)
has exactly 1 real caller in the whole binary (`0x004de5ed`, inside the already-documented "level
difference" helper `0x004de500`) — **this generalization is now also suspect pending the same real-writer search; not re-verified live this pass.**

### 14.4 A concrete, traced consequence: action 16 `move retreat` cannot be selected in retail

Action 16's own destination step (§11.3 B) calls `0x00539200`; its real call site inside `0x0053d540`
is `call 0x539200; test al,al; je 0x53d59c` — and `0x53d59c` is the function's own shared decline
exit, the same tail several of the action's other decline branches already fall through to. This
branch-target disassembly is correct and unchanged — ~~**Since `0x00539200` fails unconditionally in
every real shipped zone, this jump is taken every single time `move retreat` is evaluated, and the
action always declines before reaching its own threat-radius logic.**~~ **LIVE-REFUTED, 2026-10-08
(§14.7a): `0x00539200` ultimately depends on the same `0x00447770` gate the live capture shows
succeeding 99.25% of the time — "fails unconditionally" does not hold. `move retreat`'s real
reachability in retail is now OPEN, not confirmed-always-declines, pending the real-writer
investigation.** Other callers of the same query are NOT uniformly blocked the same way: `Flee`
(§13.1.2) and `Veh evade` (§13.1.6) already try several other, non-navmesh escape-direction candidates
before or alongside this query, so for those two a failed query removes one candidate from a list
rather than blocking the behaviour outright — a real difference in outcome severity between callers,
not catalogued exhaustively here. **STATUS, 2026-10-08: LIVE-REFUTED along with §14.7's two newer
findings — all three rested on the identical underlying fact (`0x00447770` always returns `-1`), now
shown false 99.25% of the time in real play. See §14.7a.**

### 14.5 The other three candidates for a real pathfinding data source, tested directly

- **A different file family/extension.** `spec-format-inventory.md`'s exhaustive resource-type census
  contains no unaccounted-for path/waypoint/navgraph type, and no other `.czn_pc` top-level
  content-kind id can feed the same registry (it would have to call `0x00447160`, and nothing but the
  Navmesh handler does). Dead end for this specific registry; not tested against a hypothetical,
  entirely separate parallel system with its own in-memory structures.
- **AI waypoint/path-node objects inside the Objects (`0x2234`) stream.** Not confirmed or refuted.
  Blocked by a pre-existing, structural limitation: concrete object class identities inside the
  runtime-populated `0x02cc9900` table are not statically recoverable from the cold image
  (`spec-zone-data-format.md` §11's own standing open item) — not something static disassembly from
  known addresses can get past. Left OPEN.
- **A traffic/road-spline network reused for general AI pathing.** `spec-tables-traffic-ai.md` §14.4's
  `Spline_Type` is a one-hot category bitmask for filtering distant/ambient vehicle spawns, not curve
  geometry, and scoped to vehicle traffic LOD — no evidence any human movement/combat action in §11.3
  reads it. Dead end for general NPC pathing, though not an exhaustive search for some other,
  entirely uncatalogued spline/curve format.

### 14.6 Net conclusion

~~**Shipped NPC pathfinding does not get real cell/connectivity data from anywhere.** Every concrete
"navmesh" touchpoint this project has found — the raw cell-resolve/connectivity API and the much more
widely used point-query/snap layer on top of it — shares one single registry with an exhaustively-
confirmed single writer, and that writer is unreachable in all 2,971 real shipped `.czn_pc` files.
The handful of behaviours that strictly require a successful navmesh query (`move retreat`, action 53
`brute bull rush`'s final clause, `Veh evade`'s own escape step) are provably unreachable or
permanently refused in retail.~~ **LIVE-REFUTED, 2026-10-08 (§14.7a).** The registry IS populated in
real gameplay (99.25% success rate on 87,918 real calls during one captured gunfight) through a writer
this section's own static census did not find. The behaviours listed as "provably unreachable" are
not — `move retreat`'s and action 8 `cover take def`'s real reachability in retail is confirmed
working at least some of the time by the same capture (§15.7's own correction). **What's still true,
unaffected by this correction:** most of ordinary NPC movement never touches this registry at all — it
already computes destinations directly from target/post/cover positions through non-navmesh helpers —
so for most "NPC movement," there never was a navmesh-backed path query to begin with; this reads as
direct/line-of-sight movement with the engine's own collision/physics layer (`spec-physics-runtime.md`)
presumably doing local obstacle handling for the cases that don't need the registry at all. One piece
remains open: who, if anyone, ever writes a non-sentinel value into a human's own `+0xb40` cell-key
field (a quick opcode scan found no candidate inside the AI address range — suggestive, not confirmed,
unaffected by this correction either way).

### 14.7a LIVE CAPTURE REFUTES "always dead" — real writer search IN PROGRESS (2026-10-08)

**A Frida capture against the real running game, requested by `purrsian-62` specifically to confirm
§14.7's own "statically dead, runtime confirmation pending" label before Team B implemented against
it, came back negative on the "dead" side — i.e. it refutes dead, not confirms it.** Hooked
`0x00447770` (the registry reader), `0x00a12a80` (the final path query), and entry-counters on
`0x0050a690`/`0x004f0b70`/`0x0053d540` (cover-select/pickup/retreat-dest attempted) during one real
street gunfight with armed enemies near cover. Results, from the raw log
(`scratchpad/live-capture-navmesh-registry_2026-10-08/navmesh_registry_live_log.csv`, 88,037 rows):

- **`0x00447770`: 87,918 calls. Only 656 (0.75%) returned the `-1` failure sentinel; 87,262 (99.25%)
  returned a real, non-sentinel value.** This directly contradicts §14.3's "registered-zone count is
  always zero" and the "exhaustive census, no second writer anywhere" claim built on it.
- **`0x00a12a80`: 13 calls, 10 returning success (`AL=1`).** Most follow a `cover_select_attempted`
  event within milliseconds — i.e. **`cover take def` (action 8) DOES resolve successfully in shipped
  retail**, contradicting §15.7's own 2026-10-08 correction (which is itself now superseded, see
  §15.7's in-place update).
- `cover_select_attempted` fired 15 times, `pickup_weapon_attempted` 90 times during the capture; the
  `move_retreat_dest_step_attempted` hook never fired in this particular fight (no data either way for
  `move retreat` from this capture — absence of the trigger condition, not evidence it still fails).
- **A known script bug in the same capture, NOT a confound on the above:** the registry-global value
  reads (`0x3171a58`/`0x3171a5c` themselves, meant to show the live count/array contents) failed with
  `"READ_ERROR: not a function"` — the script used the deprecated global `Memory.readU16(ptr)`/
  `Memory.readPointer(ptr)` API, which errored in this Frida version (17.17.0); the fix is the modern
  method-call style, `ptr.readU16()`/`ptr.readPointer()`. **This bug affects only the raw registry-value
  columns, not the `0x00447770`/`0x00a12a80` return-value hooks above, which use the Interceptor
  `retval` object directly and are unaffected.**

**Net effect: §14.3's "exhaustive census, no second writer anywhere in the binary" is wrong or
incomplete — a real writer exists that it did not find.** Most likely blind spot, not yet confirmed:
the original census appears to have been scoped to counting callers of the two named writer FUNCTIONS
(`0x00447160`/`0x00447050`) by name, rather than an independent raw whole-binary data-xref search for
the two registry ADDRESSES themselves (`0x03171a58`/`0x03171a5c`) — which would also catch a
bulk-copy-shaped writer (e.g. a `memcpy`-style population that never references the field by its own
scalar offset the way an individual store would), a previously-uncatalogued zone-content-kind handler,
or a runtime-built registry fed from collision geometry rather than the `.czn_pc` Navmesh section.
**A dedicated search for the real writer is IN PROGRESS as of this edit (agent `afac5d6c4a3774487`),
not yet concluded — this section will be updated again once it reports back.** Do not implement
against either "always dead" (§14's original claim) or a specific alternate mechanism (not yet found)
— the honest state right now is "demonstrably alive in real gameplay, real writer not yet located."

**For Team B specifically:** the practical, safe interim position is to implement reachability as
WORKING (per the live evidence: cover-taking succeeds, weapon-pickup is attempted frequently) rather
than as "always fails" — the opposite of §14.7's prior advice. A fully specified mechanism (what
actually populates the registry, and under what conditions) will follow once the real-writer search
concludes.

### 14.7b The real-writer search, two passes in — the count mystery is SOLVED, the presence-bit mystery is a confirmed, exhausted, STILL-OPEN contradiction (2026-10-08)

**Pass 1 found a real, previously-uncatalogued writer — the registry's COUNT is not file-driven at
all.** `0x004470a0(count, allocator)` — `0x03171a5c = allocator->Alloc(count<<4, 4, 0, 0); if
(0x03171a5c) { 0x03171a58 = count; memset(0x03171a5c, 0, count<<4); }` — called exactly once in
the whole binary, from `0x00a0f870` with a literal constant `count=8`, itself called from exactly one
place, `0x005d25f0` (a ~4,360-byte, 230+-callee monolithic engine-bootstrap dispatcher with zero
connection to `.czn_pc`/zone loading). **`0x03171a58` is unconditionally 8 from shortly after
process boot, independent of all level/zone content.** This directly falsifies §14.3's foundational
"registered-zone count is always zero" claim — the original census centered on the wrong functions
(`0x00447050`/`0x00447160`, which only search/mark an already-allocated array, never allocate it) and
never found the real allocator because it searched callers of NAMED functions rather than raw
data-xrefs of the registry addresses themselves — exactly the blind spot §14.7a's own "most likely
blind spot" paragraph predicted. Found via a literal-address xref query against `0x03171a58`/
`0x03171a5c` directly: **10 functions touch these globals, not 3.** Four more previously-uncatalogued
sibling READER functions were also found this pass (`0x00446930`, `0x004472f0`, `0x00447b10`,
`0x00447c50`, `0x0044bb70`) — none of them writers.

**Version/build mismatch, independently checked and RULED OUT.** The capturing agent recorded the
live-capture exe's MD5 (`d7557158ed721a428162b8e02467f95b`, 16,257,024 bytes); the orchestrating
session separately hashed the exe currently on disk at the known install path — **exact match.** The
live capture really was running the identical build every static address in this document is based
on; the contradiction below is not an artifact of analyzing a different binary than was running.

**Pass 1's own remaining contradiction, stated precisely:** the registry EXISTS (count=8, confirmed)
but re-tracing every path that could mark a slot "present" still comes up exhaustively dead — the one
function that ever sets a slot's presence bit, `0x00447160`, has exactly one caller, the already-dead
`.czn_pc` Navmesh handler (`spec-zone-data-format.md` §12, gate confirmed 0 in all real occurrences).
By this trace, `0x00447770` should return `-1` on literally every call — directly contradicting the
live capture's own retvals (99.25% non-`-1`), which decode into exactly the `(upperWord<<16)|index`
bit-shape only a real "slot present" success branch can produce (e.g. `0x60ee011d`) — not garbage,
not a misread.

**Pass 2, two more static checks, BOTH exhausted clean negatives — no further static avenue remains
for this specific contradiction.** (1) `0x00447050`'s own caller count re-verified by literal-address
xref AND an independent whole-PE-section raw byte-pattern scan (not just `.text`): exactly one caller
in the whole binary, full stop — `0x00447160` itself. No second caller uses `0x00447050` as a
"find slot, then write to it myself" helper; the original census's specific caller-COUNT claim for
this one function was actually correct (only its "allocator" framing was wrong — it is a free-slot
finder over the array `0x004470a0` allocates, found in pass 1). (2) `0x00447160`'s own reachability
re-checked the same two ways for any INDIRECT path (vtable slot, function-pointer table, any `DATA`
reference) — zero hits in `.rdata`/`.data`; the one `.text` byte-pattern hit is a confirmed
mathematical coincidence (one byte inside an unrelated `CALL`'s relative-displacement operand at
`0x5405cc`, whose encoded target-minus-next-instruction difference happens to reproduce
`0x00447160`'s own VA bit-for-bit — read and confirmed directly, not assumed from Ghidra's own `{imm}`
xref tag). **No hidden indirect caller exists for `0x00447160`**, the same `spec-render-pipeline.md`
§23.4-style blind spot this check was specifically designed to catch.

**A third hypothesis, raised by `purrsian-62`, tested and REFUTED, same day:** since the `~f<CCRR>`/
`~s<CCRR>` tile-file-family finding (`spec-zone-data-format.md` §13.2) shows only `~s` carries real
Mesh geometry, maybe `spec-zone-data-format.md` §12.6's own "gate=0 in all 1,647 occurrences" aggregate
(all 2,971 files, unstratified) buried a nonzero gate specifically inside the `~s` family. **Re-ran
the gate scan broken out by family, both real city archives (`sr3_city_0.vpp_pc`/`sr3_city_1.vpp_pc`,
2,878 files, 1,588 Navmesh-tag occurrences): `~f`=468/468 gate=0, `~s`=468/468 gate=0,
plain-named=652/652 gate=0 — zero nonzero gates in ANY family, including `~s`.** Cleanly refuted; the
original aggregate finding was already correct and complete, not an artifact of mixing families.

**Net state, honestly, after two full static passes plus this targeted re-check: the contradiction is
real and UNRESOLVED by any static method tried.** Three independent checks (literal-address xref,
whole-PE-section byte-pattern scan, and a population re-scan stratified by the one axis that could
plausibly have hidden something) all agree with each other and with the corrected static trace: zero
of the registry's 8 slots should ever show a nonzero presence bit. **The only remaining path is a live
write-watch directly on the registry's 128-byte block** (`MemoryAccessMonitor` on
`0x3171a5c`..`0x3171a5c+0x80`, watching specifically whether any slot's own `+2` field is ever nonzero
at the moment of a real `0x00447770` success) — scoped and ready in
`scratchpad/live-capture-navmesh-registry_2026-10-08/hook_navmesh_registry.py` (already updated with a
direct hook on `0x00447160` and a fixed, working registry-value read), not yet run. This needs the
user to play again; per this project's own standing dynamic-analysis rule, that goes through a written
sheet from the orchestrator, not something run unilaterally. Full write-up, both passes:
`scratchpad/navmesh_registry_real_writer_2026-10-08.md`.

**Two final cheap checks, 2026-10-08, PARKED after both came back inconclusive/confirming rather than
resolving — no third live capture requested today.** (1) `purrsian-62` asked whether the live log's
~87,918 background calls to `0x00447770` (far more than the 13 calls `0x00a12a80` itself makes) mean
the "immediately preceding" calls near each of the 10 real path-query successes show `-1` or
"present" values — if `-1`, `0x00a12a80` might not actually depend on `0x00447770` the way the static
trace says. **Checked directly against the raw CSV: the handful of `registry_reader_return` rows in
the same timestamp window as each of the 10 successes are overwhelmingly non-`-1` ("present"-looking")
values** — but this is NOT informative either way, honestly: at a 99.25% background success rate,
~96% of any 5 random nearby calls would show "present" by sheer base rate alone, and the existing hook
has no backtrace/caller-identity field to isolate the SPECIFIC call made from inside `0x00a12a80`'s
own stack frame from the dominant unrelated background caller(s). **A real methodological limit of
the existing log, not a finding either for or against dependency.** (2) `0x00a12a80` was
re-disassembled fresh, instruction by instruction, specifically hunting for any path to a `true`
return that does NOT depend on the registry call (a cached result, a Navpoint-graph fallback, a
same-region shortcut) — **found none.** The function has exactly one `true` exit (`mov al,1` at
`0xa12cdf`) and exactly one `false` exit (`xor al,al` at `0xa12b5d`, reached from TWO places: directly
on registry failure, or later if the real search/scoring call `0x452e10` itself fails). There is no
third path. **This confirms, not refutes, the original reading of `0x00a12a80` — the function's own
logic is correct as previously documented; the mystery is entirely in why its internal call to
`0x00447770` sometimes succeeds, not in any misreading of `0x00a12a80` itself.** One more data point,
not independently conclusive but consistent with a real (not glitching) mechanism: the "present"
retvals are not random — a small set of distinct values repeat thousands of times each (top 3 values
alone account for 19,699 of the 87,918 calls), the signature of a small, stable set of real resolved
keys, not noise. **Net: both checks came back clean in the sense of finding no bug in either prior
reading — this is PARKED as "live-confirmed working, mechanism unresolved," per the orchestrator's own
call, with the live write-watch script noted above as ready for a future session if ever revisited.**

**UN-PARKED, 2026-10-08: a direct hook on `0x00447160` itself just fired live, for the first time.**
A fresh capture run (user, ~5+ minutes, drive + gunfight) recorded **16 real calls to `0x00447160`**
(the presence-bit writer itself, not just the registry reader `0x00447770`), starting ~5 seconds after
attach. This is more decisive than the already-documented `registry_reader_return` evidence above — it
is a direct hit on the exact function both static passes proved has exactly one caller and that caller's
own gate is confirmed 0 in all 1,647 real shipped `.czn_pc` occurrences. **A third static check, run
immediately after**, re-verified "one level up" from `0x00447160` (the known caller `0x008642a0` itself,
confirmed via `spec-zone-data-format.md` §12.2 to call `0x00447160` directly with no intermediate
function, so "one level up" is exactly one target): a direct-call census (1 hit, the already-documented
`0x00865351` inside dispatcher `0x008652d0`), a literal-dword scan across all 6 PE sections (zero hits,
not even a coincidental one), a full re-disassembly of `0x008642a0`'s own body (52 instructions, the
gate check confirmed the only way in or out), and the content-kind table row's own pointer fields (don't
alias `0x008642a0`) — **all four came back clean. No second caller, entry point, or indirect path
exists one level up either.** The contradiction remains real and unresolved by any static method tried
across three full passes now. **Most plausible untested candidate, flagged not guessed:** some
in-memory mutation of the gate dword AFTER load (the static trace only proves the gate is 0 in every
shipped FILE, not that it stays 0 for the rest of a session) — not tested this pass. The existing Frida
hook script (`hook_navmesh_registry.py`) has been updated (not run) with `Thread.backtrace` (top 6
return addresses, module-relative) and a full arg dump on its `0x00447160` hook, writing to a new file
(`navmesh_registry_live_log_v2.csv`) to avoid disturbing the existing 170,628-row capture — ready for the
next capture run whenever one is coordinated. Full detail:
`scratchpad/navmesh_registry_caller_recheck_2026-10-08.md`.

### 14.7 Expanded blast radius: `0x00a12a80` is a third direct caller, and all three dependent claims are STATICALLY DEAD, RUNTIME CONFIRMATION PENDING (2026-10-08)

Dispatched to settle a real tension `purrsian-62` flagged between this section (dead registry) and
§15.7's own framing of `0x00a12a80` ("picks the cheapest path-reachable candidate") before Team B
implemented cover-selection against the latter. Full trace: `scratchpad/action45_covernode_pathquery_reconcile_2026-10-08.md`.

**`0x00a12a80`** (the multi-destination path query §15.7 cites for `cover_node`'s final pick, and §11.3
F cites for action 45 `pickup weapon`) is a **third direct caller of `0x00447770`** — not previously
traced this far by §14's own call-site census, alongside `0x00a12180`/`0x00539200`. Read in full
(`0x00a12a80`-`0x00a12ce9`, 617 bytes): it calls `0x00447770` as its own first step
(`00a12b4c CALL 0x00447770; 00a12b58 CMP EAX,-0x1; 00a12b5b JNZ ...never taken...; 00a12b5d XOR AL,AL`)
and returns `false` unconditionally on the registry's `-1` sentinel, before it ever inspects a
candidate or reaches its own real (and otherwise-functional) per-candidate cell-key builder
(`0x00a12630`) or multi-destination search/scoring engine (`0x00452e10`). Since §14.3 already proved
the registered-zone count (`0x3171a58`) is always zero, `0x00a12a80` statically always returns false
for every caller.

~~**This generalizes §14.4's `move retreat` finding to two more actions, by the same root cause:**
action 8 `cover take def` (§15.7) and action 45 `pickup weapon` (§11.3 F) are both statically
unreachable in retail — `cover_node`'s own gather/filter/score pipeline runs for real every time but
its final claim step always fails; action 45 is doubly dead (its own per-candidate `0x00a12180` snap
already empties the candidate array before `0x00a12a80` would even be reached).~~ **LIVE-REFUTED,
2026-10-08 (§14.7a): the live capture shows `0x00a12a80` returning success 10/13 times, time-correlated
with real cover-select attempts — action 8 is NOT unreachable in retail. Action 45's own upstream
`0x00a12180` dead-end claim is also now suspect (it rested on the same `0x00447770` gate).**

**A naming trap worth recording (unaffected by the correction above):** `0x3171a58`/`0x3171a5c` sit
only 12-16 bytes from `0x3171a64`, a separate, unrelated, LIVE global — the `cover_node` object-pool
singleton `0x008cd960` (§15.7's own AABB-gather) and action 45's pre-filter `0x004f0810` both read
fields on `0x3171a64` for legitimate, functioning purposes. Do not conflate the two when next touching
either function.

**STATUS, 2026-10-08: SUPERSEDED by §14.7a.** The "STATICALLY DEAD, RUNTIME CONFIRMATION PENDING"
label this note originally set, and the live-confirmation check it called for, both happened exactly
as planned — and the check came back NEGATIVE on "dead": the live capture refutes it for `0x00a12a80`
specifically (success 10/13 times) and for the underlying `0x00447770` gate generally (99.25% success
rate). **This is the correctly-cautious outcome of following "implement behind a switch, not
hard-coded" instead of shipping the static claim as fact** — exactly why that caveat was added. See
§14.7a for the full capture results and the now-in-progress real-writer search.

---

## 15. The real live movement-target mechanism: Navpoint/`cover_node` (2026-10-08) — why NPCs still move despite §14's dead navmesh

Direct follow-up to §14: if the navmesh query layer is dead, something else must be driving the movement/chase/cover behaviour NPCs visibly exhibit. This section traces that "something else" for one concrete, fully-read consumer.

### 15.1 Neither class carries any on-disk adjacency data

`Navpoint` (classIdx `0x29`, instance vtable `0x0116ceb4`) and `cover_node` (classIdx `4`, instance vtable `0x0116add8`) were each read via their real per-instance tail-readers (`0x008eb410`, `0x008cef40`). Every property either class stores is a scalar (float/byte/small string-resolved enum) or a short designer string — never a link, index, or handle to another instance. `Navpoint`'s real property set: `navpoint_type` (always present, string→enum via table `0x25e7ba0`), `navpoint_flags` (rare bitmask, 9.1% of instances), `navpoint_extra_info` (rare free-text designer string — confirmed NOT adjacency data by reading its handler in full), `audio_marker` (very rare), `navpoint_sync` (rare). `cover_node`'s real property set is **8 properties, not the 3 previously known by name**: `cover_pos`, `cover_orient`, `cover_type`, `cover_index`, `cover_angle_left`, `cover_angle_right`, `cover_def_angle_left`, `cover_def_angle_right` (table `0x024fa768` for `cover_type`). **`cover_type` and `cover_index` are real, readable properties that occur in zero of the 12,956 real shipped `cover_node` objects** — a genuine population-confirmed negative, not an oversight.

### 15.2 The real mechanism: type/flag-driven self-registration into flat arrays, then runtime spatial filtering — not a graph

Navpoints self-register at load time into up to four separate flat global pointer arrays, selected by bits of `navpoint_flags` (bit `0x1`→`0x0289fc08`, `0x2`→`0x0262d528`/cap 256, `0x4`→`0x026236a0`, `0x8`→`0x01503e1c`/cap **1**, a true runtime singleton). Each handler has an exhaustively-confirmed single caller (Navpoint's own tail-reader) — these arrays are Navpoint-specific, not shared with `cover_node`. **There is no adjacency graph anywhere in this mechanism.** "Links" between Navpoints (indices/handles/graph edges) do not exist on disk or in any traced in-memory structure.

### 15.3 The real destination-selection algorithm, traced end to end from its one call site

`0x00b3e180` has exactly one caller in the whole binary (`0x00b42974`, inside a mode-dispatcher `0x00b428d0` in a previously-uncharacterised address range, `0x00b3xxxx`–`0x00b4xxxx`, disjoint from both of this file's existing AI clusters). After several gates (acting-character liveness, target force-flags, a distance bound via the project's generic distance helper `0x00da1330` — the same helper §14.1 cites for the unrelated post-radius gate, not navmesh-specific — and a cooldown check), it reaches the real search:

```
for each pointer in the bit-0x1 Navpoint array (global count 0x0289fc00 / array 0x0289fc08):
    if distance(navpoint.pos, target.pos) is within an inner..outer radius pair: add to candidates (cap 10)
if any candidates: pick one uniformly at random (0x00dab660); output its position
else: fall through to a separate, untraced fallback
```

**This is a flat linear scan with a two-radius (annulus) geometric filter and a uniform-random pick among survivors — not a graph search, not nearest-neighbor.** The caller (`0x00b428d0`) hands the chosen position straight to a movement command (`0x00b3da30`) on success, confirming this is a live, working destination-selection path — not a passive query. On failure it degrades to a different function (`0x00b3ac20`), the same graceful-degradation shape §14.4/§14.6 already document for the dead navmesh query, but here degrading away from a LIVE search.

**Action 16 `move retreat`'s dead-navmesh-query failure (§14.4) is confirmed unrelated to this mechanism** — its own routine (`0x0053d540`) never calls `0x00b3e180` or touches the Navpoint arrays; the two call graphs are disjoint. Navpoint-based movement and the dead navmesh registry are two entirely separate systems in the shipped game, not alternate paths through the same action.

### 15.4 Population data

`Navpoint`: 5,982 real objects in 324/2,971 zones (1–206 per zone, mean 18.5), concentrated in large mission/district zones. `cover_node`: 12,956 real objects in 209/2,971 zones (1–463 per zone, mean 62.0), concentrated in enclosed combat interiors/arenas rather than open-world zones — hand-placed per-encounter design data, not a procedurally-dense navmesh substitute.

### 15.5 Correction to a prior pass's working label (propagated to `spec-zone-data-format.md` §11 separately)

The per-class *descriptor* vtable's slot `+0x04` was previously labelled "(tail-reader)" for `general_mover`/`script_npc` (`spec-zone-data-format.md` §11's U2 unit) based on its outer call shape alone. Reading Navpoint's exact analogue in full **refutes that label: slot `+0x04` is Destroy/Release** (it calls the target's own destructor, then unlinks/relinks the per-class object-pool's free list) — paired with slot `+0x00` Create, on a confirmed 144-byte-stride per-class object pool. **The real tail-reader lives on the separate *instance* vtable's own slot `+0x04`**, not the class-descriptor vtable. This was not independently re-checked for `general_mover`/`script_npc` specifically, but the mechanism (two different vtables per class — a shared/transient descriptor vtable and a distinct instance vtable) is now confirmed general, not Navpoint-specific.

### 15.6 Open items

~~The real `cover_node` selection algorithm (who picks which cover a human uses) was not located.~~ **RESOLVED 2026-10-08, §15.7 — direct follow-up.** ~~`cover_orient`'s 6-way jump table~~ **[RESOLVED 2026-10-12, capstone disassembly of `0x008cf000`–`0x008cf0c0`:** the 6-way switch at `0x008cf037` (table `0x008cf2d8`) is **not on `cover_orient`**. It switches on the byte at `+0x8e` that was just parsed as **`cover_type`** (registry `0x024fa768`, values 0–5 below). `ebp` is the loader's "from editor/zone data" flag.

| `cover_type` | Load behaviour |
|---|---|
| `editor` (0) | rewritten to **`misc_obj`** (4) when the flag is set |
| `veh_top` (1), `veh_side` (2) | read property **`cover_index`** into `+0x8f` (via `0x008cd650`) |
| `veh_door` (3), `human` (5) | **reject the cover** (return false) when the flag is clear; otherwise read `cover_index` into `+0x8f` |
| `misc_obj` (4) | nothing extra |

Every cover then resets `+0x84` to −1 and reads `cover_angle_left`/`cover_angle_right`. So `cover_index` is the attachment slot on a vehicle or human host, and `cover_orient` (`+0x48`) is only the orientation passed to `0x008ccd80`.]** ~~the `navpoint_type`/`cover_type` enum tables' actual named values~~ **[enum values RESOLVED 2026-10-12 from the static initializers, which push (name, value) pairs into the shared enum registrar `0x005963b0` (bit-flag variant `0x008ce790`):**

| Enum | Values | Initializer / registry |
|---|---|---|
| **`cover_type`** | `editor`=0, `veh_top`=1, `veh_side`=2, `veh_door`=3, `misc_obj`=4, `human`=5 | `0x01001000`, registry `0x024fa768` |
| **`cover_flags_str` bits** | `cover_crouch`=`0x2`, `cover_stand`=`0x4`, `cover_edge_left`=`0x8`, `cover_edge_right`=`0x10`, `cover_edge_over`=`0x20` | `0x01001050`, registry `0x024facb0`. These stance/edge names are **flags, not `cover_orient` values** |
| **`navpoint_type`** | `ground`=0, `floating`=1, `bookmark`=2 | `0x01002590`, registry `0x025e7ba0` |
| second registry, `0x025e8198` | `ground`=0, `floating`=1 only | `0x01002650`; HIGH CONFIDENCE the navpoint-path variant, since paths cannot be bookmarks |

]** ~~`navpoint_sync`'s effect~~ **[RESOLVED 2026-10-12, capstone disassembly of `0x008ade90`:** `navpoint_sync` set means the loader calls `0x008ade90(4, navpoint)`.
1. If the navpoint already has a network record (`+0x3c`), nothing happens.
2. Otherwise, for kinds other than 3 and `0xe` (navpoints are 4), it stops when `0x00889d10()` is true.
3. If not, it allocates a **co-op replication record** through `0x00887120(id pair +0x08/+0x0c, kind)` and registers it with `0x00894280(8, record)`.

So the property makes the navpoint **network-synchronised in co-op**. Reading `0x00889d10` as "no session" is HIGH CONFIDENCE (it gates every kind except 3/`0xe`). **Port rule (single player): ignore the flag.** It is the same `+0x3c` record that the authority gate `0x008addb0` (spec-lua-api-behaviour §8.x gate-polarity note) tests.]**, and ~~the raw-disk `navpoint_flags` bit `0x10`'s meaning remains OPEN~~ **[bit `0x10` RESOLVED 2026-10-12.** The only navpoint-module reader in a whole-binary raw scan for `test byte [reg+0x80], 0x10` (27 hits, the rest are other classes) is the navpoint teardown `0x008eaef0`. When the bit is set, it calls `0x005610e0(id pair +0x08/+0x0c)` before the base teardown `0x00754410`. `0x005610e0` swap-removes that ID from a 32-byte-entry list at `0x013c7db0` (count `0x013c7dac`) in the **camera module** (`0x0056xxxx` code, `0x013c7xxx`/`0x013c8xxx` camera globals). So bit `0x10` = **"registered as a camera point"**: the navpoint is in the camera system's point list and must be removed when it is destroyed. The structure is CONFIRMED. That it is a look-at/camera-target list is HIGH CONFIDENCE; the list's adder and consumer were not traced. **Port rule:** a navpoint with bit `0x10` is available to the camera point list. Bits `0x20`/`0x40` still have no reader and are inert as far as found.]**, with concrete addresses/offsets in `scratchpad/navpoint_covernode_realformat_2026-10-08.md` for a future unit. The `0x00b428d0` mode-dispatcher cluster is **no longer a dead end for cover specifically** — §15.7 confirms it's where the real cover claim/search path starts (reason bytes 8/9/10), though it is a separate call graph from §15.3's own Navpoint destination search.

### 15.7 The real `cover_node` selection algorithm, traced end to end (2026-10-08)

**The Navpoint-selector cluster named above (`0x00b428d0`/`0x00b3e180`) is confirmed to be a dead end for THIS question — it operates only on the bit-`0x1` Navpoint array (§15.3), never `cover_node`.** The real answer sits under code this document already had on file but hadn't fully pulled: action 8 `cover take def` (`0x00540b90`), whose Body text already noted "the mode picks the cover." That thread gives the full chain:

```
Cover-stay mode Enter 0x00543480 (dispatch on reason byte)
  → case 8/9/10: 0x00542bc0 ("claim or find cover")
      → 0x005429b0 (resolve scripted override, else search)
          → 0x0050a690  <- THE REAL SEARCH/SCORE/SELECT FUNCTION
```

**`0x0050a690(human, searchRadius)` — read in full, 1,282 instructions, CONFIRMED:**
1. Builds an AABB around the human: 14.0 base search radius (≥15.0 for brute-class enemies), fixed 4.0 vertical half-height, clipped to the human's own post/defend area if one is set, widened toward the group leader's own position for followers.
2. Gathers candidates via `0x008cd960` — a **brute-force linear scan of every live `cover_node` instance in the class's own object pool** (not a spatial grid/BVH, not a Navpoint-style flat flag array), filtered by AABB containment and unclaimed status.
3. Filters each surviving candidate: not recently failed (a per-human perception-list slot, confirmed list index **20**), an "open/usable" runtime flag, and the separate cover evaluator `0x00509e50` (below).
4. **Scores** survivors: a cost combining an arc/exposure penalty, path-reachability/distance terms, and group-cohesion penalties; clamped to `[0,100]` and quantized.
5. **Final pick** via `0x00a12a80` — the same multi-destination navmesh path-query action 45 `pickup weapon` already uses elsewhere in this document. ~~**CORRECTION, 2026-10-08 (§14.7 has the full trace): `0x00a12a80` opens by calling `0x00447770` (§14.3's dead registry) directly as its own first step... Statically, this means the "picks the cheapest path-reachable candidate" framing never actually resolves... STATICALLY DEAD; RUNTIME CONFIRMATION PENDING.**~~ **LIVE-REFUTED, 2026-10-08 (§14.7a): the live hook check this note itself called for has now run, and it refutes "dead" — `0x00a12a80` returned success 10/13 real calls during a captured gunfight, closely time-correlated with real `cover_select_attempted` events. `cover take def` (action 8) DOES successfully resolve in shipped retail at least some of the time. The real population mechanism/conditions are still being investigated (§14.7a) — implement as WORKING, not as always-fails, pending that follow-up.** These two movement-target systems use genuinely different selection philosophies, not variations on one theme: Navpoint is annulus-filtered uniform-random; `cover_node` is AABB-gathered, arc/path-scored, and path-query-resolved — now confirmed live-functional, at least intermittently, not merely statically plausible.
6. **Claims** the winner via `0x008cfeb0` (compare-and-set an ownership handle at the cover's own `+0x70`/`+0x74`); on contention or total path-query failure, blacklists every survivor into the same perception-list slot 20 and returns "no cover," throttled upstream by a 6-8s random back-off.

**The arc test, direct answer to whether `cover_angle_left`/`cover_angle_right`/`cover_def_angle_left`/`cover_def_angle_right` (§15.1) are actually respected — YES, confirmed exactly, via `0x008cd1d0(coverNode, threat, threatPos)`, read in full:**
- Reference direction = the cover_node's own generic world-placement yaw (`0x00da3910`, the shared base-class transform) — **NOT** `cover_orient` (that property's own role stays OPEN, narrowed to "not the arc reference," likely feeding its own separate, not-yet-examined 6-way jump table).
- Primary arc: `bearing = atan2(dz,dx)` from cover to threat, wrapped relative to that yaw, tested against `cover_angle_left` (property offset `+0x80`) / `cover_angle_right` (`+0x81`), each dequantized from their on-disk byte as `(byte/255)*2π − π`.
- Secondary arc: the identical test run from a point pulled back 0.45 units behind the cover along its own forward vector, against `cover_def_angle_left` (`+0x82`) / **`cover_def_angle_right` (`+0x83` — resolves this document's own previously-unmatched 4th quantize-offset OPEN item)** — with an override that forces "exposed" (fails the test) when a separate open-cover flag is set AND the threat falls within a 30° cone of a third stored direction.
- An aerial-vehicle threat bypasses the whole arc test.
- **A hard player gate, independent of score:** inside `0x00509e50`, if any threat in the human's full perception list that resolves to the local player fails a shield/visibility test, the whole candidate cover is instantly disqualified for the `cover take def` action.

**Inputs to the overall selection, for a cleanroom implementer:** the human's own position and personality/class (brute vs. normal), its post/defend area if any, its group leader's position for followers, its ENTIRE perception list (every currently-perceived threat, not just one), each candidate cover's position/yaw/angle-byte properties, each candidate's own per-human failure memory (perception-list slot 20), and real navmesh path distance (via the shared `0x00a12a80` multi-destination query). Distance/radius limits: 14.0 base search radius (≥15.0 for brutes), 4.0 fixed vertical half-height, a 5.0-unit degenerate-proximity special case, a 128/328-unit path-search budget.

**[CONFIRMED — direct, complete disassembly+decompile of the core gather/filter/score/pick pipeline (`0x0050a690`) and the full arc-test formula (`0x008cd1d0`). HIGH CONFIDENCE, not independently disassembled to their own last instruction: a scripted-interrupt early-return edge case inside `0x005429b0`, a "hold position"-only refinement, several additive cost sub-terms inside the scoring step's own path/geometry refinement, and `0x00ad5020`'s exact aerial-vehicle-bypass semantics.]** Full detail, every address, and the complete scoring-formula breakdown: `scratchpad/covernode_selection_2026-10-08.md`.

**Second confirmed consumer, 2026-10-08:** `0x008cd1d0` is also called from the roadblock-spawn system's
character/ped creation path (§19.7 §3.5, "case 2" of `0x0092ff20`), refining a newly-created occupant's
placement against the same cover-arc test — the same evaluator this section documents, reused for a
structurally unrelated system.

---

## 17. Ambient population runtime: distant pedestrian/vehicle spawn-despawn (2026-10-08)

Traces how "distant" (LOD-cheap) pedestrians and traffic vehicles spawn/despawn around the player, from
runtime code — the question `spec-tables-traffic-ai.md` §14/§18 (the `.xtbl` schema/DATA side) and
`spec-zone-data-format.md` §13.2/§13.4 (the Traffic/Sidewalks/Parking zone-data side) left open on their
own runtime-consumer side. All of §17 is new disassembly; nothing below was previously documented.

### 17.1 Verdicts up front

1. **Density/budget is a flat global target count, not per-zone/per-district.** The distant population
   carries a hardcoded target of **75 live entities** for pedestrians and **75** for vehicles (same
   literal, two separate addresses), refilled at a capped rate per maintenance pass (25/pass ped,
   20/pass vehicle). No per-zone/per-district density field exists anywhere in the shipped tables —
   `map_districts.xtbl` (`spec-tables-environment.md` §11.8, 5 districts) carries only
   lighting/skybox/audio data, nothing population-shaped.
2. **Spawn points come from a dynamic algorithm over an authored road/sidewalk graph** — the already-
   documented Traffic (vehicles) and Sidewalks (pedestrians) zone-data graphs (`spec-zone-data-format.md`
   §13.2) — filtered by a camera-motion-predicted future position, a randomized distance within the
   XTBL-configured spawn band, and (vehicles only) a `Spline_Type` one-hot bitmask match between the
   candidate road segment and the vehicle's traffic type. The final pick among valid candidates is
   uniform-random.
3. **Despawn is a distance/angle cull against the predicted camera position**, evaluated once per
   maintenance pass for every live distant entity, plus an all-at-once mass-despawn path when a whole
   category is toggled off.
4. **Driver: a single per-frame dispatcher** (reached from the same per-frame world update §1.1 already
   documents) unconditionally calls both the vehicle and pedestrian distant tick functions every frame
   (modulo the global pause/loading gate); each tick's own expensive candidate search is throttled to
   roughly once per second by a shared one-shot-per-frame latch plus a per-category countdown timer, and
   the ped/vehicle heavy passes mutually exclude each other within a single frame.

### 17.2 Density/budget — the real in-memory structure

The target live-entity count is a flat engine constant, confirmed by a clean single-reader/zero-writer
xref census:

```
@ 0130f41c  static dword 0x0000004b (=75)  — read once, by the ped tick 0x00b90960
@ 0130f4d4  static dword 0x0000004b (=75)  — read once, by the vehicle tick 0x00b93670
```

Both sit immediately after the already-documented `distant_ped_spawn_parameters`/
`distant_vehicle_spawn_parameters` XTBL-loaded structs and have no corresponding `.xtbl` element —
engine constants, not file-tunable, identical between pedestrians and vehicles. Per-pass refill is
independently capped: ped via a literal immediate inside `0x00b90960` (`if (0x19 < count) count = 0x19`,
cap 25/pass); vehicle via a second hardcoded, never-written constant `@ 0130f4d0 = 0x14` (=20, cap
20/pass).

The already-specced distance/angle tuning (`Base_Spawning_Dist`/`Max_Spawning_Dist`/`Length_Per_Spawn`,
`spec-tables-traffic-ai.md` §14.3) is consumed exactly where the loader's destinations predict, inside
the two tick functions, to compute a dynamic spawn-radius multiplier keyed to frame delta-time:

```
0x00b90960 (ped), confirmed by decompile:
  if (0.0 < frameDeltaTime) {
      spawnMul = frameDeltaTime / LengthPerSpawn_recip + 1.0;
      if (MaxSpawningDist < spawnMul * BaseSpawningDist) spawnMul = MaxSpawningDist / BaseSpawningDist;
  } else spawnMul = 1.0;
  ...
  spawnDist = BaseSpawningDist * randFloat01 * spawnMul;
  if (MaxSpawningDist <= spawnDist) spawnDist = MaxSpawningDist;
  despawnRadius = DespawnExpansionDistance + spawnDist;
```

`0x00b93670` (vehicle) is structurally identical, using `0x0130F4D8`/`0x0130F4DC`/`0x0130F4E0`/
`0x0130F4E4` in the same roles. **OPEN, not resolved this pass:** the field the vehicle tick uses in the
`Length_Per_Spawn`-reciprocal-divisor role is `0x0130F4D8`, which `spec-tables-traffic-ai.md` §14.3
currently maps to `Double_Spawn_Height` (the vehicle loader has no `Length_Per_Spawn` element at all per
that section's own element list) — either that field-to-offset mapping needs a second look, or
`Double_Spawn_Height` genuinely double-duties as the vehicle analogue of the ped spacing constant despite
its name.

No per-zone/per-district density field exists: `map_districts.xtbl` was checked field-by-field (nothing
population-shaped), and the population-adjacent `.czn_pc` content kinds (Traffic/Sidewalks) carry
graph/placement geometry, not a density scalar. Density is one global number for the whole city.

### 17.3 Spawn selection, both kinds traced end to end

**Pedestrians (`0x00b90960`, body `0x00b90960`-`0x00b91a00`, read in full):** gated on
`0x00bbdd90()` — the Sidewalks singleton's own load-gate (`spec-zone-data-format.md` §13.2) — plus a
session/host check. Distant pedestrians can only spawn when the current zone's Sidewalks data (`.czn_pc`
content kind `0x2238`) is loaded; candidate positions are drawn from the authored Sidewalks node graph,
selected dynamically each pass, not a separate procedural ground-walk. Camera-lookahead prediction
(confirmed by decompile) scales the camera/player velocity by `Camera_Lookahead_Time`
(`spec-tables-traffic-ai.md` §14.3) and adds it to the current position — every downstream distance/angle
test runs against this **predicted future** camera/player position, not the current one. Sidewalks graph
consumption reads the singleton's node-array count field at `+0x10` off `0x02924d40` and indexes the
`distant_peds` mesh table (`0x0290acb0`, `spec-tables-traffic-ai.md` §14.1). Type/weight selection is a
population-weighted uniform-random pick: the function tallies how many live distant peds occupy each of
up to 8 type-slots (walking the live circular list, head `0x0130f434`, next-pointer at `+0x4c`), then
calls the shared uniform-random-pick primitive `0x00dab660` — **the same helper §15.3 already documents
for the unrelated Navpoint selector; this is its third confirmed consumer in this project.**

**Vehicles (`0x00b93670`, body `0x00b93670`-`0x009341fb`, read in full):** gated on `0x0290ce9c` (set by
`0x00a69720()` — the Traffic singleton's own load-gate, `spec-zone-data-format.md` §13.2). Camera-
lookahead prediction is structurally identical, using `0x0130F4E8`. Traffic-graph consumption allocates a
candidate buffer sized to the Traffic singleton's (`0x02705fe0`) node count, then fills it with candidates
within an `[inner, outer]` distance band. Each candidate road-spline node carries its own `Spline_Type`-
compatible bitmask at `+0x1c`; the `distant_vehicle_traffic_types.xtbl` rows (`spec-tables-traffic-ai.md`
§14.4, array at `0x0290CE88`, each row's own `+0x04` = `Spline_Type` as a `1<<n` one-hot mask, stride
`0x1c`=7 dwords) are AND-tested against it — **direct runtime confirmation that `Spline_Type` is a
vehicle-type-to-road-type eligibility filter**, exactly as its one-hot-mask storage shape implied (a
highway-only truck type never considers a side-street node, and vice versa). Within an eligible node's
segment, the function builds a byte array of evenly-spaced slots (segment length ÷ slot count, capped at
5 slots), marks slots too close to an already-live vehicle as occupied, `qsort`s the remaining valid
slots, and picks one via the same `0x00dab660` primitive. **Vehicle selection is genuinely different in
shape from pedestrian selection** — ped is a single weighted-random graph-node pick; vehicle is a
graph-node pick *followed by* a sorted, occupancy-filtered slot search along that node's own segment —
not two implementations of one shared algorithm. Final commit (`0x00b92740`) touches
`0x0290CE88`/`0x0290CE8C`/`0x0290CE90`/`0x0290CEB0`/`0x0290CEB4` — the exact destinations
`spec-tables-traffic-ai.md` §14.4/§14.5/§14.6 already document from the loader side, independently
cross-confirming this tick function as the real runtime consumer of every `distant_vehicle_*` table on
file.

**OPEN, not traced this pass:** whether/how a distant (LOD) entity is ever promoted into a fully-
simulated near-field character/vehicle as the player approaches (or demoted back) — a separate,
untraced subsystem from the near-field vehicle population's own spawn-side algorithm (its despawn
schedule alone is specced, `spec-tables-traffic-ai.md` §18).

### 17.4 Despawn logic

Two distinct mechanisms. **(a) Distant/LOD population — per-entity distance+angle cull against the
predicted camera position, once per maintenance pass**, before any new spawns are attempted: both tick
functions walk their own live list (ped head `0x0130f434`; vehicle head `0x0130f51c`), test each entity
against `Despawn_Angle`/the stored despawn radius via a dedicated cull-test helper
(`0x00b8f5a0` ped / `0x00b925a0` vehicle), and release+unlink any that fail. **(b) Whole-category mass
despawn** — when a category's enable flag flips to OFF (§17.6), `0x00b90440`/`0x00b935d0` (§17.5) walk
the entire live list unconditionally and release every entry regardless of distance, a hard reset, not a
gradual cull. **(c) Near-field, full-detail vehicles** use the already-specced `vehicle_despawn.xtbl`
schedule (`spec-tables-traffic-ai.md` §18: bucketed `{num_cars, time}` thresholds, an abandoned-vehicle
delay, a squared player-distance cutoff) — **RESOLVED 2026-10-08, see new §17.4c below.**

### 17.4c Near-field (fully-simulated) ambient-vehicle population (2026-10-08)

A second, structurally separate ambient-vehicle maintenance system exists, driven from the *same*
per-frame dispatcher (`0x0090faf0`, §17.5) as the distant tier, via a previously-undocumented sibling
call:

```
0x0090faf0 (existing per-frame dispatcher)
  └─ 0x0090f2c0  (near-field/full-sim vehicle maintenance pass)
       ├─ 0x0090eaf0   despawn-schedule tick — applies vehicle_despawn.xtbl's thresholds
       ├─ 0x00906b00   per-vehicle despawn-eligibility predicate (consumes player_vehicle_despawn_distance)
       ├─ 0x0090f060, 0x0090ef90, 0x00906c40   bookkeeping siblings (not fully resolved)
       └─ (own body)   spawn-candidate section: 3-slot buffer, shared uniform-random-pick 0x00dab660
```

**`0x0090eaf0` is the real per-frame tick that applies the `vehicle_despawn.xtbl` schedule — this
closes the OPEN item above and `spec-tables-traffic-ai.md` §18's own matching note.** Reads the exact
five runtime destinations §18 already documents from the loader side — `in_view_despawn_info`
(`0x026092c8`, count at `+0x50`=`0x02609318`), `out_of_view_despawn_info` (`0x0260931c`, count at
`+0x50`=`0x0260936c`), and `abandon_despawn_delay` (`0x02609370`/`0x02609374`, read into both slots). If
the live near-field-vehicle count is below the smaller of the two arrays' entry-0 `num_cars` threshold,
it cancels any pending despawn; otherwise it walks the out-of-view bucket list first (shorter wait the
more vehicles are live), falling back to the in-view list, each gated by its own
`abandon_despawn_delay`-derived timer.

**`0x00906b00` is the per-vehicle despawn-eligibility predicate — CONFIRMED consumer of
`player_vehicle_despawn_distance`.** A candidate is eligible only if (a) not force-no-spawn-blocked
(`0x008addb0` — see correction below), (b) none of its 8 seats holds a flagged-important occupant
(`0x00ab5070`/`0x0097e280` loop over seats 0–7), and (c), only when the vehicle is in the player's own
world-cell, farther from the player than `player_vehicle_despawn_distance` (`vehicle_despawn.xtbl`,
`spec-tables-traffic-ai.md` §18, destination `0x02609378`) — confirmed stored squared from the runtime
side too (compared directly against a squared distance, no `sqrt`). The real shipped metres value was
not re-extracted from a live data file this pass.

**Concrete population numbers:**
- **Hard cap: 48 concurrently-live near-field vehicles** — `0x0090ee10` gates every spawn attempt on the
  live counter `0x02609120` staying below `0x30` (48); the counter is incremented on commit (`0x0090cbb0`)
  and decremented on release (`0x00905790`). Smaller than the distant tier's flat 75 — consistent with
  full physics/AI simulation being more expensive than LOD rendering.
- **Shared headroom ceiling: 150 eligible AI humans** — `0x009086d0` walks the live AI-human roster,
  counts how many pass the generic eligibility gate `0x008addb0(human, 0)`, and returns `150 - count`;
  the spawn pass only proceeds if this is positive — each new ambient vehicle most plausibly also needs a
  driver-human instantiated within the same global 150-human ceiling used elsewhere in the engine.
- **Maintenance cadence:** the whole pass only runs when a randomized 200–300 ms timer (armed by
  `0x009073a0`/`0x00905b40`) has expired, the `"os_vehicle"` toggle category (§17.6) is enabled, and the
  Traffic zone-data singleton is loaded (`0x00a69720()`); the despawn-schedule sub-step is itself gated by
  a second, independent randomized 400–600 ms timer. Net: near-field vehicle maintenance runs roughly
  3–5× per second, faster than the distant tier's ~1 Hz heavy pass (§17.5).
- **Spawn side:** a 3-candidate-slot buffer, final pick via the same shared uniform-random-pick primitive
  `0x00dab660` already documented in §17.3/§15.3 — its fourth confirmed consumer in this project. Spawn
  rate is a dynamically-computed value (`0x00910940`), scaled by a camera/view-fraction term and clamped
  by two engine constants — not a fixed literal.

**Promotion/demotion boundary — honest negative result.** No code evidence of an explicit same-entity
hand-off between the distant (LOD) tier and this near-field tier was found: separate bootstrap chains
(`0x00908fb0`→`0x009069e0` here vs. `0x00b90900`/`0x00b92e70` for the distant tier), separate live-entity
lists (`0x026093ac` here vs. `0x0130f434`/`0x0130f51c` distant), no shared pointer traffic found between
them. Each tier most plausibly maintains its own population band independently, with no object
continuity across the switch — reported as a negative/inconclusive result from the functions opened,
not an exhaustive whole-image claim.

**Side finding — corrects §19.4's framing:** `0x008addb0` (the "+0x455 force no spawn" accessor
`spec-lua-api-behaviour.md` §3.6/§6.10 and §19.4 describe in a human-only/notoriety context) is confirmed
**not** human-exclusive — `0x00906b00` and `0x0090eaf0` both call it identically on *vehicle* handles. It
is a generic ambient-actor eligibility flag shared by both human and vehicle instances, not a
human-specific one.

Full detail: `scratchpad/ped_traffic_density_radius_2026-10-08.md`.

### 17.5 Runtime driver — bootstrap, re-arm, per-frame dispatcher, throttle

**One-time bootstrap (not per-frame): `0x00908fb0`**, unconditionally called from `0x00708330` (a
load-time/zone-bring-up function, confirmed not the per-frame path). Resets pool counters, writes
compiled-in default `Base_Spawning_Dist`/`Max_Spawning_Dist`-shaped defaults, then calls 5 subsystem
inits (`0x009069e0`; `0x009142a0`; `0x0092ed10` registers a `"roadblock"` toggle category — not itself a
population pool; `0x007e2df0` registers a `"cell_disabled"` per-session world-cell kill switch, also not
a pool; `0x00917d10`), registers all 6 `"os_*"` toggle categories (§17.6), and ends by calling the
already-documented `vehicle_despawn.xtbl` parser `0x00905c20` — confirming that spec's own "which
function this loader hangs off" note exactly.

**Correction, 2026-10-08: the "reserve N/cap M" framing above for `0x009069e0`/`0x009142a0`/`0x00917d10`
is wrong — all three are timer-arm calls, not pool-capacity setters.** Their shared callee
(`0x00d9e180`/`0x00d9e3c0`, fully disassembled this pass) is a member of this project's generic
countdown-timer family (`0x00d9e140`/`0x00d9e1e0`/`0x00d9e250`/`0x00d9e4c0`, running on a wraparound
global clock): `0x00d9e180(timerSlot, min, max)` literally computes `*timerSlot = clock + rand[min,max]`
— "arm this countdown to fire after a random duration between `min` and `max` ms," not "reserve `min`
cap `max` objects." Re-reading each call site: `0x009069e0` calls `0x00d9e180(slot, 400, 600)` — arms a
**400–600 ms** randomized timer (the exact slot §17.4c's despawn-schedule tick checks/re-arms every
invocation), not a "reserve 400/cap 600" pedestrian pool; `0x00917d10` calls `0x00d9e180(slot, 20000,
60000)` — arms a **20–60 second** randomized timer, not a "reserve 20000/cap 60000" pool; `0x009142a0`
calls the fixed-interval sibling `0x00d9e3c0(5000)` — a fixed 5000 ms re-fire. **`0x009069e0` itself also
builds a genuine small free-list pool** (6 blocks × 5 circular sub-lists) alongside the timer-arm, and
zeroes the near-field *vehicle* list head/live-counter §17.4c documents — HIGH CONFIDENCE this pool is
near-field **vehicle** state, not "pedestrian" as the removed framing claimed (the pool's own entity
type was not independently opened; inferred from the shared list head/timer slots/`vehicle_despawn.xtbl`
consumption in the same call chain, not a direct type-tag read). `0x009142a0`/`0x00917d10`'s own
subsystem identities beyond "arms a timer" remain OPEN. Full detail:
`scratchpad/ped_traffic_density_radius_2026-10-08.md`.

**Idempotent re-arm, also from `0x00708330`:** `0x00b90900` (ped, latch-guarded on `0x0290acac` — does
real work only the first call after the latch clears, cleared by `0x00b903f0`, plausibly zone-unload,
not independently confirmed) and `0x00b92e70` (vehicle, un-guarded, always re-evaluates `0x00a69720()`
and rewrites `0x0290ce9c`/`0x0290ce9d`; also called twice more from an untraced `0x0072d660`, HIGH
CONFIDENCE a save/load or host-migration path).

**The real per-frame dispatcher: `0x0090faf0`**, called unconditionally from `0x00702a50` — **the same
per-frame world update §1.1 already documents** as driving the per-character AI update once per human
per frame. Its body: short-circuits on the global pause/loading gate (`0x00720640`/`0x006e44f0`), resets
the shared ped/vehicle heavy-pass-exclusion latch `0x026093b0` to 0 every frame, conditionally runs a
block of unrelated per-frame subsystems (roadblocks, cells, etc.) behind two secondary gates, then calls
`0x00b93670()` (vehicle tick) and `0x00b90960()` (ped tick) **unconditionally, regardless of whether the
two secondary gates held.** HIGH CONFIDENCE rather than hard-CONFIRMED only insofar as `0x00702a50`'s own
body wasn't re-read in full to check for a further wrapping conditional.

**Cross-reference, 2026-10-08:** `spec-player-runtime.md` §5.1 has the full cross-subsystem per-frame
order `0x00702a50` sits inside (cutscene step → controls → player-human AI pass (§1.1 above) → **this
dispatcher's own NPC-AI pass** → vehicles → animation → velocity composer → Havok physics step →
character-controller step → post-step/post-move → camera late update) — see there for how this
dispatcher's own call fits relative to everything else ticked that same frame. `spec-render-pipeline.md`
§6.6 has the outer OS-thread topology (this dispatcher runs on the main/game thread) and the boot
sequence.

**The real throttle: a shared 1-shot-per-frame latch plus a ~1-second countdown.** Inside each tick, the
actual candidate-search/spawn/despawn work only runs `if (026093b0 == 0 && countdown-expired)`, then sets
`026093b0 = 1`. Net cadence: lightweight per-entity bookkeeping (spawn/despawn-radius refresh from the
current frame's camera prediction) runs every frame for both kinds; the expensive graph walk/cull/
spawn/despawn runs on a countdown timer (seen reset to 1000ms on the ped side, so roughly once per
second) — and the ped/vehicle heavy passes mutually exclude each other within any single frame (vehicle
runs first by call order if both timers expire the same frame; the other defers to its own next
expiry).

### 17.6 Correction to `spec-zone-data-format.md` §13.4: 8 toggle slots, not 0–18

§13.4's existing claim that `0x02609394` is "a city-wide ambient-vehicle spawn-budget/eligibility layer,
slots capped 0–18" does not match the executable. `0x00908fb0` registers **six** named,
session-replicated, independently Lua-toggleable categories through one shared primitive
`0x0086d770(name, byteArrayPtr, count, hostFlag, notifyCallback)`, each owning its own **8-byte** enable
array (confirmed directly from the accessor functions `0x00905dc0`/`0x00905ea0`/`0x00905ec0` — the query
accessor's "all" branch loops a hard `8`, and the set accessor's "all" branch writes exactly 2 adjacent
dwords = 8 bytes):

| Category string | Array base | Notify/apply callback |
|---|---|---|
| `"os_distant_vehicle"` | `0x0260937c` | `0x00b935d0` |
| `"os_distance_ped"` | `0x02609384` | `0x00b90440` |
| `"os_boat"` | `0x0260938c` | none |
| `"os_parking"` | `0x02609394` | none (matches `spec-zone-data-format.md` §13.4's existing address exactly) |
| `"os_vehicle"` | `0x0260939c` | `0x009073a0` |
| `"os_ped"` | `0x026093a4` | none — **but NOT inert, 2026-10-09 correction, see §20.1**: it is the enable toggle the near-field pedestrian spawn chain (`0x0090fa60`) polls directly every frame, reading the 8-byte array inline rather than being pushed a change notification — a different wiring style, not disuse. |

`spec-zone-data-format.md` §13.4 should read "8 slots, shared architecture across 6 named ambient-
population toggle categories, each independently network-replicated and Lua-controllable via
`0x0086d770`" — a toggle/eligibility layer (mission scripts turning a whole category on or off, e.g. "no
ambient boats during this mission"), not a density control and not zone-indexed.

### 17.7 Confidence summary and recommended cross-references

**CONFIRMED (full disassembly, this pass):** `0x00908fb0` (bootstrap), `0x00b90900`/`0x00b92e70`
(re-arm latches), `0x0090faf0` (per-frame dispatcher body), `0x00b90960` (ped distant tick, full
2454-line dump), `0x00b93670` (vehicle distant tick, full 1801-line dump), the 6-category `0x0086d770`
registration block, the 8-slot bound of the `os_*` arrays, the hardcoded-constant status of
`0x0130f41c`/`0x0130f4d4`/`0x0130f4d0`, the Traffic/Sidewalks-singleton consumption (`0x02705fe0`/
`0x02924d40` now have a confirmed runtime consumer — previously "load-time only" per
`spec-zone-data-format.md` §13.2), the `Spline_Type` bitmask AND-test, the despawn-cull list-walks.

**HIGH CONFIDENCE:** `0x00702a50`→`0x0090faf0` with no further frame-skip wrapper; `0x009142a0`/
`0x00917d10`'s own pool identities (not pinned down); the countdown-timer pair as generic shared engine
utilities (not individually read).

**OPEN:** ~~the near-field vehicle population's own spawn-candidate algorithm and despawn-schedule
tick (§17.4c); the distant-to-near promotion/demotion boundary (§17.3)~~ **CLOSED, same file,
§17.4c (2026-10-08, this stale marker self-corrected 2026-10-11): the despawn-schedule tick is
`0x0090eaf0` (applies `vehicle_despawn.xtbl`'s thresholds), the spawn side is a 3-candidate-slot
buffer via the shared `0x00dab660` uniform-random-pick, and the promotion/demotion boundary is an
honest NEGATIVE result — no object-continuity hand-off found between the distant/near-field
tiers, each maintaining its own population band independently.** Still genuinely open: the
`0x0130F4D8` field-identity cross-check (§17.2); `0x00b903f0`'s own role (§17.5's own
"plausibly zone-unload" is HIGH CONFIDENCE, not confirmed); the two extra `0x0072d660` call
sites into `0x00b92e70`.

**[CONFIRMED/HIGH/OPEN as marked above.]** Full detail, every address, and the complete raw disassembly/
decompile evidence: `scratchpad/ambient_population_spawn_despawn_2026-10-08.md` (raw Ghidra query output
preserved under `scratchpad/ambient_pop_q/`). `spec-tables-traffic-ai.md` §14/§18 and
`spec-zone-data-format.md` §13.2/§13.4 carry short cross-reference notes pointing back here rather than
duplicating this material.

---

## 18. Traffic AI lane-following: consumer module, pool mechanics, and Havok integration (2026-10-08)

Traces how traffic-AI-controlled (non-player, ambient) vehicles follow the road network documented from
the data-format side in `spec-zone-data-format.md` §13.2 (`0x2236` "Traffic"). That section covers the
record layout (14 count/pointer pairs, the segment→point graph, the two position codecs); this section
covers the consumer code that reads it, how far the actual steering decision could be traced, and the
integration point with the Havok driving slice. Method: Ghidra-free, direct `capstone` disassembly of
every address below (the shared Ghidra server was saturated with concurrent jobs this pass).

### 18.1 Lane selection/following — RESOLVED: a masked, lookahead-bounded forward walk of the point graph (2026-10-10, supersedes the prior "explicitly OPEN" framing)

**Correction to the prior pass's own addressing, found and fixed 2026-10-10:** `0x00b1c5a0` was never
a function entry point — it is an instruction address *inside* a single large function at `0x00b1be40`
(body `0x00b1be40`-`0x00b1cc5e`, ~450 instructions, exactly one caller). The prior pass's Ghidra-free
capstone-only method mis-split this function at an internal label; independently re-verified against
the live Ghidra server this pass (querying `0x00b1c5a0` resolves to the function body
`0x00b1be40`-`0x00b1cc5e`, confirmed).

**CONFIRMED:** `0x00b1be40` is the forward-search function that walks the point graph from a given
current point, accumulating along-path distance until a caller-supplied lookahead distance is
exceeded, and returns a target point. Signature (register-parameter-aware):

```
0x00b1be40(int *visitedHandles, int visitedCount, float3 *originPos,
            uint maskInclude, uint maskExclude, float lookaheadDist,
            byte legalModeFlag, int filterHandle,
            float3 *out_targetPoint, Point *currentPoint /* passed in EDX */)
```

- It walks the point chain forward from `currentPoint`, using the point's own "exit A"/"exit B" byte
  indices (offsets `+0x2`/`+0x3` — the same per-point index fields `spec-zone-data-format.md` §13.2
  already documents) to find the next point(s), accumulating the per-point along-path distance (float
  at point `+0x4`, read via `0x00b1a870`) until it exceeds `lookaheadDist` (the loop condition is
  literally `lookaheadDist < accumulatedDistance`).
- Candidate segments are filtered by `(maskInclude & segmentKindFlags) != 0 && (maskExclude &
  segmentKindFlags) == 0`, where `segmentKindFlags` is the 4-byte field at segment `+0x1c` (the same
  struct whose 2-bit "kind" sub-field `spec-zone-data-format.md` §13.2 already documents). Two extra
  special-case bits (`0x8000`/`0x8000000` within that same field) gate an additional caller-supplied-
  handle acceptance test (`0x00a6af10(segment, filterHandle)`), plus a pairwise comparison via
  `0x00a69af0` (the already-documented 2-bit kind extractor) against `filterHandle` — together this
  reads as a **lane-class/vehicle-class permission filter**, not a cost function: "is this segment
  legal for a vehicle of this class, and does it belong to the zone/kind the caller is asking for."
- `originPos` (3 floats) is **not read from a live vehicle transform inside this function** — it is
  supplied by the caller (see `0x00b1cc60` below) as either a direct copy of a caller-held position or
  the **midpoint of two corner/junction endpoint positions** (averaged via `0x00a69c30`, which indexes
  the point pool's own position fields). Consistent with `originPos` being "the corner/handle's own
  reference position," not necessarily a moving vehicle's live position — whether the caller's own
  caller eventually substitutes a true vehicle position here is still OPEN (below).
- The output `out_targetPoint` (3 floats, written through the pointer param) is the function's actual
  deliverable: a literal 3-D point advanced along the (filtered) path network by the requested
  lookahead distance. **This directly answers the prior pass's own open item: the function that
  "produces a target point" is `0x00b1be40`, and the mechanism is a masked forward graph-walk bounded
  by a lookahead distance, not a closed-form spline/cross-track solve** — confirming, not merely
  inferring, the prior pass's own structural guess.

**HIGH CONFIDENCE — the immediate caller, `0x00b1cc60` (body `0x00b1cc60`-`0x00b1ce70`):** resolves a
small caller-supplied parameter block into the exact argument list above. It accepts either a
**single handle** (averaging that corner's own two endpoint positions into `originPos` — the
"mode==1" path, parameter-block field `+0x10`) or an **explicit list of up to 16 candidate
next-segment handles** (field `+0x8`, count at field `+0x10`), which it resolves one-by-one via
`0x00a6b450` (handle→point-table-index validity check, fully decompiled) into the `visitedHandles`
array passed to `0x00b1be40`. **This means the actual "which branches are even eligible to search"
decision is made by `0x00b1cc60`'s own caller**, which hands it a ready-made candidate list —
`0x00b1be40` itself does not choose among arbitrary junction exits from nothing; it either explores
exactly the two exits recorded on each point it's already walking (graph topology, not a branch
"choice"), or is handed a pre-filtered candidate set by its caller. `0x00b1cc60`'s own 8 call sites:
`0x00b1d2e7`, `0x00b20aae`, `0x00b1e775`, `0x00b1e822`, `0x00b45aa2`, `0x00b47089`, `0x00b4cdf1`,
`0x007d41a1` — independently re-verified against the live Ghidra server this pass; not individually
opened. Candidate-list cap `0x10` (16) and the parameter-block field layout (fields `+0x14`/`+0x18`
= include/exclude masks, `+0x1c` = lookahead float, `+0x20` = legal-mode byte, `+0x24` = filter
handle, `+0x4` = current-point pointer) form a reusable "path query" struct — worth
cross-referencing against any Lua/behavior-tree binding exposing equivalent fields (not checked this
pass).

**Still OPEN, narrower than before:** whether a *moving vehicle's* live position/heading/velocity ever
feeds into `originPos` or into the lookahead-distance parameter (e.g. speed-scaled lookahead) — not
resolved; the trace stopped at `0x00b1cc60`'s own 8 callers, none opened. The prior pass's blanket
claim ("no function found reads vehicle position") is now more precisely: *no function at this level
reads it directly — it would have to come from one of those 8 callers.* Most promising next step:
open `0x00b1cc60`'s 8 callers, looking for a vehicle-instance position/heading read feeding this
parameter block.

### 18.2 Junction/branch handling — REFINED: the kind-flag bits gate forward-search cutoff, not a discrete choice (2026-10-10)

Re-examined with the corrected function boundary (§18.1): the "segment kind" bits at `+0x1c` (bits
`0x1` and `0x2`, matching `spec-zone-data-format.md` §13.2's 2-bit field) are tested as **early-exit/
cache conditions on each of a point's two exits independently** inside `0x00b1be40`'s main
forward-walk loop — e.g. "if exit-A's kind bit 0 is already set, stop extending that branch;
otherwise keep walking it and testing the lane masks." This matches, and now pins down the exact bit
semantics for, the prior pass's own "is this candidate already selected/cached" cache/arbitration
hypothesis (bit 0 ↔ exit index at point `+0x2`; bit 1 ↔ exit index at point `+0x3`).

The function explicitly explores **both** exits of a flagged junction point up to the lookahead
distance (recording the first point along each as the "left"/"right" branch candidates), then
independently continues walking *both* saved candidates further. **No cost comparison, goal-distance
comparison, or RNG was found between these two candidates in this function** — the same honest
negative the prior pass reported, now confirmed on the corrected function rather than guessed.
**Branch selection at this level is lane-legality filtering (the `maskInclude`/`maskExclude` test,
§18.1), not cost-based choice** — one side is rejected outright when its kind flags don't satisfy the
caller's requested vehicle-class bits, not by comparing a cost or goal distance between the two.
Whether a higher-level cost/goal-driven choice exists in `0x00b1cc60`'s own unopened callers is the
real remaining open question — same scope as §18.1's own open item.

### 18.3 Integration with the physics driving slice — one shared input surface, write-site CLOSED (updated 2026-10-10, §18.6)

`spec-physics-runtime.md` §10.6 already documents `hk_custom_vehicle_driver_input` (`0x00ae30c0`) as the
ONE RTTI override class (row 35, §2.1) that converts the game's raw control fields into Havok's
analog-input struct — re-disassembled in full (53 instructions) this pass to pin every field it reads:

```
esi+0x128  (float) -> out+0x00   ; steering, copied straight through
esi+0x12c  (float) -> out+0x04   ; copied straight through, role not independently named
esi+0x130, esi+0x138 (floats, summed, then a sign-preserving-squared curve unless instance
                       flag +0x164 bit 1 is set) -> out+0x08; the post-curve value is ALSO
                       written back to esi+0x134 (a cached/smoothed value)
esi+0x14f  (byte)  -> out+0x0c
esi+0xb0   (byte)  -> out+0x0d   ; the same status byte spec-physics-runtime.md §10.7 names as
                                    the component-array slot +0x20 (driver input) bookkeeping field
```

There is exactly ONE class in the entire 39-class/72-slot RTTI census that overrides this Havok interface
— no second, traffic-specific driver-input class exists anywhere in the binary's RTTI. Whatever writes a
vehicle's steering/throttle (player, AI-passenger, or ambient traffic) has no second code path available:
it MUST write into these same six vehicle-instance fields, because Havok only ever calls this one
override. Independent corroboration from `spec-player-runtime.md`'s pre-existing "Vehicle Steering"
animation-parameter query (`0x00a926b0(vehicleHandle)`) — a vehicle-agnostic "live steering value" read
used to drive the steering-wheel-holding animation for any vehicle, including presumably
ambient-traffic-driven ones.

**The literal write site inside the traffic module — RESOLVED 2026-10-10, §18.6.** An earlier disp32
byte-pattern scan for the literal `0x128` restricted to the known traffic address range
(`0x00a60000`-`0x00b70000`) found 79 occurrences, all `[esp+0x128]`-style stack-frame-local
references, not a struct-field write — a real negative for that method, not for the write's
existence: the write happens one hop further down, inside a shared vehicle-command dispatcher the
traffic module calls into, not as a direct `[reg+0x128]` store at the traffic-module call site
itself, which is exactly why the byte scan couldn't find it. §18.6 traces the real chain end to end
and confirms ambient traffic writes the identical `+0x128`/`+0x12c`/`+0x130`/`+0xb0` fields named
above, on the same instance layout, via the identical `+0x20` curve-parameter sub-pointer and
`+0x164` flag-byte semantics `0x00ae30c0`'s own body reads — not merely a plausible-offset guess.

### 18.5 One level above `0x00b1cc60`: live vehicle position found, and the junction cost-comparison located (2026-10-10, continuation)

Opened 6 of `0x00b1cc60`'s 8 call sites (they resolve to 5 distinct functions — `0x00b1e775`/
`0x00b1e822` are two call sites inside the same function). `0x00b45aa2` and `0x007d41a1` were not
opened this pass (budget; depth over breadth on the other 6, per standing instruction).

**CONFIRMED — `0x00b4c710` (call site `0x00b4cdf1`) reads a live, moving entity's own position and
gates the path re-query behind a cached-target distance check.** A `this`-pointer method on a
per-entity traffic-AI record. It reads three consecutive floats at the entity's own `+0x40`/`+0x44`/
`+0x48` repeatedly (e.g. `0x00b4c872`, and via `LEA ESI,[EDI+0x40]` feeding two box-builder calls,
`0x00b4ca40`) — independently re-verified against the live Ghidra server before splicing — the
first time in this trace a position field has been shown to belong to the entity being driven,
rather than to a static junction/corner record. It also reads a cached previous target point, stored
on the same per-entity record at absolute `+0xe38`/`+0xe40` (`0x00b4ccf5`, re-verified), compares it
against a candidate position via a squared-distance computation against a threshold constant
(`0x01117a60`), and — **only when the cached point is stale or the vehicle has moved far enough
away** — rebuilds a search box around the live position, runs the nearest-candidate picker
`0x00b46fc0`, calls `0x00b1cc60`, and writes the new result back into the same cache fields. **This
is a throttled re-query mechanism, not a naive per-frame full graph-walk, and it directly answers
§18.4 item (a) for at least one of the 8 callers: yes, live entity position reaches this query
layer, gated by a per-entity cached-target/distance threshold rather than fired every frame.** One
level further up: `0x00b4c710`'s only two call sites are both inside `0x00b4ceb0` (itself called
only from `0x00b4d070`) — a per-entity update-tick-shaped function (status/counter fields at the
same entity's `+0xe08`/`+0xe20`) not fully decompiled this pass; its own second parameter (passed
via a separate object) was not identified. This chain (`0x00b4d070`→`0x00b4ceb0`→`0x00b4c710`) is
the most promising concrete lead toward the Havok write-site (§18.3/§18.6).

**CONFIRMED — `0x00b46fc0` (call site `0x00b47089`) is a generic "pick the candidate minimizing a
distance" selector, not itself a live-position reader.** Given a caller-supplied current-point
pointer and up to 4 candidate next-points, it loops calling `0x00b1cc60` once per candidate,
measures the resulting target point's distance via `0x00b1ad80`, and keeps the minimum-distance
candidate — a genuine cost comparison, just not goal-seeking (no destination parameter reaches it).

**CONFIRMED — `0x00b1ce80` (call site `0x00b1d2e7`) runs a genuine goal/destination-distance-
minimizing choice across candidate branches — this answers §18.2's own open question: yes, a
cost/goal-driven choice exists, one layer above `0x00b1cc60`.** Signature: `0x00b1ce80(float3
originPos, float3 *destPos, int *out_pointHandle, byte mode)`. It finds the point nearest `originPos`
via a spatial box query, separately builds a candidate list from a box query centered near
`destPos`, and for each candidate calls `0x00b1cc60` to get a lookahead-advanced target point, then
keeps the candidate minimizing distance-to-`destPos` (`0x00b1d2e7`-`0x00b1d35a`, an explicit
replace-running-minimum loop). **A real "closest-to-goal" greedy branch choice** — directly
contradicting the narrower reading that no cost/goal comparison exists above `0x00b1be40`/
`0x00b1cc60`; it exists, one hop further up than either. Whether `originPos`/`destPos` are
themselves a live vehicle position/a route destination is NOT resolved — both are plain function
parameters here; `0x00b1ce80` has exactly 2 callers (`0x00b2578e` inside `0x00b25620`, `0x007e33cb`
inside `0x007e3380`), neither opened.

**CONFIRMED — `0x00b1e5e0` (call sites `0x00b1e775`/`0x00b1e822`) runs the identical nearest-
candidate-by-distance shape as `0x00b1ce80`**, over a different box-query radius/offset, parameterized
by `(originPos, maskInclude, maskExclude)`. Same caveat: `originPos` is a plain parameter; its single
caller `0x00b20420` (inside `0x00b20390`) was not opened.

**HIGH CONFIDENCE — `0x00b20920` (call site `0x00b20aae`) is static junction/connectivity-graph
construction, not a per-frame steering consumer.** Takes a junction-definition struct (count, an
array of corner-position records, flag/handle fields) and, for every pair of its own member corner
positions, resolves each to a point handle and links adjacent pairs via `0x00b1cc60`'s output, with
early failure-return on any resolution failure — the shape of a one-time (or infrequent) graph
build/validation pass over fixed geometry, not a moving entity. 6 callers, none opened, but nothing
in its own body reads anything resembling a live transform.

**Updated answer to §18.4(a):** of the 6 call sites opened, **one (`0x00b4cdf1`/`0x00b4c710`)
CONFIRMED to read live entity position**, gated by a cached-target/distance throttle; **two
(`0x00b1d2e7`/`0x00b1ce80`, `0x00b1e775`+`0x00b1e822`/`0x00b1e5e0`) CONFIRMED to run a genuine
nearest/goal-distance cost comparison**, though whether their own position parameters are
themselves live-vehicle-sourced requires opening their own callers (`0x00b25620`, `0x007e3380`,
`0x00b20390` — not attempted); **one (`0x00b20aae`/`0x00b20920`) is load-time/infrequent graph
construction, not a steering read**; **two (`0x00b45aa2`, `0x007d41a1`) remain fully unopened.**

**RESOLVED 2026-10-11 (SPEC TEAM, agent `ad580496a8ccbfc32`, independently re-verified by direct
query before splicing — `0x00b1ce80`'s call site inside `0x00b25620` and `0x007d39b0`'s global
singleton selection both matched exactly). Opened all of this section's own remaining callers:
`0x00b25620`, `0x007e3380`, `0x00b20390` (plus one further hop, `0x00b20c20`→`0x00b2b630`), and
the 2 still-fully-unopened `0x00b1cc60` callers, `0x00b45aa2` and `0x007d41a1`.**

- **`0x00b1ce80`'s `originPos` is CONFIRMED live-vehicle-sourced in both of its callers.** In
  `0x00b25620`, the call passes its own first argument's `+0x40` offset as `originPos` (confirmed
  by raw disassembly) — that first argument is the per-entity traffic record, confirmed by the
  same function's own later reads of its `+0x8`/`+0xc` (the vehicle-handle pair §18.6 already
  established) and `+0xc68` (the drive-state sub-record), ending in the same commit call to
  `0x0051ddd0` on that same entity pointer. In `0x007e3380`, the call passes its own first
  argument's `+0x40` offset the same way — again the per-entity record. `0x00b1ce80`'s `destPos`
  is **NOT uniformly live-vehicle-sourced** — it genuinely varies by caller: in `0x00b25620`, the
  `destPos` argument is passed through opaquely from `0x00b25620`'s own sole caller, `0x00b2d4d0`,
  which reads a handle and 3 floats off a byte-stream deserializer — an externally-supplied
  (scripted/network-command) literal destination point, NOT a live object's position. In
  `0x007e3380`, the `destPos` argument is the `+0x40` offset of a pointer returned by a
  no-argument singleton accessor, `0x009da4e0` — the same `+0x40` live-position convention, on a
  different object whose exact identity (player? a lead/target vehicle? a cached route-request
  target?) was not resolved this pass (would need `0x009da4e0`'s own body opened).
- **`0x00b1e5e0`'s `originPos` is CONFIRMED live-vehicle-sourced, one hop further removed.** Its
  sole caller, `0x00b20390`, loops over a caller-supplied candidate-position array (not itself a
  raw entity field at this layer) — but that array is built one hop further up, in `0x00b2b630`
  (reached via `0x00b20c20`), which IS a direct thiscall on the per-entity traffic record: it
  reads that record's own `+0x40` (position) and `+0x64`/`+0x68`/`+0x6c` (the same orientation row
  §18.6 already established), tests its `+0xc6e` bit `0x20`, writes through its `+0xc68`, and
  ends with the same commit call to `0x0051ddd0` on that same record — so the candidate array
  `0x00b1e5e0` searches is ultimately seeded from the live entity's own position/orientation, not
  a static junction record.
- **`0x00b45aa2` (inside `0x00b45840`) belongs to the same live-entity family.** `0x00b45840` is
  itself a thiscall on the per-entity record; it computes `entity+0xc68+0x53c` = `entity+0x11a4`
  and checks it via `0x00a1ef50` — the exact same gate §18.6 already names for the state
  `0xe`/`0x10` branch — then reads `entity+0x40` directly to anchor a search box before reaching
  `0x00b1cc60`.
- **`0x007d41a1` (inside `0x007d39b0`) is a DIFFERENT consumer entirely — RESOLVED 2026-10-11,
  item (b) below: it's the already-documented GPS waypoint-navigation module, not a traffic or
  camera system.** Confirmed by direct disassembly: `0x007d39b0` selects between two global
  singleton addresses, `0x0012fdff0`/`0x0012feb20`, and reaches `0x00b1cc60` fed from that
  singleton's own fields, not any per-entity `+0x40` field. Full resolution below item (b).

**Net answer, closing §18.4(a) for this entire cluster: the junction/branch-choice decision chain
is live-vehicle-sourced on its origin side everywhere traced; its destination side can be EITHER
a live object's position OR an externally-supplied scripted/network point, depending on caller —
both are real, confirmed mechanisms, not a single uniform answer.** Remaining open threads if
picked up again: `0x009da4e0`'s own return-object identity; whether `0x00b2b630`'s other call
sites are all per-tick traffic callers; the exact semantics of the `0x0012fdff0`/`0x0012feb20`
singletons `0x007d39b0` operates on.

### 18.6 The Havok driver-input write site — RESOLVED: traced from `0x00b4d070` through the shared vehicle-command dispatcher to the exact `0x00ae30c0`-matching fields (2026-10-10, continuation)

**CONFIRMED — `0x00b4ceb0` itself never reaches a steering/throttle write; it is purely the
per-entity state-gate wrapper around `0x00b4c710`'s target-point resolution.** Opened in full (body
`0x00b4ceb0`-`0x00b4d062`). Its own second parameter (register-passed, correcting §18.5's framing of
which register) is threaded into five sibling calls (`0x00b4a980`, `0x00b4c270`, `0x00b4a180`,
`0x00b4a730`, `0x00b4a530`) as a plain argument, never into `0x00b4c710` itself. The function's
entire body is conditional bookkeeping on the entity's own status bits (`+0xe08`, `+0xe09`, `+0xe20`)
and throttled countdown timers (`0x00d9e140`) that gate *whether* `0x00b4c710` runs this tick — no
steering/heading computation, no write to anything resembling a vehicle-instance field, appears
anywhere in `0x00b4ceb0`.

**CONFIRMED — the write happens one level up, in a sibling call from `0x00b4ceb0`'s only caller,
`0x00b4d070`, independently re-verified against the live Ghidra server before splicing (confirmed:
`0x00b4d070` subtracts `0xd` from its own state byte and calls `0x00b47490` directly after).**
`0x00b4d070` (body `0x00b4d070`-`0x00b4d25a`) calls `0x00b4ceb0` first (the gated target-point
refresh), then switches on the entity's own state byte at `+0xc69`:

- state `0xd` → calls `0x00b47490` (opened in full this pass — the write path, below).
- states `0xe`/`0x10` (conditioned on `0x00a1ef50(entity+0x11a4)` returning nonzero) → calls
  `0x00b2bbc0` (NOT opened — a second possible write path left genuinely OPEN, not fabricated).
- Either way, it then calls `0x00b49820` (an "is a path/route pending" test) and, when false, runs a
  path-request block that caches the entity's own live position (`+0x40`/`+0x44`/`+0x48`, matching
  §18.5) into `+0xe44`/`+0xe4c` and advances the state to `0x10` — route bookkeeping, a different
  code path from the write traced below.

**CONFIRMED — `0x00b47490` computes a steering sign from a heading/target dot-product and distance
check, and writes it through the engine's shared vehicle-command API, which lands in fields that
exactly match `0x00ae30c0`'s own documented layout.** Full body opened (`0x00b47490`-`0x00b475f3`),
independently re-verified against the live Ghidra server before splicing (confirmed: calls both
`0x00a929e0` and `0x00a92640` directly). Mechanism:

1. Computes a dot product of two 3-vectors read off the entity record — `(+0xe14,+0xe18,+0xe1c)·
   (+0x64,+0x68,+0x6c)` — into a scalar (HIGH CONFIDENCE, not independently proven: the shape
   matches "cosine of the angle between the desired heading and the vehicle's current forward
   axis," consistent with `+0x64..0x6c` being a live orientation-basis row and `+0xe14..0xe1c` being
   a target-direction vector cached near the `+0xe38`/`+0xe40` target-point fields §18.5 already
   found, but the vector roles were not independently confirmed — the magnitude helper
   `0x00abaf10`, called right after, was not opened).
2. Picks a sign (`+1.0`/`-1.0`) from a lane-side status bit at `+0xe09` bit 2.
3. Calls **`0x00a929e0(entityHandleLo=+0x8, entityHandleHi=+0xc, signFloat)`** — the entity's own
   vehicle-handle pair, not a raw pointer.
4. Based on which side of a `cos(30°)`/5.0-unit distance gate the dot-product/magnitude pair falls
   on, also calls either `0x00a92640(+0x8, +0xc, 0)` (far/misaligned case) or `0x00b207d0(entity+
   0xc68, 3.5, distance, 1)` (close/aligned case — not opened), then unconditionally calls
   `0x00a92ab0(+0x8, +0xc, 0)` (not opened — likely a third command, "set brake/handbrake 0").

**CONFIRMED — `0x00a929e0` and `0x00a92640` are the SAME generic "send a float command to a
handle-addressed vehicle" dispatcher used everywhere in the traffic/vehicle code (21+ and 30+ call
sites respectively, spanning player, mission-script, and traffic systems alike) — the shared
command API `spec-player-runtime.md`'s pre-existing `0x00a926b0` "Vehicle Steering" query sits
alongside, not a traffic-specific path.** Both resolve the handle pair via `0x00458230` into an
object pointer; validate a liveness flag (`+0x33` bit `0x10` clear), a class-dispatch byte at `+0x34`
indexing a global per-class capability table (`0x02cc9900`) whose own `+0x6` byte has bit `0x80` set
("this class supports vehicle driving"), that `0x00853b10` returns false, and — only for
`0x00a929e0` — that `+0xbd0 == 1` ("currently vehicle-controlled"). Then: `0x00a929e0` switches
(via a 0-5 vehicle-type/variant selector, `0x00ad2f80`, not decompiled) between calling
**`0x00ae5290`** with the sub-object at `entity+0x808` (cases 0/1/2/4-fallthrough) or `0x00aed340`
with the sub-object at `entity+0x818` (case 5, not opened — plausibly a different vehicle-class
variant's own driver-input instance). `0x00a92640` calls `0x00a90b50` directly (skipping the
class-table gate), which re-reads the same selector and switches among `0x00aed2f0`
(`entity+0x818`), `0x00ad9910`, and **`0x00ae8fd0`** (`entity+0x808` — the SAME sub-object slot
`0x00ae5290` targets).

**CONFIRMED — `0x00ae5290` and `0x00ae8fd0` write exactly the fields §18.3 already documented for
`0x00ae30c0`, with matching flag-bit semantics, not merely matching offsets — both opened in full,
independently re-verified against the live Ghidra server before splicing:**

- `0x00ae5290(this, signFloat)`: reads `this+0x164` bits 0/1 (the SAME zero-override and
  curve-disable flags §18.3 names as gating the sign-preserving-squared curve on `+0x130`/`+0x138`),
  clamps the input to `[-1,1]`, and — unless bit 1 is set — runs it through a curve parameterized by
  a struct at `this+0x20`, then **writes the result to `this+0x130`** (confirmed by direct
  disassembly: `MOVSS dword ptr [ESI+0x130],XMM0`, 3 write sites) — the exact steering-accumulator
  field §18.3 names.
- `0x00ae8fd0(this, value)`: same `this+0x164` bit-0 gate, clamps/curves the value, and writes
  **`this+0x128` and `this+0x12c`** (confirmed by direct disassembly, multiple write sites), plus the
  status byte **`this+0xb0`** and a secondary byte at `this+0xcd` — exactly the `+0x128`/`+0x12c`/
  `+0xb0` fields §18.3's own disassembly of `0x00ae30c0` already named, read off the identical
  instance layout (same `+0x164` flag byte, same `+0x20` curve-param pointer `0x00ae5290` also
  reads).

**This closes §18.3/§18.4(c)'s open item: the ambient-traffic write site is CONFIRMED, not a
byte-pattern guess.** The structural match is what closes it — the `+0x164` bit-0/bit-1 flag
semantics, the `+0x20` curve-parameter sub-pointer, and the `+0x128`/`+0x12c`/`+0x130`/`+0xb0` field
set are all read off the SAME instance layout `0x00ae30c0`'s own body (§18.3) reads from, reached
here via an ordinary component-slot pointer (`vehicleEntity+0x808`), not any special-cased
traffic-only object. Full chain:

```
0x00b4d070 (per-vehicle traffic-AI drive tick, state == 0xd)
  -> 0x00b47490 (heading-dot-product + distance gate, picks a steering sign)
       -> 0x00a929e0(vehicleHandle, vehicleHandle, signFloat)   [generic vehicle-command dispatch]
            -> 0x00ae5290(driverInput, signFloat)  writes driverInput+0x130   (steering accumulator)
       -> 0x00a92640(vehicleHandle, vehicleHandle, 0)           [generic vehicle-command dispatch]
            -> 0x00a90b50(vehicleEntity, 0) -> 0x00ae8fd0(driverInput, 0)
                 writes driverInput+0x128/+0x12c (throttle pair), +0xb0 (status byte)
  where driverInput == *(vehicleEntity + 0x808), and 0x00ae30c0's own field map
  (§18.3) reads +0x128/+0x12c/+0x130/+0x138/+0xb0/+0x14f off this SAME object.
```

**CONFIRMED (2026-10-10) — the `0xe`/`0x10`-state sibling path through `0x00b2bbc0` is NOT a
different subsystem: it reaches the SAME write site as the state-`0xd` path, one hop shorter (no
`0x00b47490` intermediary).** Full body opened (`0x00b2bbc0`-`0x00b2c238`), independently
re-verified against the live Ghidra server before splicing (confirmed: direct `CALL 0x00a929e0` at
`0x00b2be43`, pushing the entity's own `+0x8`/`+0xc` handle pair exactly as `0x00b47490` does; and
`CALL 0x00a92640`/`CALL 0x00a92ab0` pairs at `0x00b2bfb2`/`0x00b2bfcc`, `0x00b2c06f`, and
`0x00b2c0c0`/`0x00b2c0da`). Mechanism: early-outs on the entity's own `+0xc68` sub-record being null,
on the same `0x00a1ef50(entity+0x11a4)` gate `0x00b4d070` already checks, or on a braking test
`0x00ad3820` (not opened) — all three with no dispatcher call. Otherwise it resolves a SECOND
handle pair at `entity+0xcc0`/`+0xcc4` (HIGH CONFIDENCE: a lead/target vehicle, not independently
proven — resolver `0x004d7f30` not opened) and gates on a distance-like scalar (`0x0046c9d0`,
`<=225.0`) plus flag `entity+0xc72` bit `0x20`: when that gate passes, it picks a sign and calls
`0x00a929e0(entity+0x8, entity+0xc, signFloat)` directly — per this section's own mapping, reaching
`0x00ae5290(driverInput, signFloat)`, writing **`driverInput+0x130`**; otherwise it falls through a
shared tail (status-byte checks on `entity+0xc71`/`+0xc6c`/`+0xc70`) that calls
`0x00a92640(entity+0x8, entity+0xc, 0x3c23d70a)` then `0x00a92ab0(entity+0x8, entity+0xc,
throttleValue)` — reaching `0x00a90b50`→`0x00ae8fd0(driverInput, value)`, writing
**`driverInput+0x128`/`+0x12c`** and **`driverInput+0xb0`** — the exact same fields already
confirmed above. Full chain:

```
0x00b4d070 (state == 0xe or 0x10, gated on 0x00a1ef50(entity+0x11a4) != 0)
  -> 0x00b2bbc0 (distance/brake gate against a SECOND handle-pair target at entity+0xcc0/+0xcc4)
       -> [close/aligned case] 0x00a929e0(entity+0x8, entity+0xc, signFloat)
            -> 0x00ae5290(driverInput, signFloat)  writes driverInput+0x130
       -> [far/misaligned case, shared tail] 0x00a92640(entity+0x8, entity+0xc, 0x3c23d70a)
            -> 0x00a90b50(vehicleEntity, ...) -> 0x00ae8fd0(driverInput, value)
                 writes driverInput+0x128/+0x12c, +0xb0
  (same driverInput == *(vehicleEntity + 0x808) as the state-0xd path; one hop from 0x00b2bbc0 to
  the shared dispatcher, vs. two hops — via 0x00b47490 — on the state-0xd side)
```

`0x00b2bbc0` itself has 9 call sites spanning many unrelated functions, so it is a generic shared
"distance/brake-gated steering decision" helper, not traffic-exclusive — consistent with
`0x00a929e0`/`0x00a92640` already being generic dispatchers (§18.6 above), not a sign this path is
traffic-specific.

**CONFIRMED (2026-10-10) — `0x00ad3820`, named in this section as a "braking/stop-condition test,"
is actually a generic entity-status gate, not a literal brake/speed check; its bailout is a hard
position snap-correction, not a steering/throttle no-op.** Full body opened (`0x00ad3820`-
`0x00ad3853`, independently re-verified against the live Ghidra server before splicing — matched
exactly). It returns "continue" (0) only when ALL of: the entity pointer is non-null; `0x008cc4d0`
(entity) is false (a one-instruction extractor of `+0x3b` bit 0); `0x0091f600`(entity) is false
(same shape, `+0x3a` bit 0); and `+0x33` bit `0x8` is set. Otherwise it returns "bail" (1). HIGH
CONFIDENCE (not independently proven at the individual-bit level): with 30 call sites spanning
dialogue/script triggers, physics, and many unrelated AI functions — not vehicle- or
traffic-specific — this reads as a generic "entity disabled/suspended/capability-missing" gate
reused anywhere a caller wants to skip per-tick work on an entity not in a normal active state, not
a brake-pedal or near-zero-speed test specifically. The concrete meaning of each individual bit
(`+0x3a` bit 0, `+0x3b` bit 0, `+0x33` bit 3) stays OPEN. When it signals bail, `0x00b2bbc0` calls
`0x00b2ed40`, passing the same sub-record argument — opened one level: it resolves a second handle-pair object (`param+0x4e0`/
`+0x4e4`, via `0x00458230`, the SAME liveness/class-capability-table gate pattern
`0x00a929e0`/`0x00a92640` use), computes a position delta between a cached target point and that
object's own live position (`+0x40`/`+0x44`/`+0x48`, matching §18.5), blends toward it, then calls
the resolved object's own vtable slot `+0x40` directly with the corrected position/orientation (a
transform-setter, not a vehicle-command dispatch) and, if a physics sub-object exists at `+0xe0`,
zeroes it via two more vtable calls. So the "brake bailout" path snaps the resolved object's
position/velocity rather than sending any steering/throttle command — no `0x00a929e0`/`0x00a92640`
call appears anywhere in `0x00b2ed40`'s body. Its own callees (`0x00b1f4d0`/`0x00b2ec20`/
`0x00da0800`/`0x00da57b0`/`0x00ab98a0`/`0x00c150c0`) were not opened.

**OPEN:** the second handle pair's exact role (`entity+0xcc0`/`+0xcc4`); the exact geometric roles
of the two 3-vectors dot-producted in `0x00b47490` (heading vs. target-direction) are HIGH
CONFIDENCE from context, not independently proven (`0x00abaf10` not opened). `0x00ad2f80`'s 0-5
vehicle-type/variant selector semantics are RESOLVED, 2026-10-11 — see
`spec-lua-api-behaviour.md` §21.24/§22.19: a plain 8-byte getter for the vehicle's static
`Vehicle_Type` xtbl class (0=Automobile, 1=Motorcycle, 2=Airplane, 3=Helicopter, 4=Vtol,
5=Watercraft, 6=failure sentinel), not an AI-mode gate.

### 18.6a The lane-side bit itself (`+0xe09` bit 2): setter found — CONFIRMED, dynamically recomputed under an overtake/pass-side gate (2026-10-10)

**CONFIRMED — exactly one instruction pair in the entire binary writes bit 2 of `+0xe09`,
inside `0x00b47c80`-`0x00b47f03`.** Independently re-verified against the live Ghidra server before
splicing (confirmed: `0x00b47c80`'s only caller is `0x00b4ad30`, reached via an unconditional direct
`CALL` at `0x00b4ad67`, immediately following a `TEST AL,AL`/`JZ` on `0x00b47f10`'s return value — the
masked bit-2 read/write itself sits at `0x00b47dac`-`0x00b47dba`: `XOR AL,[ESI+0xe09]` /
`AND AL,0x4` / `XOR [ESI+0xe09],AL`). Method (agent's own, re-verified by spot-checking the two central
functions rather than re-running the full scan myself): a binary-wide raw disp32/literal scan for the
4-byte pattern encoding offset `0xe09` found 18 genuine code references (plus a handful of unrelated
matches in non-traffic CRT code and `.rdata`, discarded as false positives). Of those 18, only
`0x00b47c80` touches bit 2 specifically — the others either only read it (`0x00b47490`, §18.6's own
reader; `0x00b47730`) or write different bits of the same byte (bit 0/bit 4 at `0x00b4c710`; bit 3 via
an XOR-toggle at `0x00b4ceb0`; bit 5 at `0x00b4ad30` itself, confirmed directly above the bit-2 call:
`OR byte ptr [EBX+0xe09],0x20` at `0x00b4ad8f`, a separate case of the same switch, not a second bit-2
writer).

**CONFIRMED — the write is a standard compiler "assign-bit-from-boolean" idiom, not a flag set once
and left alone.** The instruction sequence computes
`new_e09 = old_e09 ^ (((dot < 0.0) ? 4 : 0) ^ old_e09) & 4`, i.e. bit 2 is unconditionally overwritten
to `1` if a signed dot product is negative, `0` otherwise, every time this code runs. The dot
product (independently re-read off the disassembly, matching the decompile exactly):

```
dot = (ESI+0x50) * ((ECX+0x44 + 0.5*halfWidth.y) - (ESI+0x44))
    + (ESI+0x4c) * ((ECX+0x40 + 0.5*halfWidth.x) - (ESI+0x40))
    + (ESI+0x54) * ((ECX+0x48 + 0.5*halfWidth.z) - (ESI+0x48))
```

where `ESI` is the traffic-AI entity record (same record as every other `+0xe0x`/`+0x40` field
§18.5/§18.6 already document — `ESI+0x40/0x44/0x48` is the entity's own live position, per §18.5), the
`halfWidth` 3-vector comes from `0x00b47b40` (not independently opened), and `ECX+0x40/0x44/0x48` is a
position on a second record passed into `0x00b47c80` as its own argument (register-confirmed: `ECX` on
entry, moved to `EDI` inside the function body) — this second record's identity was NOT resolved.
`ESI+0x4c/0x50/0x54` is a second 3-vector on the entity, role not independently identified (candidate:
a lateral/right-axis basis vector, distinct from the `+0x64/0x68/0x6c` orientation row §18.6 names).

**CONFIRMED — the call chain from the known per-vehicle drive tick, `0x00b4d070` (§18.6's own entry
point), to the setter, via a third sibling call not previously documented in §18.6.** Independently
re-verified against the live Ghidra server (confirmed: `0x00b4ad30`'s only caller is `0x00b4d070`, an
unconditional direct call):

```
0x00b4d070 (per-vehicle drive tick)
  -> 0x00b4ceb0   (gated target-point refresh, §18.6 — already documented)
  -> 0x00b4ad30   (UNCONDITIONAL call, every tick — not previously in §18.6)
       switches on the entity's own drive-state byte +0xc69 (same field §18.6 already names):
         state 0xd -> calls 0x00b47730 (reads +0xe09 bit 2 via TEST, never writes it); if that
                      test comes back false AND +0xe08 bit 0 is set, ORs 0x20 into +0xe09 (bit
                      5, NOT bit 2) and returns; otherwise advances +0xc69 to 0xe.
         state 0xe -> calls 0x00b47f10 (a geometric gate: tests +0xe09 bit 1 (0x2) as an early-
                      out, then runs distance/closing-speed/alignment comparisons between the
                      entity (ESI) and a second record passed as its own parameter (EDI — a
                      different register slot from the "ECX" second record inside 0x00b47c80;
                      not proven to be the same object) against several tuning constants — reads
                      like an overtake/obstacle-pass-opportunity test, not an ordinary lane-
                      centerline check); ONLY if this gate returns true does it call 0x00b47c80,
                      the bit-2 setter.
         states 0xf/0x10 -> unrelated connectivity/overlap bookkeeping, no +0xe09 touch.
  -> (then 0x00b4d070's own +0xc69 switch: state 0xd -> 0x00b47490, the §18.6 steering-sign reader)
```

**Answer to the static-vs-dynamic question: dynamic, not a spawn-time/lane-assignment constant — but
throttled by a nontrivial gate, not recomputed unconditionally every tick.** `0x00b4ad30` runs every
drive tick (unconditional call from `0x00b4d070`), but it only reaches the bit-2 write when (a) the
entity's own `+0xc69` drive-state is `0xe`, and (b) `0x00b47f10`'s own multi-stage geometric gate
against a second record passes. On any tick where either condition fails, bit 2 keeps whatever value
it was last set to — there is no "clear every frame" or "set once at spawn" path; it is a live
recomputation gated behind closing-speed/alignment/distance preconditions.

**HIGH CONFIDENCE, not independently proven — semantic reading:** the dot-product sign test (entity's
own `+0x4c/0x50/0x54` vector · (second-record position + half-width offset − entity's own live
position)) reads as "which side does this other point currently sit on relative to my own lateral
axis," recomputed from current positions each time it fires. Given `0x00b47f10`'s own gate shape
(closing-speed/alignment/width checks against a second vehicle-shaped record, not a lane-segment
record), this reads more like an **overtake/pass-side decision** than a generic lane-centerline
cross-track sign. Team B should not assume bit 2 is a drop-in replacement for an ordinary lane-keeping
cross-track-error sign without accounting for the overtake gate it sits behind.

**OPEN:** the exact identity of the second record passed into `0x00b47c80` (as `ECX`/`EDI` inside the
function) and into `0x00b47f10` (as `EDI`) — same object or not, not proven; the role of
`ESI+0x4c/0x50/0x54` (candidate lateral/right-axis vector, not confirmed against any other reader);
`0x00b47b40`'s half-width output; and the magnitude/distance helpers inside `0x00b47f10`, none
independently opened. Next concrete step for whoever picks this up: open `0x00b47b40` and resolve
what object reaches `0x00b4ad30`/`0x00b47f10`/`0x00b47c80` as their second parameter.

### 18.6b `0x00b47f10` fully reconstructed — CONFIRMED gate logic and all three tuning constants read; likely root cause of Team B's gate never firing (2026-10-11)

**Urgent context:** Team B implemented §18.6a's overtake gate using CHOSEN (guessed) thresholds for
the distance/alignment/closing-speed checks, because the magnitude helpers inside `0x00b47f10` were
never independently opened. In a live 900-frame test, the gate never fired once. This subsection
reopens `0x00b47f10` and its helpers in full to give the real numbers.

**CONFIRMED — the three previously-cited tuning constants, read directly off their data addresses
(cross-checked against raw `.rdata` dwords, independently re-verified against the live Ghidra
server before splicing — all six constant reads below matched exactly):**

- `0x01117a4c` = `0x3f800000` = **1.0f**. Used TWICE in the gate for two unrelated purposes (steps 5
  and 10 below) — not one threshold.
- `0x0126d2cc` = `0x3f000000` = **0.5f**. Also used twice (steps 5 and 10).
- `0x012a2fb0` = `0x3e800000` = **0.25f**. Used once, gating an unopened helper `0x00dab5e0`, only
  when a second "lead vehicle" handle resolves.
- Seven more constants live in the same function and are necessary to reconstruct the real
  condition: `0x0125e270`=0.0f, `0x01116440`=64.0f (8² units), `0x01116444`=3.0f (raw units),
  `0x01117da8`=81.0f (9² units), `0x012a2d38`=0.1 (double, clamp floor), `0x0127e354`=0.1f
  (fallback literal for the same clamp), `0x01116448`/`0x0111644c`≈0.8 (double, margin scale).

**CONFIRMED — the two "magnitude/cross-product" helpers are NOT a cross product and NOT a
square-rooted magnitude; both are plain FPU dot-product idioms over 3-float vectors, no `sqrt`
anywhere — independently re-verified against the live Ghidra server before splicing, both bodies
matched exactly:**
- `0x0043aa90(vecPtr)` → `vecPtr[0]² + vecPtr[1]² + vecPtr[2]²` (float10/x87 ST0 return). **This is a
  SQUARED magnitude (`|v|²`), never a linear distance** — every comparison against its result is a
  squared-units comparison (confirms the 64.0/81.0 constants above are 8²/9², not 8/9).
- `0x00434f10(a, b)` → `a[0]*b[0] + a[1]*b[1] + a[2]*b[2]`, a plain 3-vector dot product.

**CONFIRMED — `0x00b47b40` is a vehicle bounding-extent accessor, not new math:** resolves the
passed object's handle pair via the same liveness/class-capability-table chain the main gate uses,
calls `0x00abae00(outBuf, handle)` and copies its 3-float result out. On failure it falls back to a
second handle pair at the object's own `+0xcc0`, and if that also fails, calls `0x00d9fc60()` (a
zero/default-fill, not opened) and returns that.

**HIGH CONFIDENCE, not independently proven — `0x00a78d30(obj)` is a scalar speed/velocity-magnitude
read:** a thin wrapper making an indirect vtable call through `[*obj+0x60]` (target not identified)
and returning only the 3rd float (`+0x8`) of whatever struct that call writes. Every call site in
`0x00b47f10` multiplies this scalar against the same object's own orientation row (`+0x64/+0x68/
+0x6c`) and adds it to that object's live position (`+0x40/+0x44/+0x48`) — used exactly like
`predicted = position + forward * speed`, a short-horizon dead-reckoning extrapolation.

**CONFIRMED — the full gate, step by step, with real units.** `0x00b47f10(ESI=own entity,
EDI=second record)`, returns bool in AL. Independently re-verified the forward-vector dot-product
test (step 3) against the live Ghidra server before splicing — both `EDI+0x64/0x68/0x6c` and
`ESI+0x64/0x68/0x6c` are read and compared against `0x0125e270` near the function's start, matching
the claim below:

1. Early-out unless `ESI+0xe09` bit 1 (`0x2`) is set AND (`ESI+0xe0c != 2` OR `0x00b473a0()` returns
   `> 1`) — else return false immediately. This is the same bit `0x00b47c80` tests as its own
   early-out, confirming this gate is strictly gated behind that bit already being set from a prior
   tick.
2. Resolve `EDI+0x16c0/+0x16c4` as a handle pair into a "lead/target" object (`leadHandle`), via the same
   class-table chain; `leadHandle` may end up null — a normal "no lead vehicle" case, not an error path.
3. Require (`ESI+0xe08` bit 0 set OR `leadHandle != 0`) AND `dot(ESI.fwd, EDI.fwd) <= 0.0` (raw
   `+0x64/+0x68/+0x6c` orientation rows, constant `0x0125e270`=0.0). **This requires the two
   vehicles' forward axes to be ≥90° apart — an oncoming/crossing-traffic test. A gate wired for
   same-direction tailgating/following traffic will structurally never satisfy this.**
4. Require ESI's own bounding half-extent (via `0x00abae00`) to satisfy `|halfExtent|² >= 64.0` (an
   8-unit combined-half-extent floor — "only run this check for vehicles at least this big"), else
   bail.
5. Call `0x00b47b40(EDI, &halfWidth)`. If `leadHandle != 0`: require `dot(halfWidth, EDI.fwd) >= 1.0`
   (FIRST use of `1.0`), then `dot(halfWidth, EDI.fwd)² >= 0.5 * |ESI.halfExtent|²` (FIRST use of
   `0.5`), else bail.
6. Compute a speed-extrapolated predicted position for the other vehicle (or `leadHandle`, if resolved):
   `predictedOther = other.pos + other.fwd * 0x00a78d30(other)`. Same for ESI itself.
7. `closingProjection = dot(predictedOther - predictedSelf, ESI.fwd)`. **Require
   `closingProjection >= 3.0`** (raw linear units — not squared, not a cosine). The real "is the
   other vehicle far enough ahead of me along my own forward axis" check.
8. `sizeDelta = EDI.halfWidth - ESI.halfExtent`. `fwdComponent = -dot(ESI.fwd, sizeDelta)`. Require
   `fwdComponent > 0.0`, else bail.
9. `passRatio1 = closingProjection / fwdComponent`;
   `passPoint = sizeDelta * passRatio1 + (predictedOther - predictedSelf)`;
   `passPointMagSq = 0x0043aa90(passPoint)` (squared magnitude). **Require `passPointMagSq <= 81.0`**
   (9² — a 9-unit radius bound on the projected pass point), else bail.
10. Recompute `fwdComponent` and blend: `margin = fwdComponent*0.8 + fwdComponent`, clamped to a 0.1
    floor. `finalRatio = closingProjection / max(margin, 0.1)`. **The real pass condition is
    `0.5 <= finalRatio <= 1.0`** — THIS is where `0.5` (second use) and `1.0` (second use) actually
    apply, on a dimensionless RATIO of two raw-unit distances, NOT independent absolute
    distance/alignment thresholds.
11. If `leadHandle != 0`: additionally require `0x00dab5e0() >= 0.25` (`0x00dab5e0` not opened — likely
    another closing-speed or brake-state scalar, OPEN).
12. Only if ALL of the above pass does the function return true, letting `0x00b4ad30` call
    `0x00b47c80`.

**Drop-in guidance for Team B:** do not implement this as three independent threshold comparisons.
The structurally minimal correct port is: (a) a forward-facing-divergence gate (dot of forward
vectors ≤ 0 — oncoming/crossing only), (b) an absolute own-vehicle-size floor (8-unit half-extent),
(c) a speed-extrapolated forward closing-distance floor of 3.0 raw units, (d) a squared-distance cap
of 81 (9²) on a computed "pass point," and (e) a final ratio test `0.5 <= (forward closing
distance) / clamp(sizeDeltaProjection*1.8, 0.1, inf) <= 1.0`. **If a 900-frame test never saw step 3
(forward dot ≤ 0) succeed — i.e. all observed traffic was same-direction, non-crossing — the gate
was structurally guaranteed never to fire regardless of distance/speed tuning, which is the most
likely root cause of "never fired once."**

**Secondary item: ordinary lane-keeping steering MAGNITUDE source — RESOLVED 2026-10-11, the curve
evaluator is fully inline in `0x00ae5290` itself, no separate evaluator function exists.** Full
body re-opened (`0x00ae5290`-`0x00ae5417`), independently re-verified against the live Ghidra
server before splicing. **CONFIRMED by direct disassembly:**

1. An early-out gate not previously documented: on entry, `0x00ae5290` calls
   `0x00d9e4c0`/`0x00d9e1e0` on a sub-object at `this+0x150` (same airborne/grounded-pair shape
   already seen elsewhere in §18 — e.g. §18.6a's `0x00ad3820`) and returns immediately, writing
   nothing, when that pair reads "airborne" (`0x00d9e4c0()==1 AND 0x00d9e1e0()==0`).
2. The input sign is **negated** (multiplied by -1.0) before anything else — the stored
   convention inside this function is inverted relative to the caller's own sign.
3. Clamped to `[-1,1]`, then (unless `this+0x164` bit 1 is set) the curve runs: it reads exactly
   two fields off the struct at `this+0x20` — **`+0x08` = a threshold/breakpoint (float, 0..1 range
   on the input magnitude)** and **`+0x0c` = a primary slope (float)**. For `|input| <= threshold`,
   output `= slope × |input|` (simple linear response below the breakpoint). For `|input| >
   threshold`, output continues linearly but with a SECOND slope computed on the fly
   (`(1.0 - slope×threshold) / (1.0 - threshold)`) so the curve lands exactly at `±1.0` when
   `|input| = 1.0` — a two-segment piecewise-linear response shaper (soft deadzone/ramp below the
   breakpoint, a steeper or shallower ramp above it depending on the two field values), not a
   lookup table, spline, or any higher-order curve. The sign is reapplied at the very end.
4. Result is clamped again to `[-1,1]` and written to `this+0x130` (confirming the existing
   write-site finding).

**Net: the curve struct at `driverInput+0x20` is CONFIRMED as the source of the continuous
steering magnitude — a 2-float `{threshold, slope}` response-shaper, not a lookup table.** The
`±1.0` sign from `0x00b47490`/`0x00b2bbc0` is the curve's x-input (after an internal negation);
the curve's own two parameters determine how sharply steering ramps in near zero vs. how it
behaves past the breakpoint.

**RESOLVED 2026-10-11, Team B-routed (SPEC TEAM, agent `ab93c2359d407d0c6`, independently
re-verified by direct query before splicing — the ctor's `MOV dword ptr [ESI],0x1182760` vtable
write and both `MOVSS` reads of the constant `0x01117a4c` into `+0x8`/`+0xc` matched exactly):
`this+0x20` is populated by a single, universal, hardcoded constructor,
`0x00aba420`, called unconditionally from the driverInput group constructor `0x00abde60` on
EVERY branch of that constructor's own vehicle-class size selector (`0x600`/`0xa40`/`0x580`
bytes) — i.e. the curve sub-object's own construction is identical for every vehicle, player and
ambient traffic alike; there is no per-vehicle-type variation and no `.xtbl` read. A fresh
24-byte (`0x18`) object is allocated, base-initialized via the generic Havok reference-counted-
object constructor `0x00c55b00` (vtable `0x011a04d8`), then overwritten with: `+0x00` vtable =
`0x01182760` (whose own slot `+0xc` is `0x00ae30c0`, the exact Havok driver-input override §18.3
already names); **`+0x08` threshold = 1.0 and `+0x0c` slope = 1.0, both the literal constant at
`0x01117a4c`**; `+0x10` = 0.0 (float, role not identified — no reader found); `+0x14` = 1
(byte/bool, role not identified — no reader found). Confirmed as the ONLY construction site in
the whole binary (exactly 1 xref on the vtable-pointer write). This independently corroborates,
and pins the exact numbers for, `spec-physics-runtime.md`'s own pre-existing line ~2136 note on
the same constructor ("hardcoded 1.0/1.0/0.0/true defaults, no xtbl read").

**Consequence for the curve math, since `0x00ae5290` always clamps its input to `[0,1]` before
evaluating the curve: with threshold = 1.0, `|input| <= threshold` is always true, so the
above-threshold branch is structurally dead for these real constants — the curve degenerates to
a pure identity pass-through** (`output = slope × |input| = |input|`, sign reapplied at the end).
There is no deadzone and no ramp for ordinary ambient-traffic/player steering: input passes
through unshaped. **Team B's own placeholder values (`kSteerCurveThreshold=0.2`,
`kSteerCurveSlope=0.3`) should be replaced with `threshold=1.0`/`slope=1.0` — i.e. drop the curve
shaping entirely and pass steering input straight through.**

- **Evidence:** direct disassembly of `0x00abde60` (driverInput group ctor, size-switch, the
  unconditional `0x00aba420` call on every branch), `0x00aba420` (the curve-struct ctor itself —
  allocation size, vtable write, both float writes from the shared constant), `0x00c55b00`
  (base-object pre-init), and a re-confirmation of `0x00ae5290`'s own consumer-side read
  (`[ESI+0x20]` then `[EAX+8]`/`[EAX+0xc]`, matching §18.6b exactly); a full xref on the
  vtable-pointer write `0x01182760` confirming exactly one construction site project-wide.
- **Confidence:** CONFIRMED (construction site, unconditional call path on every vehicle-class
  branch, both numeric values, single-source-of-truth/no-`.xtbl`-read claim). PLAUSIBLE (not
  independently traced through a live gameplay capture) for the identity-pass-through
  consequence, since it follows arithmetically from the confirmed constants rather than being
  observed directly.
- **Unknowns:** the semantic role of the struct's `+0x10`/`+0x14` fields (written by the
  constructor, no reader found); an exhaustive whole-binary search for a hypothetical later
  "tuning" write that pokes an already-constructed struct's `+0x08`/`+0x0c` directly (bypassing
  the vtable-write uniqueness signal used here) was not performed — judged unlikely given the
  independent `spec-physics-runtime.md` corroboration, but not formally excluded.

**OPEN:** the vtable target of `0x00a78d30` (confirms/refutes "speed" framing); `0x00dab5e0`'s own
body (step 11's helper); whether `leadHandle`'s resolution source and the main gate's own second-record
identity are the same kind of object — carried over from the existing OPEN note above.

### 18.7 Traffic driver reactions to the player — port recipe (2026-10-12, FINAL)

Static analysis only (listings and decompiles of the read-only `gp_shadow1` copy, plus raw `E8` caller
scans). The reactions are not done by the human AI modes of §6. The in-vehicle human stays seated, and
nearly every reaction is a write to the **vehicle-AI directive sub-record** at vehicle `+0xc68`
(called "the directive" below; its handle pair is at `+0x4e0`/`+0x4e4`). Only bail-outs (GetOut, and the
"aimed at" kind 6) touch the occupants, through the generic seat-exit call `0x00af3cd0(occupant, request)`.

#### 18.7.1 The directive's mode byte (`+0xc68`, i.e. directive `+0`) — the codes used here

The mode is set by `0x00b24e40(directive, mode, sub)` (thiscall; `0x00b26250` is a cdecl wrapper). The
setter runs the old mode's exit slot and the new mode's enter slot. These come from a 5-dword-per-mode
table at `0x01186da0`, with slots +0 exit, +4 tick, +8 enter, +0xc net write and +0x10 net read. Mode 11
skips the old mode's exit. The setter keeps the previous mode and sub-mode at directive `+2`/`+3`. It
clears directive `+8` bit 0x80 and `+9` bits 0x02/0x80; bit 0x80 of `+9` re-arms a 20000 ms timer. It
also sets **obey-traffic-lights** through `0x00b22a80(on)`: on for modes 0, 7, 8, 0xf, off for modes
1–6, 9, 10, 0xc, 0xd, 0x10. Modes 1–6, 9, 10, 0xc, 0xd and 0x10 also clear directive `+8` bit 0x04.
CONFIRMED.

| Mode | Meaning (evidence) | Set by |
|---|---|---|
| 2 | **Flee** (sub 5). Two forms: from an object handle (`0x00b464c0(directive, lo, hi)`, which stores the threat at `+0x58`/`+0x5c` and its position at `+0x238`, with `+0x228` bit 0 = 1) or from a point (`0x00b462a0(directive, &xyz)`). Same vetoes in both: the driver is a player (flag `0x21`), directive `+4` bit 0x80, or the co-op proximity veto (25 m). A non-owner forwards network opcode `0x42`. | below |
| 3 | **Ambient traffic / go back to lane following** (sub 7). `0x00b61cc0(vehicle, &block)`: a no-op when already 3; builds the cruise flags at vehicle `+0xfe8`/`+0xfe9` from the block; any player occupant clears `+0xfe8` bit 0x40 (the random cruise-speed bias). A target in the block whose team differs from the vehicle's (`+0x16e0` vs human `+0x1ca8`) is stored at `+0xff8`/`+0xffc` with `+0xfe9` bit 4. The default block comes from `0x004fa060`. | many (§18.7.4) |
| 4 | **Chase/attack a target** (sub 0xe, or 0xf when the block flag 0x100 is set and the driver's crew count is at least 2). `0x00b4d260(directive, &block)`, block built by `0x00b47110(block, lo, hi, isPolice)`. Refused when the driver is a player, or the driver's `+0x16c0` bit 0x4000000 is set. | collision Attack, threat kinds |
| 6 | Route-to-ambient re-entry (sub 0x11). `0x00b63ac0(directive, …)`, which falls back to clearing the directive (`0x00b46ca0`) when there is no road graph (`0x02705fe0`) or directive `+7` bit 4 (`never_go_ambient`) is set. | flee end |
| 11 | **Hold/wait** (sub 0x13). `0x00b50180(directive, lo, hi)` saves the previous mode and sub-mode at directive `+0x3c8`/`+0x3c9` and the handle at `+0x3c0`. A no-op when already 11 or 14. | carjack approach |
| 12 | Set by `0x00b37210` (sub 0) for a vehicle that passes `0x00ad31a0`. Not part of this chain. | — |

`0x00b1f6c0(mode)` = "mode is 11 or 14" (held). CONFIRMED.

#### 18.7.2 (a) Something in the path: wait, honk, threaten; the stuck give-up

- **Wait response** (`0x00b55460`, reached from the per-vehicle tick `0x00b58320`). It runs while the
  directive is mode **8** with `+0xc70` bit 2 set, no `0x00ad4160` result, and `+0xc70` bit 4 clear.
  Every **2000–4000 ms** (random) it draws the driver's `VehicleWaitResponse` from the personality record
  (`0x00507e30(driver)`, `spec-tables-traffic-ai.md` §11, bytes `+0x11..+0x13` = DoNothing / Honk /
  Threaten; draw `0x00507a00` = uniform over the weight sum).
  - Honk (1): calls `0x0055a520(vehicle handle)` (the horn) and adds the horn length `0x00559840()` to
    the next 2–4 s wait.
  - Threaten (2): re-arms in 500 ms.

  The result is cached at vehicle `+0x1114`; −1 at `+0x1110` resets. CONFIRMED.
- **Stuck give-up** (`0x00b20e20(vehicle, active)`, called from the flee tick and 8 other mode ticks).
  - It needs vehicle `+0x33` bit 8 and `+0x11e0` ≠ 0.
  - After **13000 ms** without progress it calls `0x00b1e410`. That re-issues mode 3 (ambient) when the
    directive is 2 (flee), 4 (chase) or any other mode except 3 and 0xf, which return "not handled".
  - A reverse-out window applies: **1000 ms**, or **2000 ms** in modes 4 and 8, with the progress check
    once 751 ms (`0x2ef`) have passed. The vehicle counts as stuck when it moved less than **4.0**
    (16.0 squared, `0x0127e214`) since the last sample (`+0x10d4`). Two such samples in a row also call
    `0x00b1e410`.
  - Hard stop: when within 1000 ms the larger of (distance moved)² and (speed ÷ (`+0xc58` × 2.0))² is
    below **6.25** (`0x01115350`), it commands brake 0, throttle 0.1 or 1.0 (`+0xc71` bit 0x10 picks)
    and steering 0 through the §18.6 dispatchers on the handle at `+0x1148`.

  CONFIRMED (constants read from the image).
- **Braking for a car or the player in the lane** is the §18.6 follow logic. `0x00b2bbc0` steers on a
  lead handle at `+0xcc0`/`+0xcc4` inside **15.0** (225.0 squared). Otherwise it applies throttle `0.01`
  (`0x3c23d70a`) plus brake. The traffic tick `0x00b4d070` drops into mode 3 (re-plan) when
  `0x00b49820` reports a blocked route, carrying `+0xe0a`, the `+0xcc0` lead and `+0xe09` bit 6 → block
  flag 0x800. **OPEN:** the exact "player ahead" gap distance. No separate pedestrian-in-lane test was
  found besides this lead-handle path.

#### 18.7.3 (b) Rammed or shot: the two entry points

**Collision response** — `0x007642f0` → `0x00b29e10(vehA, vehB, impulse)`:
- impulse ≥ **3.0** (`0x01116444`): clear `+0xc71` bit 0x80 on both vehicles (and re-arm 20000 ms if it
  was set);
- then `0x00b273e0`. It does nothing below impulse **6.0** (`0x012a2f7c`), nothing for `0x00ad31a0`
  vehicles, and nothing when either driver is dead (`0x0096f4f0`) or the two drivers are friends
  (`0x00950550`). An unmanned vehicle uses its `+0x1a3c` last-driver link. For each side whose driver is
  **not** a player:
  1. Skip when the directive mode is 2, 4, 9, 10 or 0xf, or mode 6 with `+0xed4` bit 0 set. Skip when
     the per-vehicle response timer started less than **10000 ms** ago.
  2. Draw `VehicleCollisionResponse` with `0x00507a50(record, otherIsPlayer)`. The weights are bytes
     `+0x14..+0x1a` = DoNothing, Honk, Threaten, Flee, GetOut, Attack, AttackPlayer. Against a player,
     AttackPlayer's weight replaces Attack's and an Attack result reads as AttackPlayer.
  3. When `+0xc6d` bit 2 is set, or the mode is 7, Attack/AttackPlayer become Honk (random ≤ **0.5**,
     `0x0126d2cc`) or Threaten.
  4. Act (jump table `0x00b27754`):
     - 0 nothing;
     - 1 Honk `0x0055a520`;
     - 2 Threaten `0x00b1e2c0`: a random occupant, weighted driver **0.8** / others **0.1** (`0x01173df8` /
       `0x0127e354`), plays gesture 0x3f1/0x3f2/0x3f3 through `0x00959b50`, with voice line hash
       `0x6ba26edd` when `0x00ad3290`;
     - 3 **Flee** `0x00b462a0(directive, other vehicle +0x40)` unless already 2;
     - 4 **GetOut** = mode 3 with block byte 0 bit 0x80, unless already 3;
     - 5/6 **Attack** = `0x00b47110(block, other driver, isPolice = vehicle team 5 or 6)` → `0x00b4d260`
       (mode 4). Skipped when directive `+5` bit 0 or `+4` bit 0x20 is set.

     Every non-zero result restarts the 10 s timer. CONFIRMED.

**Threat notification** — `0x00b33dc0(vehicle, attacker lo, hi, &pos, kind, extra)`. It records the threat;
the reaction is processed later by `0x00b34040` (from `0x00b2abe0`).

Kinds, by call site (CONFIRMED pushes):

| Kind | Caller | Trigger |
|---|---|---|
| 4 | `0x00b29d60` | from `0x00950320`; HIGH: a human hit the car |
| 5 | `0x0051f5c0` | the `Passenger` mode's event handler (§6 row 25); an occupant's perception event **76** (gunfire). Re-armed at most every **4000 ms**. |
| 6 | `0x0051f5c0` | occupant event **1** (aimed at) |
| 7 | `0x00afb8ad`, `0x00afd49a` | hijack attempt |
| 8 | `0x00b5680f` | the local player near the vehicle |
| 9 | `0x00ab76f0` | a vehicle part damaged; carries the damage source |
| 10 | `0x0051f5c0` | event **0x6b** on a civilian driver |
| reg | `0x00a9c230` | the damage callback, cases 0/1/2/4 |

Recording:
1. Gates: the vehicle has a live driver d (`0x004d7e00` of seat 0), and `0x00b33ab0` is false (the
   directive is "interruptible": mode 0, 5, 8, 11, 14, 0x10, or 6 without `+0xed4` bit 0, and neither
   `0x00b1f450` nor `0x007e2cd0`). The attacker is not the vehicle itself and not a friend.
2. Kind 6 additionally passes the cone test `0x00b33c70`. It stores `+0xdf0` = attacker, `+0xdfc` = 6 and
   a 750–1250 ms delay.
3. Other kinds need one of two conditions. Either d is a civilian (team 7, `0x00943ea0`-style) and
   `0x00b33a50` holds; or `0x00b33a80` holds, d can fight (`0x004e9110`), d is hostile to the attacker
   and, when d is team 7, the kind is not 8 or 10 and the attacker is not a player unless
   `0x009ba760`/`0x009ba7a0` holds.
4. On pass: `+0xdf0` = attacker, a 0–500 ms delay, `+0xdfc` = kind, and **`+0xdf8` = attack flag =
   NOT(personality `flee_events` bit 0x08 "bullet hit")**.

Processing (`0x00b34040`, when the delay expires and `0x00b33b20` allows it: modes 0, 5, 8, 0x10, or 6
without `+0xed4` bit 0; modes 11/14 wait):
- **kind 6**: unless `+0xc6c` bit 0x20, every occupant without `+0x1c9c` bit 8 gets a seat-exit request
  (`0x00af3cd0`, 250–1000 ms stagger), so the occupants **bail out on foot**.
- **attack flag 1** and `0x00b33a80` (not `0x00ad31a0`, `+0xc6d` bit 0 clear): **chase/attack**,
  `0x00b47110` + `0x00b4d260` → mode 4.
- otherwise, if `0x00b33a50` (`+0xc6c` bit 0x80 clear): **flee**, mode 2. For kind 8 with no attacker
  object, it flees from the player's position, or from its own position when within **30.0** (900.0
  squared, `0x01115340`) of the player.
- then a persona line, hash `0xfc4d52f7`, on d (`0x0070dcf0`) unless the voice check returns
  `0xaab95695`.

**A third path, only for a player-driven tank:** `0x005058e0`, the perception vehicle sweep (§10).
A civilian (team 7) driver near a player-driven vehicle that passes `0x00ad3140` (the special type 14,
tank) takes one of two actions, unless its directive is already 2, 3 or 8+`+0xee0` bit 2:
- **1 call in 4** (the global counter `0x013b8014` mod 4 = 0): flee from the player (mode 2);
- **the other 3**: mode 3 with the `0x004fa060` default block.

A civilian on foot gets `0x004eaae0` instead (§20.4). CONFIRMED.

Notoriety (§19.8) does not appear in any of these gates. Hostility is team/friend based (`0x00950550`,
`0x009d8ca0`), plus the player exemptions above.

#### 18.7.4 (c) Carjack: flee in the car, hold still, resist

- **On approach** (the routing hub `0x00afd4c0`, `spec-player-runtime.md` §10 step 16): when the player
  goes for seat 0, the driver d is examined if the directive is not 3 and not held.
  `0x004e6600(d, jacker)` decides **flee**:
  - false when d and the jacker share a team, or d's team is 1, 2, 3, 5 or 6 (gangs and police never
    pre-flee);
  - true when the jacker is a player and d's vehicle passes `0x0069d470`;
  - otherwise **`rand % 100 <= PreJackFleeChance`** (personality `+0x1d`).

  Flee → `0x00b464c0` and a 5000 ms vehicle timer. The car speeds off (mode 2). Otherwise, team-7
  `Passenger` occupants get `+0x8d0` |= 2 and condition row 9, and the vehicle **holds** (mode 11,
  `0x00b50180`). CONFIRMED.
- **Resist**: `0x004de220(victim, jacker)` (from `0x00afb220`) = different team **and**
  `rand % 100 <= JackFailureChance` (`+0x1b`). Its sibling at `0x004de266` reads `PostResistFleeChance`
  (`+0x1c`) the same way. CONFIRMED read sites. **OPEN:** what the hijack state machine does on each
  outcome (whether a failed jack becomes a brawl or the driver drives off) is in
  `spec-player-runtime.md` §10 (the hijack chooser `0x00afa540`), not re-derived here.
- **After the victim is out on foot**: the human force flag `dont_flee_post_vehicle_exit` (`+0x2bb`
  bit 3, §9.9) gates the on-foot flee. **OPEN:** its reader was not located in this pass. Hijack kind
  7 (above) also notifies the jacked vehicle's directive.
- **An NPC thief** (seating commit `0x00afbd90`): an AI human with flag `0x22` who takes the wheel with
  request bit 2 flees (mode 2) from the recorded threat, or from its own spot when there is none.

#### 18.7.5 (d) Back to normal, or gone

- **Flee tick** (`0x00b467b0`, mode 2):
  1. Sub-state **5**: plan with `0x00b45840`. It takes the road-graph node (`0x02705fe0`) farthest
     from the threat position (`+0x238`), walks it with the §18.1 lane walker
     (`0x00b1a760`/`0x00b1cc60`, flags 0x100) and commits the route (`0x00b516b0`). The planner re-runs
     every 200 ms until it succeeds; then a 5000 ms timer starts and sub-state becomes 6. A
     `+0x33`-bit-8 vehicle uses `0x00b45c40` instead, with escape points at distance bands chosen by
     the retry count `+0xe91` (<4, <8, more).
  2. Sub-state **6** (`0x00b46230`):
     - the stuck give-up `0x00b20e20` runs first;
     - on the timer, re-plan;
     - steer and brake with `0x00b2bbc0`;
     - **end test `0x00b46120`**, checked every 1000–2000 ms. It ends when the threat object is gone or
       dead, or when the vehicle is at least **70.0** from the threat object (`0x0116dba4`), or at least
       **45.0** from a threat point (`0x011169a8`). `+0xc6e` bit 0x40 or `+0xc72` bit 0x40 suppresses it.
     - When it ends: `0x00b63ac0(directive, 0, 0, 0)` → mode 6/0x11, which rejoins ambient traffic, or
       the directive is cleared when `never_go_ambient` is set or there is no road graph.

  The flee route still uses the lane graph. It does not leave the lanes; it ignores traffic lights
  (§18.7.1). **OPEN:** whether mode 2 also raises the speed cap. No speed write was seen in the planner
  or the tick.
- **Chase (mode 4)** has no end of its own here. It ends through the stuck give-up (13 s, → mode 3) or a
  later directive.
- **Despawn**: none of these reactions despawns a vehicle. Removal is the ordinary near-field budget and
  distance cull of §17.4/§17.4c.

#### 18.7.6 Port recipe

1. Give each AI vehicle a directive {mode, sub, prevMode, prevSub, threat handle, threat point, flags}
   and implement `setMode` with the table in §18.7.1. Include the traffic-light rule.
2. Load the `ai_personalities.xtbl` rows (§11 of `spec-tables-traffic-ai.md`) and resolve each driver's
   row.
3. In mode 8 when blocked (`+0xc70` bit 2): every 2–4 s draw a `VehicleWaitResponse`; Honk = horn.
4. Run the stuck give-up: 13 s without progress, or 2 samples < 4.0 m apart → mode 3.
5. Vehicle–vehicle contact with impulse ≥ 6.0 and a non-player driver not in modes {2, 4, 9, 10, 0xf},
   with the 10 s cooldown: draw a `VehicleCollisionResponse` from the 7 weights (Attack→AttackPlayer
   against a player; attack downgraded to Honk/Threaten 50/50 on `+0xc6d` bit 2). Dispatch: Honk;
   Threaten gesture; Flee-from-point; GetOut (mode 3 + bit 0x80); Attack (mode 4).
6. Threat kinds 4–10: record them with a 0–500 ms delay (750–1250 ms for kind 6). attack =
   NOT(`flee_events` & 0x08). On expiry: kind 6 → all occupants exit; attack → mode 4; else → mode 2.
7. Carjack approach: `PreJackFleeChance` roll (teams 1, 2, 3, 5, 6 never) → mode 2 + 5 s; else mode 11
   hold (save the previous mode).
8. Flee: plan to the road node farthest from the threat, re-plan every 5 s. End at 70 m (object) or
   45 m (point), or when the threat dies → mode 6 → ambient (mode 3).
9. Never react when the driver is a player; all reactions are owner-only (non-owners send opcode 0x42).

**OPEN (carried):**
- the player-ahead brake gap;
- the GetOut bit 0x80 consumer (who actually issues the exit for collision GetOut);
- the reader of `dont_flee_post_vehicle_exit`;
- the meaning of `0x00b1f450` and of `0x0069d470`;
- the semantics of modes 5, 7, 8, 9, 10 and 0x10 beyond the gates above;
- whether flee raises the cruise speed.

### 18.4 Open items

**Updated 2026-10-10:** the steering/target-point computation is now CONFIRMED at `0x00b1be40` (a
masked, lookahead-bounded forward graph-walk, §18.1); the junction kind-flag semantics are REFINED
(per-exit cache/already-resolved bits plus lane-legality filtering, not a discrete RNG/cost choice,
§18.2). **Item (a) is now substantially answered, §18.5:** live entity position does reach this
query layer (confirmed at `0x00b4c710`, gated by a cached-target/distance throttle), and a genuine
goal/nearest-distance cost comparison does exist one layer above `0x00b1cc60` (confirmed at
`0x00b1ce80` and `0x00b1e5e0`) — resolving §18.2's own "does a cost-based choice exist" question in
the affirmative, just one hop higher than originally searched. **(a) now FULLY RESOLVED, 2026-10-11
(§18.5's own follow-up above):** `0x00b1ce80`/`0x00b1e5e0`'s `originPos` is confirmed
live-vehicle-sourced in every traced path (`0x00b25620`, `0x007e3380`, and — one hop further up —
`0x00b1e5e0`'s caller chain via `0x00b2b630`); `destPos` is NOT uniform — confirmed to be either
another live object's position (via an unresolved singleton accessor, `0x009da4e0`) or a literal
externally-supplied scripted/network destination (via `0x00b25620`'s own caller `0x00b2d4d0`'s
byte-stream deserializer) depending on the call path — both are genuine, confirmed mechanisms. Of
the 2 originally-unopened `0x00b1cc60` callers, `0x00b45aa2` joins the same live-entity family;
`0x007d41a1` is a DIFFERENT consumer (a corner-curve point-pool system keyed on one of two global
singletons, `0x0012fdff0`/`0x0012feb20`), sharpening item (b) below rather than belonging to this
item. **(b) RESOLVED, 2026-10-11 (SPEC TEAM, agent `a0bad2c44b64da2b0`, independently re-verified
by direct query before splicing — `0x007d1040`/`0x007d3830`'s own reads of `0x02705fe0`, the BSP
pool addresses `0x0242dd90`/`0x0242ddcc` inside `0x0084b170`, and the GPS-module address range of
every `0x0012fdff0`/`0x0012feb20` xref site all matched exactly): the "corner-curve point pool"
framing was a misreading — there is no separate pool.** `0x007d1040`/`0x007d3830` are plain
accessors on the ALREADY-DOCUMENTED Traffic zone-data singleton `0x02705fe0` (`spec-zone-data-
format.md` §13.2) — the same global every other `0x18` traffic function reads; `0x007d1040`
computes a node-sequence distance, `0x007d3830` interpolates a position along a road segment
(endpoints directly, or via a quantized int16-triple sub-sample table for intra-segment curve
shape, scaled by the constant `0x0115a220` — this resolves §18.4's own long-standing open note
about that constant's value and about whose point pool this object is). `0x0084b170`'s "has the
vehicle passed this curve sample" hypothesis is **REFUTED**: it's a generic 2-point segment query
walked recursively through a global BSP-tree pool (`0x0242dd90`/`0x0242ddcc`, via `0x00dcdb00`→
`0x00dcd410`'s classic plane-classify-and-split BSP clipper) — a line/volume intersection test,
not a progress parameter; also called from the already-CONFIRMED ordinary lane-follower
`0x00b1be40` (§18.1), so it's shared generic infrastructure, not curve-specific. The 2 singletons
`0x0012fdff0`/`0x0012feb20` are per-player-slot records of the already-documented GPS
waypoint-navigation module (`spec-hud-runtime.md`'s "Waypoint/GPS" section, `0x007d1450`-
`0x007d8be0`) — confirmed by the identical 22-function xref set for both addresses, confined
entirely to that module's own address range, and by the module's master tick calling `0x007d39b0`
exactly twice per frame (once per player slot). **So `0x007d41a1` is the GPS route pathfinder
reusing the ordinary traffic lookahead machinery (`0x00b1cc60`/`0x02705fe0`) to route the GPS
line along the real street graph — not a traffic, camera, or cutscene consumer.** The GPS
player-slot question is already answered elsewhere in this project, found by reconciling against
it just now: `spec-hud-runtime.md`'s own already-CONFIRMED text (its own "Waypoint/GPS route
target" finding) states plainly that `0x0012fdff0` is for the local player and `0x0012feb20` for
a co-op partner, "selected by a 2-way selector, not an array index" — exactly the local-vs-remote
split this section's own earlier pass had only guessed at. No longer open.

**BSP pool identity RESOLVED, 2026-10-11, same-day follow-up (SPEC TEAM, agent
`ae950690639a4fa3f`, independently re-verified by direct query before splicing — the
`hood_display_tag`/`hood_owner`/.../`hood_bsp_tree` field-name strings and the `.?AVdistrict@@`/
`.?AVhood@@` RTTI tags at `0x01301adc`/`0x01301b1c` both matched exactly via raw-memory `ptrs`
read, the strongest evidence in the report).** CONFIRMED, not just high-confidence: each BSP pool
entry is a named city **district/"hood" (gang-territory) boundary volume** — the same concept
`spec-cellphone-menu-runtime.md`'s already-documented pause-map district-control meter and
`"STAG"` gang-territory-takeover sub-mode reference by name — not a visibility/occlusion volume,
water volume, exclusion zone, or Havok collision shape. Of the 4 registration functions: only
`0x0084d120` is a real constructor (reached via a data-pointer dispatch slot, `0x01164304`, like
other content-kind object constructors in this project) — it reads 9 named fields through a
generic key-value accessor (`hood_display_tag`/`hood_owner`/`hood_spawn_category`/
`hood_alt_spawn_category`/`hood_flags`/`hood_island_type`/`hood_income`/`hood_takeover_areas`/
`hood_bsp_tree`) from a SEPARATE named "hood definition" table — not any `.czn_pc` content-kind id
(not `0x2247`/`0x2249`/`0x2243`/`0x2246`) — builds the BSP tree from the `hood_bsp_tree` field via
the same shared primitive (`0x00dcd3d0`) Interior Triggers/Water Volumes use, and inserts the
record into the pool only if `hood_island_type==0` (islands excluded) and the pool (cap 15) isn't
full. `0x0084af30` is actually the pool RESET (not a registrar); `0x0084b0c0` is a
single-point "which hood contains this point" query (same BSP family as `0x0084b170`); `0x0084b110`
is the segment-test sibling of `0x0084b170` without the detailed recursive-walk output. The reuse
by traffic lane-following and the GPS module is explained: both plausibly need "which district is
the vehicle/route in" (per-district spawn category; route narration crossing district boundaries).
Genuinely still open, minor: the hood definition table's own file format/location; `hood_flags`
bits `0x40`/`0x4`'s exact meaning; `0x0084af30`'s sole caller `0x00707c80` (likely a level-load/
teardown routine, not confirmed). **(c) RESOLVED, §18.6/§18.3:** the literal Havok driver-input write site is
now CONFIRMED end-to-end — `0x00b4d070` (state `0xd`) → `0x00b47490` (heading-dot-product steering
sign) → the shared vehicle-command dispatcher `0x00a929e0`/`0x00a92640` → `0x00ae5290`/`0x00ae8fd0`,
writing `driverInput+0x130`/`+0x128`/`+0x12c`/`+0xb0` on the SAME `0x00ae30c0`-shaped instance layout.
**This closes the full §18 arc the orchestrator asked to finish** — the decision (§18.1/§18.2/§18.5)
and the write (§18.3/§18.6) are now both traced. New, narrower opens surfaced by closing this:
whether `0x00b4d070`'s `0xe`/`0x10`-state branch (`0x00b2bbc0`) is a second, parallel write path
— **CORRECTION 2026-10-11: already RESOLVED, §18.6 above (2026-10-10) — self-correcting my own
momentary miss here (checked this item before reading far enough forward in this same section).
`0x00b2bbc0` reaches the SAME write site as the state-`0xd` path (one hop shorter); its own
brake-bailout sub-case additionally calls `0x00b2ed40` (live position-snap via vtable `+0x40`),
documented in §18.6's own text at line ~4814-4824 as a SEPARATE, already-fully-traced effect, not
an open question. Nothing new to add — struck here to avoid duplicating §18.6.**; the precise
vector roles inside `0x00b47490`'s heading dot-product (`0x00abaf10` not opened).

Not individually decoded/traced in either pass: the segment-entry fields the main graph fixup leaves
alone (`spec-zone-data-format.md` §13.2); the `0x00a69330` tail-jump target; the record `+0x70`
name/key buffer; the `0x0115a220` quantization constant's value; whether `0x007d1040`/`0x007d3830`'s
object is pair #8's point pool or a shared generic point-list class (not cross-checked against any
camera/cutscene-spline spec); whether the Traffic record is strictly one-per-zone-file (not
population-checked); `0x00b1a560` (the shared growth/removal helper both pool mutators tail-call
into, not opened).

**[CONFIRMED — `0x00b1be40`'s full behavior and its caller `0x00b1cc60`'s parameter resolution, both
fully disassembled/decompiled via the live Ghidra server, 2026-10-10, independently re-verified
(function-boundary resolution and the 8-caller list both spot-checked directly against the Ghidra
server before splicing). HIGH CONFIDENCE — the lane-mask-filtering interpretation of branch handling,
the Havok single-input-surface argument (§18.3), and that `0x00b1cc60`'s 8 callers are the next
concrete trace target. OPEN — whether live vehicle state ever reaches this query, and the Havok
write-site, reported honestly rather than inferred.]** Full detail, every address, and the complete
raw disassembly: `scratchpad/traffic_ai_lane_following_2026-10-08.md` (original pass).

---

## 19. Notoriety (wanted-level) tracking and police-response dispatch (2026-10-08)

Traces where the live per-player notoriety value lives, what raises/lowers it, and how a level change
propagates toward police response — extending `spec-tables-progression.md` §6's XTBL loader-side account
(`notoriety.xtbl`/`notoriety_levels.xtbl`/`notoriety_spawn.xtbl`) and `spec-tables-traffic-ai.md` §6's
(`roadblock_notoriety.xtbl`) with their runtime consumers. Placed here, next to §17/§18, because its
central finding is exactly the same shape as those two: a system that is structurally separate from
ambient population but shares its underlying infrastructure (the Traffic zone-data singleton, the
`0x00d9e140`/`0x00d9e1e0` countdown-timer primitive family) rather than duplicating it.

### 19.1 The live record: a 4-slot array, {level, fraction} per track

The live value is a 4-slot array at `0x014A4F70` (stride `0x4C`) — one slot per "track": **0 = Luchadores,
1 = Deckers, 2 = Morningstar, 3 = Police**, confirmed from the runtime side (a 4-iteration loop bound) and
matching `spec-tables-progression.md` §4.6's save-file-side mapping exactly, via the same `track==3`
police test used independently in multiple functions below. Each slot stores **{level: s32 at `+0x00`,
fraction: float at `+0x04`}** — the fraction is points banked toward the next level, scaled against that
level's own `NotorietyLevelLimit` entry; crossing the threshold rolls the integer level up or down. A
per-track min/max **bound stack** (two independent 3-deep arrays each for min/max, at `+0x1c`/`+0x34` and
`+0x28`/`+0x40`, `-1` = unconstrained) backs the already-documented `notoriety_set_min`/`notoriety_set_max`
Lua natives (`spec-lua-api-behaviour.md` §6); the max-bound getter (`0x00605640`) defaults to **5** when
fully unconstrained, matching `notoriety_levels.xtbl`'s 5-level scheme directly from the runtime side.
Record `+0x10` is the decay-interval countdown timer (§19.3). Several other fields (`+0x08`/`+0x0c`,
`+0x14`, `+0x18` bit 0) are confirmed to exist and be written but their exact roles are OPEN.

`notoriety_set`'s core setter (`0x006079c0`, `spec-lua-api-behaviour.md` §6.7 names it but had not opened
its body) clamps the input against the bound stacks, truncates to an integer level index into
`NotorietyLevelLimit`, splits the fractional remainder into the native {level, fraction} pair, and — on an
actual level change — calls the shared dispatcher `0x00606b80` (§19.4). `notoriety_reset`'s helper
(`0x00609030`, `spec-lua-api-behaviour.md` §6.16) zeroes level/fraction, optionally clears the bound
stacks, re-arms the same 500ms decay-timer slot, prunes a small 800-byte "active notoriety alert/spawn
record" registry (`0x014A4C50`, stride `0x14`, keyed by a per-level "heat code" at `0x011286C4`) of
entries belonging to the reset track, and also calls `0x00606b80` on change.

**What the save stores versus what's recomputed — synthesized 2026-10-11, orchestrator-routed,
no new RE needed: every fact below was already independently established in this section and in
`spec-save-format.md`; this is the first place the two are connected directly.** `spec-save-format.md`
§10.6 (`0x4A24`-`0x4A33`) stores exactly **4 `float32` decay-rate MULTIPLIERS**, one per track,
default `1.0`, set by an unlockable of type 10 (`category := 1.0 + amount`) — a persistent,
purchase-driven buff to how fast notoriety decays, entirely separate from this section's own live
record. **Nothing else about live notoriety state is saved.** The actual per-track `{level,
fraction}` pair (`0x014A4F70`, §19.1 above), the decay-interval countdown timer (`+0x10`), the
min/max bound stacks (`+0x1c`/`+0x34`, `+0x28`/`+0x40`), the gain/decay-suppressed flags
(`0x014A4C0D`/`0x014A4C0F`), and the "active notoriety alert/spawn record" registry
(`0x014A4C50`) are all recomputed/reset every session — confirmed by their total absence anywhere
in `spec-save-format.md`'s own near-complete field map (99.5% of the save file attributed, per
Team B's own audit), not merely unsearched. Practically: a loaded save always starts at zero
notoriety on every track, regardless of how "hot" the player was when they saved — the only thing
that survives is the decay-SPEED buff, if purchased. **Two related but structurally separate
pieces of saved data, for completeness, not part of the 4-track system above:** the 5
"time-at-maximum-notoriety" lifetime stat counters (`spec-save-format.md` line ~369, stat ids
13-17, pure play-time tracking, not live state) and a single boolean "STAG notoriety" script
variable (`spec-save-format.md` line ~1240, bit 2 of the generic session-toggle byte at
`0x014FF6BB`) — a mission/script-level flag whose setter also updates the *lockdown-notoriety*
record's own flag, unrelated to the per-track level/fraction mechanism.

### 19.2 What raises it: a crime-report consumer, confirming `Check_Detection` is real

`0x006076a0` (newly opened) is the runtime reader of `notoriety.xtbl`'s per-activity records
(`spec-tables-progression.md` §6.1), confirmed field-for-field against that loader-side layout: it selects
the gang or police sub-block of the activity record, checks the current level is within that activity's
configured `Min_Level`/`Max_Level` window, and — when `Check_Detection` is set — additionally requires a
live player (local or remote co-op) to currently be detecting the actor (via `0x00606570`) before awarding
points. **This is the first direct runtime confirmation that `Check_Detection` really does gate on live
player detection**, not just a documented gloss. Points then flow through the add-points primitive
(`0x00606f40`): an early-out respects a newly-identified **`0x014A4C0D`** "gain suppressed" flag (the
direct sibling of the already-documented `0x014A4C0F` "decay suppressed" flag,
`notoriety_set_can_decay` — together they let a script freeze either direction of notoriety change
independently); otherwise the delta is added to the fraction and a roll-over loop advances the level while
the fraction exceeds each successive level's own point threshold, calling `0x00606b80` on a resulting
change.

### 19.3 What lowers it: a genuine decay tick, sharing a primitive with ambient population

`0x00607200` is a real, timer-gated decay tick: it tests the per-track decay-interval timer (record
`+0x10`) via **`0x00d9e1e0`** — **the exact countdown-timer-expiry primitive §17 §4.4 already documents
as the shared throttle underlying the ambient-population distant-tick's own ~1-second cadence.** On
expiry it computes a decay amount from the same `NotorietyLevelLimit` table (scaled by a constant at
`0x01123400`, value not read), subtracts it from the fraction, rolls the level down in the same manner as
§19.2's roll-up (in reverse), and calls `0x00606b80` on a resulting change. A separate, read-only HUD
decay-preview tick exists purely for display and is confirmed NOT to write back to the live record. **This
resolves `spec-tables-progression.md` §6.2's own flagged OPEN item** ("which `NotorietyLevels` set is
police vs. gang is positional... the loader alone does not say"): a decay-rate reader branches explicitly
on `track==3`, confirming **Set 0 (`0x014A45C4`) = Gang (tracks 0-2), Set 1 (`0x014A45DC`) = Police
(track 3)** — by positional analogy (not independently re-derived), `NotorietyLevelLimit`/
`NotorietyFirstDecay` are HIGH CONFIDENCE to follow the same Set 0/Set 1 = Gang/Police convention.

**No distance-based "lost the cops" mechanic was found** — an explicit search, not a skip: the decay tick
is purely timer/table-driven with no position read anywhere in its body, the crime-report function reads
player detection only for GAIN not decay, and the level-change dispatcher (§19.4) does no distance/position
computation anywhere in its own body. Reported as a negative result from the specific functions opened,
not an exhaustive whole-image claim.

### 19.4 The shared level-change dispatcher, and the police-response question

**Every real path that changes a track's level — `notoriety_set`, `notoriety_reset`, and organic
crime/decay — funnels through one shared dispatcher, `0x00606b80`** (9 confirmed call sites). ~~Its
body: (1) looks up the per-level "heat code" and calls a global heat/difficulty setter; (2) walks the
entire live AI-human roster...; (3) walks up to 2 (co-op) player pointers...; (4) re-arms a 2000ms
per-track timer — described as if these ran unconditionally, every call.~~ **CORRECTED, 2026-10-09
(full re-disassembly, `scratchpad/notoriety_rules_synthesis_2026-10-09.md`): the function branches on
the SIGN of the new level, and only the `newLevel <= 0` branch runs the roster/player/timer sequence.**

```
0x00606b80(newLevel, track):
    heatCode = heatCodeTable[track]      // indexed by TRACK, not level — corrected below
    call 0x004f6540(heatCode)             // generic AI state-rewrite, unconditional — corrected below

    if newLevel > 0:
        record[track].+0xc = -1
        call 0x009b7130(0, heatCode, 0, 1)      // team-relationship matrix write, see below
        return                                  // NO roster walk, NO player dispatch, NO timer rearm

    else (newLevel <= 0):
        call 0x009b7130(0, heatCode, 1, 1)
        [roster walk, player dispatch, timer rearm — as already documented below]
```

An ordinary **upward** step (organic crime gain, or a scripted increase staying positive) takes the
cheap branch only — it never touches the roster, never dispatches to a player, never re-arms the timer.
This sharpens why Handlers 1/2/3 (below) were never going to be "spawn a pursuer" calls: by construction
they are reachable **only on the clear-to-zero path**, exactly where a "release whatever was held for
this pursuit" cleanup belongs, not an escalation.

**Two further corrections to the heat-code step itself.** First, the 4-byte table at `0x011286c4`
(values `1, 2, 3, 5`) is indexed by the function's own **track** argument, not by level — proven by the
same index value being reused immediately after to address the live per-track notoriety-record array
itself (`0x014a4f7c` base, stride `0x4c`), which is unambiguously track-indexed. So the codes are
**Luchadores=1, Deckers=2, Morningstar=3, Police=5** — a track→code mapping, not a level→code one.
Second, `0x004f6540` is NOT "a global heat/difficulty setter" — full disassembly shows it is a generic
**AI known-hostiles state-rewrite pass**: for every live human on the roster, it walks two of that
human's own linked lists (`+0x1c54`/`+0x1c50`, most plausibly "known hostile" target lists) and, for each
node whose target is live/class-eligible and whose team matches the function's input value, rewrites the
node's own state byte from `{11, 12, 0x4e}` to `0x77` if currently one of those — a bookkeeping pass over
EXISTING AI's own threat-tracking state, unrelated to spawning, run unconditionally on every level change
regardless of direction.

**Genuine, previously-undocumented finding: `0x00606b80` is a fourth writer of the already-documented
team-relationship matrix (§12.7, `0x0262d940`), via its write primitive `0x009b7130`** — the same
primitive the Lua natives `team_make_allies`/`_unfriendly`/`_hostile` already use. Called from team 0
(the player/civilian baseline — the one team value the roster-loop switch above doesn't map to any gang/
police track) against the heat-coded team slot: **`newLevel > 0` writes `0` = HOSTILE** (matching
`_hostile`'s own write value exactly); **`newLevel <= 0` writes `1` = UNFRIENDLY** (matching
`_unfriendly`'s own write value — a stand-down, not a return to full neutrality). **This is the real,
confirmed answer to "which levels trigger police response" for the dimension of making EXISTING nearby
police/gang NPCs actually hostile: a binary trigger at the zero boundary** (any level ≥1 on a track flips
that track's relationship cell to hostile; the specific magnitude 1-5 is never read by this function, only
the sign) — **not** graduated by level. The graduated, level-specific part (how many NEW units, of what
kind, at what rate) is the separate spawn-production question, §19.5/§19.7.

The roster/player/timer sequence, confirmed only on the clear-to-zero branch: (2) walks the entire
live AI-human roster and, for each human on the affected track, re-checks eligibility and calls
`0x008addb0(human, 0)`. **Correction, 2026-10-08: `0x008addb0` is NOT the `+0x455` "force no spawn" flag
setter** — full disassembly (7 instructions: null-check, else forward to `0x00884760(*(param+0x3c))`)
shows it is a generic "single-gate numeric-mode" liveness wrapper, the exact one
`spec-lua-api-behaviour.md` §3.6/§7.33 independently document as "variant 2... a thin wrapper over
`0x00884760`," reused across dozens of unrelated systems (also confirmed NOT human-exclusive —
`spec-ai-runtime.md` §17.4c finds it gating vehicle eligibility identically). **The real `+0x455`
force-no-spawn setter is `0x008ecda0`** (a genuine single `mov byte ptr [ecx+0x455],al`), per
`spec-lua-api-behaviour.md` §3.6 itself. This does not change the higher-level structural claim — the
roster loop still has no allocation call in it either way — confirming this half is a
spawn-SUPPRESSION-eligibility reconsideration pass over already-existing pursuers, not a creation pass;
(3) walks up to 2 (co-op) player pointers and dispatches each through a 10-case jump table to one of 3
player-state handlers, each managing a different handle-pair field; (4) re-arms a 2000ms per-track timer
via the same `0x00d9e140` primitive family.

Of the three player-state handlers, one (terminal call `0x00b63ac0`) was opened in full: it gates on the
**Traffic zone-data singleton `0x02705fe0`** (`spec-zone-data-format.md` §13.2, the same singleton §17/§18
already use) and releases a vehicle/slot handle pair — but it has **46 distinct callers project-wide**, so
it is a shared generic utility, not a notoriety-specific spawn call; it's reported as HIGH-CONFIDENCE
circumstantial placement (something road/vehicle-related is released/reassigned here), not a confirmed
spawn primitive. A second handler is a trivial 2-field setter (2 callers, genuinely notoriety-specific, but
not itself a spawn call). **The third, Handler 3 (`0x00606e39`), opened 2026-10-08 (§19.7): confirmed a
third instance of the same "resolve a handle, check class/liveness, release it" shape as Handlers 1/2 —
not a spawn call either. `0x00606b80`'s own chain is now exhaustively characterized: none of its 3
handlers creates anything.** **The literal instruction that instantiates a police vehicle or roadblock at
a resolved world position was NOT found in `0x00606b80`'s own chain — it lives on a separate, sibling
per-frame tick instead, see §19.7.**

**Net verdict: police response is CONFIRMED structurally separate from the ambient "distant/LOD"
population system** (`spec-ai-runtime.md` §17) — no call-graph edge connects `0x00606b80`'s chain to
either `0x00b90960`/`0x00b93670` or their `0x0090faf0` dispatcher, and none of §17's 6 `"os_*"` toggle
categories names police/roadblocks. **The separate `"roadblock"` toggle registered at `0x0092ed10` is
NOT an apply/notify callback for a toggle category — §19.7 found it to be a plain debug-console cvar
registration, and traced the real roadblock-spawn tick as an independent sibling of `0x0090faf0`'s own
ambient-population ticks, not downstream of `0x00606b80` at all.** **But it is not wholly separate
either**:
Handler 1's gate on the Traffic singleton places it inside the exact same ~90 KB road-network consumer
region §18's traffic-AI lane-following material already identified. The honest reading: police-response
vehicle placement most plausibly rides the same underlying road-network graph ambient traffic AI walks,
through its own dedicated per-player trigger path, not through the LOD-cheap distant-population spawner.

### 19.5 `roadblock_notoriety.xtbl`/`notoriety_spawn.xtbl` — no runtime consumer found (honest negative)

An exhaustive literal-dword xref scan against `roadblock_notoriety.xtbl`'s destination grid (`0x014A50B8`,
`spec-tables-traffic-ai.md` §6) and `notoriety_spawn.xtbl`'s destination structures (`0x01308098`/
`0x013080F8`, `spec-tables-progression.md` §6.3) found hits only inside their own already-documented
loaders — no independent runtime consumer that re-cites either base address directly. Not proof of
absence (a consumer reached only through an intervening pointer, never re-touching the literal base,
wouldn't be caught by this method) — flagged as the single largest remaining gap. Most promising next
step: trace forward from dispatcher Handler 3 (`0x00606e39`, not opened) and from the `"roadblock"` toggle
category's own apply/notify callback (`0x0092ed10`, not identified). **Narrowed, 2026-10-08 (§19.7 §4):
both leads were chased and neither is the scheduler — the real, much narrower remaining gap is whoever
WRITES a `"roadblock"`-class (`0x02cc9900` slot `0x2c`) instance's own `+0xd8`/`+0xe0` slot table** (the
spawn-consumption side, traced exhaustively in §19.7, never configures its own slot count — it only fills
pre-populated slots), not a literal-address reference to either XTBL's destination.

### 19.6 Open items

~~The literal police-vehicle/roadblock spawn instruction (§19.4);~~ **RESOLVED 2026-10-08, §19.7**: a
third, previously-undocumented per-frame tick (`0x00930e20`→`0x00930db0`→`0x00930a40`→`0x0092ff20`),
sibling to (not downstream of) `0x00606b80`, with a literal position formula and 3 distinct type-specific
creation dispatches. `roadblock_notoriety.xtbl`/`notoriety_spawn.xtbl`'s runtime consumers (§19.5,
narrowed but still OPEN); ~~whether `"roadblock"`'s toggle category connects to `0x00606b80`~~ **RESOLVED
2026-10-08, §19.7: it doesn't — `0x0092ed10` is a plain debug cvar, and the real spawn tick is an
independent sibling of `0x0090faf0`'s ambient-population calls, not reached from `0x00606b80` at all**;
~~the crime-report function's ×3 points-bonus trigger condition~~ **CLOSED 2026-10-12, §19.8 G2:
pending-crime flag bit 1, from `0x00b81760` on the report's object (the meaning of that descriptor flag is
still OPEN)**; ~~several record fields' exact roles (`+0x08`/`+0x0c`/`+0x14`/`+0x18` bit 0, §19.1)~~
**CLOSED §19.8: these are the decay timer, the 2 s just-cleared timer, the network stamp (HIGH), and the
per-frame "hold at level ≥ 1" flag**; ~~the per-frame/per-tick top-level caller of the decay-tick driver~~
**CLOSED §19.8: `0x00702a50` → `0x00609dc0`; the real decay tick is `0x00607380`, and `0x00607200` is a
ramp-to-minimum tick (correcting §19.3)**; ~~whether a distance/line-of-sight "lost the cops" condition
exists~~ **CLOSED §19.8: there is no distance formula; escape means the AI "seen within 15 s" re-arm of
the first-decay timer stops.** Also corrected in §19.8: `Check_Detection` quarters the award (minimum 1)
rather than suppressing it (correcting §19.2).
New OPEN items from §19.7: the upstream notoriety-level→slot-table scheduling link (narrowed per above,
and further narrowed 2026-10-09 — ~~the allocation primitive itself is now named, `0x00930950`, only its~~ **[CORRECTED 2026-10-12, raw bytes: `0x00930950` is not an allocator. It is **slot 0 (the destructor) of vtable `0x01170108`**: its only reference is that vtable slot, and it reinstalls the vtable at `0x00930998`. The class's constructor is `0x009306f0` (it installs the vtable at `0x0093076d`). Its **only caller is the static initializer `0x01004010`, which builds one global instance at `0x026171d0`** at startup (atexit `0x010196d0`). So the roadblock manager is a **startup singleton**, not allocated per notoriety change. **Refined the same day:** vtable `0x01170108` has 9 slots (0 `0x00930950` destructor, 1 `0x00930850`, 2 `0x008d8df0`, 3 `0x005a4e60`, 4 `0x009307b0`, 5 `0x006948c0`, 6 `0x00930830`, 7 `0x00754410` the shared base teardown, 8 `0x009309d0`). It is followed directly by the class-name literal **`"roadblock_layout"`** (`0x0117012c`). So this is the **`roadblock_layout` world-object class**, a placed zone-data object type, and the startup instance at `0x026171d0` is its class prototype or descriptor, not a manager. Slot 8 (`0x009309d0`) calls `0x0092b070`/`0x0092fe20` and the liveness helpers `0x00853b10`/`0x00853ea0`: HIGH CONFIDENCE the per-instance spawn-or-activate method. **Port consequence:** roadblocks spawn at authored `roadblock_layout` objects from the zone data, chosen by the §19.7 tick, not at computed positions alone. The level→slot scheduling link must be found among that object's other virtual slots or its per-frame tick (§19.7) instead. The original wording is kept struck for history: ~~"only its caller remains unfound"~~.]**); `0x0092c280`/`0x0092d300`/`0x0092c580`'s own interiors; a slot's real authored
forward-offset distance; case 4's real-world identity (§19.7 §3.7, still blocked 2026-10-09 on a
zero-filled-at-rest class-table entry — needs a live capture or a broader static sweep); the innermost
allocation/placement-constructor instruction inside `0x0096eb30`/`0x00456e30` (substantially advanced
2026-10-09 for the roadblock-manager instance itself, §19.7; still open one hop deeper for the
ped/vehicle/case-4 occupant-creation path — the indirect `+0x2c` virtual call target was not opened).

**[CONFIRMED/HIGH/OPEN as marked above.]** Full detail, every address, and the complete raw evidence:
`scratchpad/notoriety_police_response_2026-10-08.md`.

### 19.7 The literal police/roadblock spawn instruction — found, on a track separate from `0x00606b80` (2026-10-08)

**FOUND — a complete, CONFIRMED chain from the per-frame driver to three distinct type-specific
creation calls, with an explicit position-resolve step.** This is a **third, previously-undocumented
per-frame population system**, riding the exact same per-frame dispatcher (`0x0090faf0`) §17.5 already
names but left unopened behind its own parenthetical "(roadblocks, cells, etc.)" — this section opens
exactly that parenthetical. It is NOT `0x00606b80`'s own chain (§19.4) — Handler 3 was opened and
confirmed to be another generic handle-release pattern, not a spawn call.

```
0x00702a50 (main per-frame world update, spec-player-runtime.md §5.1)
  -> 0x0090faf0 (per-frame dispatcher, §17.5 — SAME dispatcher ambient population uses)
       -> 0x00930e20  (per-frame "roadblock" population tick — gated on debug cvar 0x026171b0)
            -> 0x00930db0  (per-instance eligibility + despawn-timer test)
                 -> 0x00930a40  (THE SPAWN-ATTEMPT walk: re-centers anchor near a tracked reference
                                 position, walks a slot table, enforces a per-category per-TICK
                                 spawn-rate throttle)
                      -> 0x0092ff20  (POSITION-RESOLVE + 5-way TYPE DISPATCH)
                           -> case 2 -> 0x0092f160 -> 0x009d73f0 -> 0x0096eb30   [character/ped creation]
                           -> case 3 -> 0x0092f9d0 -> 0x00456e30(0x1e), vtable+0x84 [vehicle creation,
                                                                incl. a literal "Spike_strip" name check]
                           -> case 4 -> 0x0092fc20 -> 0x00456e30(0x2f)           [third, unidentified category]
```

**`0x0092ed10` is a plain debug-console cvar registration, not a toggle-category apply/notify
callback.** Full disassembly: unconditionally forces byte `0x026171b0` (the `"roadblock"` cvar) to 1 at
bootstrap, then registers it through the generic named-cvar primitive `0x0086d770` (thousands of
unrelated callers project-wide) — exactly matching §17.5's own careful phrasing ("registers a
`"roadblock"` toggle category — not itself a population pool"). The real per-frame gate reading this
flag is `0x00930e20`, called directly from inside `0x0090faf0`, structurally parallel to but entirely
independent of the ambient-population ticks (`0x00b93670`/`0x00b90960`) — a genuinely new call-graph
edge.

**The spawn-attempt walk (`0x00930a40`):** walks the roadblock-manager's own pre-populated slot table
(`this+0xd8`, stride `0x18`, count `this+0xe0`). Per unfilled slot, enforces a per-category, per-TICK
spawn-rate throttle (NOT a total-live-count cap — these 3 counters are zeroed fresh every tick by
`0x00930e20`): categories 0/1 share counter `0x026171a4` (at most 1/tick), category 2 uses `0x026171a8`
(at most 2/tick), category 3 uses `0x026171ac` (at most 4/tick), category 4 has no counter (always
allowed). If under the throttle, calls `0x0092ff20` (below). When no unfilled slot exists, instead
re-centers two tracked anchor positions by subtracting the delta a reference position moved since the
last tick (`this+0x40/+0x44/+0x48`) — HIGH CONFIDENCE, not independently proven, that this reference is
the owning player's own position.

**Position answer: `spawn_xyz = anchor_xyz + direction_xyz * slot->forward_offset`** — a point a
configured distance ahead along a resolved road-network direction (`0x0092c280`, sign-corrected via a
dot-product test, normalized), starting from an anchor that re-centers every tick against the tracked
reference position above. **Not on top of the player, and not a fixed world point — a point "down the
street," sliding as the player moves.** `slot->forward_offset`'s real authored metres value was not
resolved this pass (no live game state to read it from) — OPEN; the mechanism itself is CONFIRMED.

**Type dispatch (`0x0092ff20`, 5 cases):** case 0/1 writes into the resolved object's own `+0xc68` — the
exact per-player state-dispatch-byte offset `0x00606b80` itself reads (§19.4) — HIGH CONFIDENCE a "a
live co-op player is standing in this slot" hand-off, not creation. **Case 2 (`0x0092f160`): the literal
character/ped creation path** — reuses the cover-arc evaluator `0x008cd1d0` already documented at §15.7
for `cover_node` selection (a genuinely new, confirmed second consumer), then stages a creation request
through the project-wide-shared `0x009d73f0`/`0x00bb9070` pair (resolved against the global singleton
`0x02442750` — the same singleton `spec-tables-traffic-ai.md` §7's `roadblock_stag_lockdown.xtbl`
consumer already documents). **Case 3 (`0x0092f9d0`): the literal vehicle creation path** — includes an
explicit `__stricmp` match against the literal string `"Spike_strip"` (the classic SR3 police
spike-strip roadblock prop), then creates via a virtual call through a type-selected factory object
(`0x00456e30(0x1e, ...)`, vtable slot `+0x84`). **Case 4 (`0x0092fc20`): a third category**, same shape
(`0x00456e30(0x2f, ...)`), real-world identity unresolved — OPEN.

**What remains genuinely OPEN: the upstream notoriety→schedule link.** None of the functions above read
the live notoriety record (`0x014A4F70`) or `roadblock_notoriety.xtbl`'s own destination grid
(`0x014A50B8`, §19.5) — every one treats the manager instance's own slot table as pre-populated data.
This directly explains §19.5's own honest negative: the real consumer is most plausibly whatever
creates/(re)configures a `"roadblock"`-class (`0x02cc9900` slot `0x2c`, registered by `0x009306f0`)
instance's slot table in the first place — the next pass should look for whoever WRITES
`this+0xd8`/`this+0xe0` on such an instance (reached via that class's own vtable, `0x01170108`), not
re-scan for literal XTBL-address references (already shown not to work).

Full detail: `scratchpad/notoriety_police_spawn_instruction_2026-10-08.md`.

**Narrowed further, 2026-10-09: the class's own creation primitive is now named, address and all — who
calls it is still the open part.** The `"roadblock"` class descriptor's global address is pinned down:
**`0x026171d0`** (constructed at process start by a static initializer at `0x01004010`,
`MOV ECX, 0x026171d0 / CALL 0x009306f0`, with a matching atexit-registered destructor; an exhaustive
check — Ghidra's own reference index plus an independent raw-byte literal/call scan of the whole
image — found exactly these 2 references anywhere in the binary, nothing else touches this object
directly). Its 9-slot vtable (`0x01170108`) was read in full: 4 slots are generic base-class virtuals
shared by 100+ other classes (not roadblock-specific); the other 5 are class-specific and resolve to a
classic **fixed-size free-list pool allocator embedded inside the class descriptor itself** —
**slot 0** (`0x00930950`) allocates and tags a new element as class `0x2c`; **slot 1** (`0x00930850`)
bounds-checks and releases one back to the pool; **slot 6** (`0x00930830`→`0x009307c0`) is the pool's
own initializer, sized for exactly **32 elements of `0x3f0` (1008) bytes each** (large enough to hold
every field offset this section already found on the roadblock-manager "this" object, up to `+0x3e8`,
8 bytes from the end — strong structural evidence this pool IS the allocator for every live
roadblock-manager instance `0x00930e20`'s own roster walks, meaning **at most 32 can exist
simultaneously**); slot 4 (`0x009307b0`) returns a class-specific literal constant `0x7e94` (32404,
exact consumer not identified); slot 8 is the standard scalar-deleting destructor. **This makes
vtable slot 0 (`0x00930950`) the real creation primitive for a roadblock-manager instance** — but
WHO calls it, and whether/how that's gated on live notoriety level or `roadblock_notoriety.xtbl`'s
per-(team,level) row, was still not found: one hop further into the generic creation entrypoint
`0x00456e30` (already used for case-3/case-4 occupant creation above) shows it resolves via
`0x00456be0` through the same `0x02cc9900`/class-table chain, then makes an indirect virtual call
through the CALLING object's own `+0x2c` vtable slot (not the roadblock descriptor's) — a generic
"resolve-or-create, validate, commit-or-rollback" shape used by 133 distinct call sites project-wide,
which did not resolve to a roadblock-specific call this pass. **The open item is now precisely
bounded: whoever holds a pointer to `0x026171d0` (or resolves class id `0x2c` generically) and invokes
its vtable slot 0** — not found by static disassembly within this pass's budget; a live capture
(breakpoint/log on `0x00930950`, or on writes to a freshly-allocated element's own `+0xd8`/`+0xe0`) is
the natural next step. Full detail: `scratchpad/notoriety_rules_synthesis_2026-10-09.md`.

**CLOSED, 2026-10-09 — found, and the answer is "both": one of (at least) 6 named call paths into this
allocator is genuinely notoriety-gated; the other 5 are scripted/placement-triggered and bypass
notoriety entirely.** A direct-literal-reference search for `0x00930950` itself is exhaustively
negative (its only citation anywhere in the binary is inside its own vtable), proving any real caller
must resolve the roadblock class descriptor indirectly, through the generic engine-wide class-id table
at `0x02cc9900` (already documented elsewhere in this file) rather than by holding the descriptor's own
address. Since the class id is the literal `0x2c`, the compiler folds the table lookup into one fixed
address, `0x02cc99b0` — and an exhaustive scan of every reference to that exact address finds precisely
3 external functions reading it, each calling the generic creation dispatcher `0x00456e30(0x2c, ...)`
directly (byte-exact, confirmed by direct disassembly at every call site), which — via `0x00456be0`'s
own capacity-gate-then-allocate shape, already documented above — terminates at the allocator.

Tracing those 3 functions to their own real callers finds exactly one notoriety-gated path: **a
dedicated per-frame scheduler, `0x0060a8d0`**, driven from the identical per-frame dispatcher
(`0x0090faf0`) that already drives the ambient-population and roadblock-consumption ticks documented
above — a structural sibling of the consumption tick `0x00930e20`, not a separate mechanism. Every
frame it runs, `0x0060a8d0` loops 5 roadblock-grid "team" slots (4 distinct tracks — Luchadores,
Deckers, Morningstar, Police — with grid slots 3 *and* 4 both mapping to the Police track, confirmed
directly from the resolved switch statement), reads each track's own live notoriety level through a
6-instruction getter (`0x00605200`, confirmed to return `record[track].level` out of the already-
documented live notoriety array `0x014A4F70`, stride `0x4C`), and **requires that level to be in
`[1,5]` — level 0 ("no notoriety") skips the track entirely for that tick, not merely deprioritized.**
Only past that gate does it cross-reference `roadblock_notoriety.xtbl`'s own per-(level,team) grid for
a capacity/cooldown check (a byte-exact, field-by-field match against this project's own already-
published XTBL schema, §6 of `spec-tables-traffic-ai.md` — the real consumer's literal base address is
one row before the documented grid start, exactly the "intervening pointer arithmetic" blind spot that
explains why an earlier literal-address scan for the documented grid base itself came back negative)
and a per-track suppression flag from `notoriety_spawn.xtbl`, before finally calling into the same
`0x00456e30(0x2c)` chain that reaches the allocator. **This is checked fresh every time the scheduler
runs, every frame — not a one-shot gate evaluated once and cached.**

The other 5 named call paths found all bypass notoriety: two are the already-documented
`roadblock_stag_lockdown.xtbl` scenario's own state-machine drivers (a scripted boolean + count gate,
confirmed by address-cluster match against this file's own §7 traffic-AI documentation); one is a named
placed "spawn point" trigger object (resolved by handle, not notoriety); one is a per-object placed
trigger-volume "think" method (its own further caller not traced, OPEN); and one is a second, previously
undocumented "roadblock" subsystem — a separate singleton object (`0x024e7fe8`, distinct from the class
descriptor `0x026171d0`) registered under the literal name `"roadblock"` into a generic named-system
registry, whose own real trigger (what actually invokes its vtable slot) was not found this pass —
flagged honestly OPEN, HIGH CONFIDENCE it is a Lua-exposed or debug/console "request a roadblock now"
entry point given its registration and staging-buffer shape, not confirmed further. Full detail,
every address and cross-check: `scratchpad/notoriety_roadblock_spawn_caller_2026-10-09.md`.

### 19.8 Escalation rules — port recipe (2026-10-12, FINAL)

Static disassembly only (capstone over the image, plus the raw call scanner). **Two corrections to
§19.2/§19.3 come first; they change the port.**

- **`0x00607200` is NOT the decay tick.** It ADDS points. It is a timer-gated "ramp up to the minimum
  bound" tick: the timer is at `+0x10` and re-arms every 500 ms. Each tick adds
  `max(1, trunc(limit(set, level+1) × 0.02))`. The `0.02` is the double at `0x01123400`, which closes
  the "value not read" item. The step is capped at the points still needed to reach the minimum-bound
  level (`0x00604f90`). The level then rolls up, and the timer is disarmed (`-1`) once the level equals
  the minimum bound (`0x00604780`). This is how `notoriety_set_min` raises the level gradually, at
  50 ticks (25 s) per level. **The real decay tick is `0x00607380`** (below).
- **`Check_Detection` does not suppress an unwitnessed crime. It reduces the award** (rule G3 below).
  §19.2's "silently suppressed" is wrong.

**Record fields** (`0x014A4F70 + 0x4C·track`). Every timer is a single dword holding an absolute
expiry time. It is armed by `0x00d9e140(ms)`, tested by `0x00d9e1e0`, and `-1` means disarmed.
- `+0x00` level (s32, 0…5). `+0x04` fraction (float points banked inside the current level).
- `+0x08` **the decay timer** (closes §19.6 (e)). A crime arms it with the first-decay delay, and a
  decay step re-arms it with the decay interval.
- `+0x0c` **the "just cleared" window timer.** The clear-to-zero branch of `0x00606b80` arms it for
  2000 ms. Any upward change and the level>0 branch set it to `-1`. `0x00606410(track)` reads it as
  "this track was cleared within the last 2 s". For track 5 it reads the global timer at `0x012ED168`
  instead, which every crime report also resets to `-1`.
- `+0x10` the 500 ms ramp-to-minimum timer described above.
- `+0x14` a network stamp. At init it is set from `0x0086be20()`, the session object's `+0x68`.
  The co-op state receiver at `0x00609345` compares it against an incoming value and drops stale
  updates. HIGH CONFIDENCE.
- `+0x18` bit 0: **"hold at level ≥ 1 this frame".** `0x00608740(track)` sets it. That function has
  two AI-side callers (`0x004de8d4` and `0x005076fe`, both on AI-versus-player engagement paths). When
  the maximum bound is ≥ 1, it first raises a level-0 track to exactly 1.0 through `0x006079c0`, then
  sets the bit. While the bit is set, the decay roll-down floors at 1 instead of at the minimum bound.
  The master tick clears the bit on every track, every frame, after decay has run (`0x00609e31`).
  Net effect: **an AI of that team actively engaging the player keeps that track at level 1 or above.**

**Thresholds and maximum** (closes (b)). `set = (track == 3)`: 1 for police, 0 for the gangs.
`limit(set, L)` is `NotorietyLevelLimit` for level L, an xtbl field (`0x014A45F0 + 4·(5·set + L)`,
L = 1…5). It is the number of points that must be banked inside level L−1 to reach level L. **The
maximum level is the literal constant 5** (`cmp …, 5`) in every roll-up loop. The maximum-bound getter
`0x00605640` also defaults to 5. On reaching level 5 the fraction is zeroed and `0x00bd31f0(localPlayer,
…)` is called, which is most plausibly the time-at-maximum stat start. Base-game threshold values are
not in the executable: they come from the base `notoriety_levels.xtbl`, which was not read.

**Gain path** (`0x006085a0` enqueue → `0x00607960` drain → `0x00607840` → `0x006077d0` → `0x006076a0`
→ `0x00606f40`):
- G1. **Enqueue.** A report becomes a pending record in the 40-entry ring at `0x014A4C50` (stride
  `0x14`). The ring count is at `0x014A4580`. When the ring is full, the report is processed
  immediately. Record fields: `+0x00` activity index, `+0x04` team, `+0x08` timer armed with the
  activity's `Delay`, `+0x0c` session stamp, `+0x10` flags. For activities 0, 3, 4, 11 and 23, a
  pending duplicate (same activity, team and stamp) is dropped. For activity 20, reports are
  dropped once 5 or more are pending.
- G2. **The ×3 bonus** (closes (a)). It is **pending-flag bit 1**, set at enqueue to
  `0x00b81760(obj)`. That test is true when the report's optional object argument is non-null, its
  descriptor pointer at `+0x110` is non-null, and that descriptor's byte `+0x08` has bit `0x10` set.
  A null object gives no bonus. **The real-world meaning of that descriptor flag is OPEN**
  (two other callers: `0x005062d7` and `0x009baa1e`). Pending-flag bit 0 is the caller's "treat as
  witnessed" flag, which skips the detection test. Raw points: `P = Points(sub-block) × (bit1 ? 3 : 1)`.
- G3. **The award** (`0x00606f40`, `D` = witnessed). Reports are skipped when `Points ≤ 0`, or when
  the gain-suppress byte `0x014A4C0D` is set and the delta is positive. `D` is 1 unless
  `Check_Detection` is set, bit 0 is clear, and neither the local player (`0x009da4e0`) nor the co-op
  player (`0x009df3d0`) passes `0x00606570`. The award `A` is chosen as follows:
  - If level ≥ the maximum bound, or level ≥ the activity's `Max_Level`: `A = 0`.
  - Else if level < `Min_Level`: `A = D ? max(P, pointsTo(Min_Level)) : max(1, P/4)`.
    So a witnessed crime always lifts the level at least to its `Min_Level`.
  - Else: `A = D ? P : max(1, P/4)` (`P/4` truncates toward 0).
  - Then `A = min(A, pointsTo(Max_Level))`, where
    `pointsTo(T) = (limit(set, level+1) − trunc(fraction)) + Σ_{k=level+2..T} limit(set, k)`
    (`0x00604f90`, 0 when `level ≥ T`).
- G4. **Roll-up.** `fraction += A`; `+0x0c := -1`. While `level < 5` and `trunc(fraction) ≥
  limit(set, level+1)`: subtract that limit and increment the level. If the level passes the maximum
  bound, clamp to it with `fraction = limit(set, max+1) − 1`. At 5, `fraction = 0`. On a change, call
  `0x00606b80(level, track)`. Then, if the level is below 5,
  `fraction = max(fraction, 0.25 × limit(set, level+1))`, using the double `0.25` at `0x012A2EB8`:
  **a fresh level starts at least a quarter of the way to the next one.**
- G5. **The decay timer is reset by the crime.** Before the award, `0x00605020(track)` arms `+0x08`
  with `FirstDecay(set, level) × difficulty[+0x10]`. `FirstDecay` is read at
  `0x014A4594 + 4·(6·set + level)` and is already in ms. `difficulty[+0x10]` is the
  `difficulty_levels.xtbl` `Notoriety_Decay_Mult` reciprocal (`spec-tables-progression.md` §7),
  taken from the record that `0x005f4a50` picks. When the global mode byte `0x014A4C0C` is set, the
  delay is capped at 5000 ms. The identity of that mode byte is OPEN: the master tick toggles it
  together with the gain-suppress byte and `0x008ed270`.

**Decay** (`0x00607380`, closes (c)). It runs only when the decay timer `+0x08` has expired and the
gate `0x006096e0` passes. That gate fails if the decay-suppress byte `0x014A4C0F` is set, or if the
local or co-op player is in a vehicle of `Vehicle_Type` 2, 3, 4 or 5 (airplane, helicopter, VTOL or
watercraft; tests `0x00ad3160`, `0x00ad31a0` and `0x00ad3290`). **Decay is not paused by vehicles in
any other way, and it is not paused by distance.**
- Step: `fraction −= DecayRate(set, level) × saveMult[track]`. `DecayRate` is at
  `0x014A45C4 + 4·(6·set + level)`. `saveMult` holds the four floats at `0x014A4584`, the save-file
  decay multipliers of §19.1, defaulting to 1.0.
- Roll-down: while `fraction < 0`, add `limit(set, level)` and decrement the level. The floor is
  `floor = minBound`, or 1 when `minBound < 1` and `+0x18` bit 0 is set. On reaching the floor, set
  `fraction = 0`. On a change, call `0x00606b80`.
- Re-arm: if `level ≤ minBound` and `fraction ≤ 0`, set `+0x08 = -1` (decay finished). Otherwise
  re-arm with `1000 × difficulty[+0x10]` ms (`1000.0` at `0x012A2D90`). **The decay interval is
  1 s, scaled by difficulty.**
- (A positive DecayRate would take a mirror "gain" branch with the same award logic. Shipped data
  presumably never uses it.)

**"Seen by cops" suppression and "lost the cops"** (closes (d)). **No distance or line-of-sight
formula exists in the notoriety code itself.** Escape works by starving the first-decay timer. While
an AI perceives a player, per-AI code keeps re-arming `+0x08` through `0x00605100(team)`, which
resolves the team to a track and then calls `0x00605020`. The call sites are `0x009dd0a8`/`0x009dd2d9`
(police, track 3) and `0x009e274a`/`0x009e2894` (team resolved by `0x006051c0`). "Perceives" means
the AI's handle is in one of two 25-slot "recently seen" tables on the player, with stride `0x10`
and a timer at `+0x08` of each slot. The police table is at `+0x25C8` (`0x009dc590`); the generic
table is at `+0x2118` (`0x009dc4a0`). A slot counts only if its elapsed time is ≤ **15000 ms** (the
literal `0x3A98`). So **the player loses the cops once no pursuing AI has seen them for long enough
that `FirstDecay(set, level) × difficulty` ms pass with no re-arm**. Each table slot itself stays
"fresh" for at most 15 s after the sighting. After that, decay proceeds at 1 s steps as above. Other
re-arms: the `notoriety_set`/`_min`/`_max` paths (`0x00607bcb`, `0x00607e47`, `0x00608096`,
`0x0060848f`, `0x006094dd`, `0x006095a9`). `0x00607e15` arms a flat 5000 ms first.

**The per-frame driver** (closes (c), last part). `0x00702a50` calls `0x00609dc0` at call site
`0x00702f04`. The per-frame order inside `0x00609dc0` is:
1. `0x00607960` drains due pending crimes (G1).
2. If not a network client, for each track 0…3: `0x00607200(set)` ramps toward the minimum bound.
3. For each track: if `0x006096e0()` passes, `0x00607380(set)` runs decay; then `+0x18 &= ~1`.
4. Maintain the `0x014A4C0C`/`0x014A4C0D` mode bytes.
5. Every expiry of the `0x012ED164` timer, call `0x006069c0` (co-op sync, HIGH CONFIDENCE).

**Level → response** (f).
- Hostility: binary at level > 0 (§19.4).
- Roadblocks: `0x0060a8d0` requires level 1…5 and reads `roadblock_notoriety.xtbl` row `level−1`
  for the track (§19.7).
- `notoriety_spawn.xtbl` `level_info` is keyed by the same 0…5 level (`spec-tables-progression.md`
  §6.3). Its runtime reader is still not found.
- `RoadblockSpawnTime*`/`BruteSpawnTime*` (`0x014A4BA8`/`0x014A4B48`): the only literal-address
  references are in the loader, so their consumer is OPEN, as in §19.5.

**Port recipe.**
1. Keep `track[4] = {level, fraction, decayT, clearedT, rampT, netStamp, holdFlag, min/max stacks}`,
   with `set = (track == 3)`.
2. On a crime: enqueue it with the activity `Delay`, `bonus = victimDescriptorFlag0x10`, and
   `witnessedOverride`. When it comes due, apply G3–G5.
3. Each frame, in order: drain the queue; run the ramp-to-minimum tick (500 ms, 2 % of the next
   threshold); run decay if `decayT` has expired and the gate passes; clear `holdFlag`.
4. Decay step: subtract `DecayRate × saveMult` every `1000 × diffRecip` ms. Roll down with
   `limit(set, level)`, floor at the minimum bound (or 1 while held), and stop at the floor with
   fraction 0.
5. Each AI that has seen a player within 15 s re-arms that team's `decayT` to
   `FirstDecay(set, level) × diffRecip`. AI in combat sets `holdFlag` (level ≥ 1).
6. On any level change, call the §19.4 dispatcher (hostile when level > 0; stand down and start the
   2 s `clearedT` at 0). The roadblock scheduler then reads the level directly every frame (§19.7).
7. Level cap 5; thresholds from `NotorietyLevelLimit`; a new level's fraction is at least 25 % of
   the next threshold.

**[CONFIRMED — disassembly for every rule, constant and address above, except items marked HIGH or
OPEN. HIGH: the `+0x14` network-stamp role, the AI-engagement meaning of the `0x00608740` callers,
and the `0x006069c0` sync role. OPEN: the meaning of the ×3 descriptor flag; the identity of the
`0x014A4C0C` mode byte; the spawn-timer arrays' consumer; shipped threshold values.]**

---

## ~~9.~~ ~~10.~~ ~~11.~~ ~~12.~~ ~~13.~~ ~~14.~~ ~~15.~~ ~~16.~~ ~~18.~~ ~~19.~~ 20. Clean-room check

- Addresses are plain hex throughout. There are no Ghidra auto-names for functions, globals or labels,
  no decompiler variable names, and no pasted pseudocode. Two shared stubs carry library symbol names in
  the database because of identical-code folding; they are cited by address only. Mode and sub-state
  names are the engine's own data strings read from `0x012e1538` and `0x012e1168`; the two network tag
  strings in §3.1 are engine data strings too.
- The private Ghidra copy used while deriving this material was deleted after the dumps.
- **A1 (§9), 2026-10-03.** The same conventions hold. Goal names, action names and the 41 force-flag
  names are engine data strings, read from the goal table `0x012e22f0`, the action table `0x012e1c60`
  and the network-tag literals of the setters `0x004df990`…`0x004e3530`. Behaviour-record and
  action-table field names are the shipped `.xtbl` element names, cited from
  `spec-tables-traffic-ai.md` §10 and `spec-tables-weapons-combat.md` §11.1. The stub `0x00db60a0`
  is cited by address only. A1's private Ghidra copy (`tools\gp_aiA1`, including its private
  instruction-scan script) was deleted after the dumps.
- **Scan result after the A1 edit** (run on this file as saved, 2026-10-03). Two checks were run;
  the token lists are not repeated here because writing them out would trip the second check.
  - The project's standard word-bounded regex for decompiler variable-name families, stack and
    register-input names, and auto-generated function, data and label names: **0 hits**.
  - The plain-substring backstop for the same families: **0 hits**.
  - **0/0**, the same as the pre-edit file.
- **A2 (§10), 2026-10-03.** The same conventions hold. The three voice-line names quoted in §10.3
  ("ouch", "stop it", "sounds like trouble") are engine data strings at `0x01117600`, `0x01117608` and
  `0x01117610`. Float values are read from the executable image and quoted as numbers. A2's private
  Ghidra copy (`tools\gp_aiA2`, including its private function-list script) was deleted after the dumps.
- **Scan result after the A2 edit** (run on this file as saved, 2026-10-03), with the same two checks
  as for A1:
  - the word-bounded regex: **0 hits**;
  - the plain-substring backstop: **0 hits**;
  - **0/0**, unchanged from the A1 baseline.
- **A3 (§11), 2026-10-03.** The same conventions hold.
  - Action and goal names are engine data strings (§9.5). Behaviour-record field names are the shipped
    `.xtbl` element names, cited from `spec-tables-traffic-ai.md` §10.
  - The table contents quoted in §11.1 and §11.4 (goal lists, behaviour action lists,
    `combat_actions.xtbl` rows) are data read from the shipped `misc_tables.vpp_pc`.
  - The shared stubs `0x006948c0` and `0x00db60a0` are cited by address only. Float values are read
    from the executable image and quoted as numbers.
  - A3's private Ghidra copy (`tools\gp_aiA3`) was deleted after the dumps. Its private instruction-scan
    scripts lived in the session scratchpad only.
- **Scan result after the A3 edit** (run on this file as saved, 2026-10-03), with the same two checks
  as for A1:
  - the word-bounded regex: **0 hits**;
  - the plain-substring backstop: **0 hits**;
  - **0/0**, unchanged from the A1/A2 baseline.
- **A4 (§12), 2026-10-07.** The same conventions hold. The voice line quoted in §12.3 ("call a doctor")
  is an engine data string at `0x011175c0`. Tunable names (`Ai_fov_*`, `Pain_sound_radius`,
  `Grenade_shout_radius`, `Player_friendly_fire_threshold`) are the shipped `tweak_table.xtbl` element
  names, cited from the real base-game file (`tools/ai_base_xtbl/tweak_table.xtbl`) and the registry
  `spec-tables-progression.md` §10.10 already specifies. Float and double values are read from the
  executable image (or, where the image reads zero, from the literal operand of that constant's own
  one-instruction startup stub) and quoted as plain numbers. No Ghidra project was used for this unit;
  all reads were done with `tools/harnesses/exe_va.py` plus `capstone` through disposable scratch
  scripts, deleted at the end of the session along with the rest of the session scratchpad.
- **Scan result after the A4 edit** (run on this file as saved, 2026-10-07), with the same two checks
  as for A1:
  - the word-bounded regex: **0 hits**;
  - the plain-substring backstop: **0 hits**;
  - **0/0**, unchanged from the A1/A2/A3 baseline.
- **A5 (§13), 2026-10-07.** The same conventions hold. All addresses are plain hex; field/offset names
  already established elsewhere in this file (`+0x4a5`/`+0x4a6`, `+0x8d0`, `+0x450`, `0x02cc9900`,
  `0x008addb0`, `0x00853b30`, `0x0097e280`, `0x00614d00`) are cited, not re-derived. The two trajectory
  solvers' confirmation includes an empirical Unicorn-emulation oracle (11 input/output vectors against
  the real function bytes, §13.2.1) — one disclosed stub (`0x00853b30` forced to a literal `eax` value)
  was needed only for the one call site that depends on the runtime-populated `0x02cc9900` table (the
  same table §13.1's own class gate depends on, confirmed not file-backed); the stub is reported as a
  stub, not silently folded into a CONFIRMED result. No Ghidra project was used for this unit; all reads
  were done with `tools/harnesses/exe_va.py` plus `capstone`, and the oracle runs with
  `tools/harnesses/unicorn_call.py`, through disposable scratch scripts, deleted at the end of the
  session along with the rest of the session scratchpad.
- **Scan result after the A5 edit** (run on this file as saved, 2026-10-07), with the same two checks
  as for A1:
  - the word-bounded regex: **0 hits**;
  - the plain-substring backstop: **0 hits**;
  - **0/0**, unchanged from the A1/A2/A3/A4 baseline.
- **Navpoint/`cover_node` unit (§15), 2026-10-08.** The same conventions hold. All addresses are plain
  hex. Property/class name strings (`navpoint_type`, `navpoint_flags`, `navpoint_extra_info`,
  `audio_marker`, `navpoint_sync`, `cover_pos`, `cover_orient`, `cover_type`, `cover_index`,
  `cover_angle_left`/`_right`, `cover_def_angle_left`/`_right`, `"navpoint"`, `"cover_node"`) are
  shipped engine/content strings read directly from `.rdata`, cited the same way this file already
  cites comparable strings elsewhere. No Ghidra project was used; all reads were done with
  `tools/harnesses/exe_va.py` plus `capstone` through disposable scratch scripts (never committed to
  `tools/harnesses/`), deleted at the end of the session.
- **Scan result after the Navpoint/`cover_node` edit** (run on this file as saved, 2026-10-08), with
  the same two checks as for A1:
  - the word-bounded regex: **0 hits**;
  - the plain-substring backstop: **0 hits**;
  - **0/0**, unchanged from the A1-A5 baseline.

---

## 20. On-foot ambient pedestrian runtime: sidewalk spawn, path-following, idle behaviour, and the
gunfire-to-flee trigger chain (2026-10-09)

Closes `purrsian-04`'s wave 6 item 2. Dispatched as 3 parallel sub-investigations (spawn/placement;
walking+idle; gunfire-to-flee), cross-verified against each other and against this project's own
existing text, with one connective thread closed by the orchestrating synthesis itself.

### 20.1 Spawn/placement — a genuinely separate near-field pedestrian subsystem, traced end to end

**The near-field (close-to-player, fully-simulated) on-foot pedestrian population is NOT spawned by the
distant/LOD tick `0x00b90960` (§17.3).** A second, previously entirely undocumented near-field
PEDESTRIAN spawn subsystem exists, structurally parallel to §17.4c's near-field VEHICLE system, traced
end to end from the shared per-frame dispatcher `0x0090faf0` to the literal human-creation call. **This
directly closes §17.3's open promotion/distant-vs-near-field question for pedestrians the same way
§17.4c closed it for vehicles: no promotion/hand-off exists — the near-field tier spawns its own,
independent population directly on the sidewalk graph.**

**Full chain, CONFIRMED by direct disassembly, every link:**

```
0x00908fb0 (bootstrap, existing spec) -> 0x009142a0 (subsystem init)
0x00702a50 -> 0x0090faf0 (per-frame dispatcher, re-read in full, extends §17.5)
  -> 0x009149a0 (throttle gate: not blocked, "os_ped" toggle on, both registries non-empty, two
                 independent per-registry elapsed-time checks both pass)
  -> 0x0090fa60 (dual-purpose: configure vs. per-frame work; checks "os_ped" toggle array
                 0x026093a4..+7, then Sidewalks load-gate 0x00bbdd90)
       -> 0x0090c6d0 (THE NEAR-FIELD PEDESTRIAN TICK)
            -> 0x009085f0 (builds a filtered candidate-segment array, Sidewalks singleton 0x02924d40)
            -> per candidate: resolves two endpoint node positions from a 24-byte-stride Sidewalks
               sub-array (distinct from the already-documented 36-byte-stride one, spec-zone-data-
               format.md §13.2), computes real-world segment length (0x00da1290), builds a
               LENGTH-WEIGHTED cumulative distribution — a genuine consumption of the graph's real
               topology, not just its node count (unlike the distant tick, §17.3)
            -> spawn budget = min(local headroom, the SAME shared 150-human ceiling 0x009086d0 §17.4c
               already documents) x a rate constant
            -> per attempt (<=3 retries): weighted-random segment pick (0x00dab5e0) -> 0x0090b840
                 -> 0x0090b840 (ONE SPAWN ATTEMPT): random point along the chosen segment, offset
                    sideways -> 0x0090b0a0
                      -> 0x0090b0a0 (CREATE + INITIALIZE ONE NEW PEDESTRIAN, ~1980 bytes, the largest
                         function in the chain): difficulty gate; rejects within ~90 units of the
                         camera (no pop-in next to the player) and beyond a second distance threshold;
                         rejects if visible to the current (or splitscreen) camera; a ground-validity
                         check; resolves a character model; several attribute/eligibility rolls ->
                         call 0x00bb9070 (the actual human-object creation call) -> post-creation setup
```

**Critical, directly-checked negative: the creation routine `0x0090b0a0` never calls any of the 4
`sidewalking`/`wandering`/`sidewalk_leashed`/`dont_use_sidewalk` force-flag setters** (zero occurrences,
checked by grepping the complete disassembly of its own body). This refutes the task's own working
premise that finding a caller of the `sidewalking` setter would lead to the spawn code — the spawner was
found by a completely different route (the per-frame-dispatcher chain above) and, once found, does not
touch the flag at all. Two HYPOTHESIS/OPEN explanations, not resolved: (1) `sidewalking` may simply be
the AI sub-object's own default state at construction, with the 41 force-flag setters existing to
*override* that default for scripted/network/Lua purposes, not to establish it; (2) the flag may be set
by a later step outside the creation routine (e.g. the human's first AI/goal-evaluation tick). The
question is sharply narrowed from "find the spawn code" (now DONE) to "find why newly-spawned near-field
peds walk sidewalks without an explicit force-flag call."

**The 4 flag setters, their bit positions, and an exhaustive caller census (all CONFIRMED):**

| Flag | Address | Byte/bit |
|---|---|---|
| `sidewalking` | `0x004e2c50` | `+0x2b8` bit 0 |
| `wandering` | `0x004e3530` | `+0x2b8` bit 1 |
| `sidewalk_leashed` | `0x004e2ad0` | `+0x2b8` bit 2 |
| `dont_use_sidewalk` | `0x004e1a60` | `+0x2bb` bit 5 |

An exhaustive raw `CALL rel32` opcode census (not this project's usual literal-4-byte-match method,
which does not catch relative calls — flagged as a gap in the standing xref method, worth fixing for
future "who calls this" work) found: **every confirmed caller of all 4 setters operates on an
already-resolved, already-EXISTING human** (session/handle lookup, never fresh allocation) — a named/
tracked-character task dispatcher, a "release back to civilian defaults" batch resetter, the inverse
("leaving sidewalk behavior on entering combat/alert states," 7 near-identical case blocks, cross-
referenced to §20.4 below), and the generic Lua/network by-name force-flag dispatcher (`0x008b14c0`).
**No caller is a bulk/population spawner.** One caller (`0x006af664`) has zero references anywhere in
the whole PE image by either direct call or static pointer — reached only by an indirect/vtable call
this method can't find, or a function-boundary misidentification; flagged OPEN.

**Side finding: `0x009142a0` (the bootstrap init) builds real infrastructure, not just a timer** — two
instances of a generic registry/container class (vtable `0x0116f124`) and one of a different class
(vtable `0x0116f1f8`), a 9-slot sentinel pool, and the standard 5000ms re-arm timer (matching §17.5). A
second, smaller, related pool/timer (`0x00917d10`, its own 2-node circular linked list, 20-60s cadence)
is functionally CONNECTED to the same subsystem (one of its own helpers, `0x00917da0`, calls into
`0x00914390`, the same candidate-picker `0x009142a0`'s own registry feeds) but its distinguishing role
is not pinned down — HIGH CONFIDENCE a secondary/related entry point (e.g. scripted or notoriety-
triggered), not independently confirmed. **Checked and found inconclusive, not resolved either way:**
whether `action_node_groups.xtbl`'s `Convert_to_ped_min_delay`/`Convert_to_ped_max_delay` fields
(`spec-tables-traffic-ai.md` §13.1) connect to this subsystem — all 8 real references to that array were
read directly and none touches those two record offsets; `0x00917d10`'s own literal 20000-60000ms timer
is suggestively similar in shape but is a hardcoded engine constant, not a value read from the xtbl.

**Correction to §17.6: `"os_ped"` (`0x026093a4`) is NOT inert despite having no registered notify
callback** — it IS the enable toggle this entire near-field pedestrian chain checks every frame
(`0x0090fa60` reads the 8-byte array inline). It has no callback because this subsystem polls the toggle
directly rather than being pushed a change notification — a different wiring style from the categories
that do have one, not an indication of disuse.

### 20.2 Walking/pathing — sidewalk and wander are `Idle`-mode sub-behaviours, not separate modes

**Confirms the task's own hypothesis directly.** No mode among the already-documented 44 is named
"Sidewalk" or "Wander" — the mechanism lives entirely inside `Idle`'s (mode 0) own tick dispatch on the
shared sub-state byte (`human+0x500`), via seven previously under-documented entries in the 177-entry
sub-state name table (`0x012e1168`, §2.5 had only ever quoted two of these seven as bare examples):

| Index | Name | Index | Name |
|---|---|---|---|
| 13 | `sidewalk start` | 32 | `wander pause` |
| 14 | `sidewalk` | 33 | `wander choose dest` |
| 15 | `sidewalk formation` | 34 | `wander move` |
| | | 35 | `wander to sidewalk` |

**The Sidewalks singleton `0x02924d40` (`spec-zone-data-format.md` §13.2) has 121 real uses across 50
functions** — a whole-binary reference census found this directly; the existing spec had only ever read
its loader. Layout, now read at the instruction level: `+0x00`/`+0x04` is a **24-byte-stride segment
array** (two `u16` node-index fields per record, with dedicated pointer↔index conversion helpers,
`0x00bbdda0`/`0x00bbddf0`) — this is a DIFFERENT sub-array from the already-documented 36-byte-stride
one (`+0x18`/`+0x1c`, cross-linked to the Traffic singleton); `+0x10`/`+0x14` is the already-documented
node-count array §17.3 already cites; `+0x08`/`+0x0c` remains uncharacterized (OPEN); `+0x20`/`+0x24` is
a spatial-index handle built via the generic "register a named spatial collection" primitive
`0x00dc6190`. **`human+0xb94` is a per-human pointer directly into the pair-0 segment array** — the
human's own "current segment," confirmed network-replicated exactly like other per-character state.

A dense, previously entirely unread ~28-function Sidewalks query/steering-support cluster
(`0x00bbdc30`-`0x00bc0970`) was opened in full — segment acceptance tests, bounding-box computation,
endpoint-position/direction-vector resolution (sharing the Traffic graph's own position-quantization
codec), and the real multi-destination path query (`0x00a12a80` — the SAME primitive the existing §14.7
navmesh material already confirms works in retail via a live Frida capture for combat cover-selection;
Sidewalks destination-resolution sharing it is HIGH CONFIDENCE to work too, by shared-code-path
inference, not independently re-tested for pedestrian wandering specifically — this does not reopen or
contradict the explicitly-parked navmesh saga, §14.6/§14.7a/§14.7b, still no writer found for
`human+0xb40`, now with two further readers confirmed, not a contradiction).

**The actual per-frame movement tick, `Idle`'s own `0x00513270`, confirmed in full:** sub-states 13/14/15
all route to `0x0051bc60` ("sidewalk" tick); sub-states 32/33/34 all route to `0x0051c860` ("wander"
tick); sub-state 35 is a one-frame transitional no-op. `0x0051bc60` advances along the segment via
`0x0051b960` (HIGH CONFIDENCE, not independently disassembled to its own last instruction) or, on
sub-state 13/gate failure, calls the "snap onto the graph" init `0x0051bb70`, which reads the human's own
cached cell-key (`+0xb40`) and calls `0x00bbfbe0` — one of the Sidewalks-cluster functions, shared with
the already-named-but-untraced "human navigator chain" (`0x00a1b000`-`0x00a1d000`, **closing that
cross-reference from §14.2**). `0x0051c860`'s wander tick resolves a target handle, computes a steering
direction, and (for "wander choose dest," sub-state 33) generates a validated random candidate point,
transitioning to sub-state 34 and calling `0x00993630` — **CONFIRMED to be the exact "human navigator
chain" function §14.2 already named and left untraced; this closes that cross-reference too.** A small
helper (`0x0051bd20`) force-transitions "sidewalk"→"wander choose dest," the real abandon/retarget path.
**Event kind 81 (previously OPEN, §8 item 2) is now CONFIRMED** produced by the wander-destination-commit
step (posted via `0x004f5dd0`, sub-state filter 34, delay 15-35s).

**Honest OPEN items:** `0x0051b960` (the literal per-frame "advance along segment" math) and several
sibling helpers were identified by call shape, not independently disassembled to their own last
instruction; singleton pair 1 (`+0x08`/`+0x0c`) uncharacterized; the near-field candidate system found
alongside this investigation (a block inside `0x0090faf0`'s own neighbourhood checking "os_ped" and the
Sidewalks gate, calling `0x0090c6d0`→`0x0090c520`/`0x0090c3f0`→`0x00907f50`) could not be cleanly
separated from §20.1's own spawn-side scope by static code alone — flagged as an honest scoping overlap,
not resolved either way; it is most plausibly the SAME chain §20.1 traces (both check "os_ped" + the
Sidewalks gate and call `0x0090c6d0`), not a second, independent system.

### 20.3 Idle behaviour selection for a background pedestrian

`Idle`'s own `enter` (`0x00512300`) picks exactly one of four sub-states from PRE-EXISTING flag bits, not
a live random roll each time: `human+0xe8` bit `0x1000` → sub-state 36 "sleeping"; bit `0x200` → 37
"sitting" (both matching §2.5's already-documented exception); an inventory-held-item check → 11 "wait to
holster weapon"; otherwise the default, sub-state 9 "idle," with a genuine cooldown roll via `0x00dab660`
(250-1000ms or 3000-5000ms) — the same per-action cooldown idiom §4.1's decision-layer finish routine
already documents, now confirmed used directly inside `Idle`'s own enter too, independently. "Sleeping"/
"sitting" are gated on pre-set flag bits whose own writer was not traced — HIGH CONFIDENCE reachable only
via scripted/action-node placement, flagged OPEN not invented. `Idle`'s tick jump table is confirmed the
single dispatcher for sidewalk/wander (§20.2) and structurally separate from everything else (ordinary
per-frame bookkeeping for every other sub-state).

**Action-node idle/loitering behaviour is DATA-DRIVEN** — `action_node_npcs.xtbl` (`spec-tables-traffic-
ai.md` §13) already carries a complete per-role idle-animation state machine (`Enter_anim`/`Exit_anim`/
`Startle_anim`, up to 4 `States`×4 `Animation_action`s, `CowerAnims`, `Min_anim_delay`/`Max_anim_delay`,
and the `Spawn_Priority` flag distinguishing `Sidewalk (normal)` from `Action Node (high)`). The 177-entry
sub-state table confirms this is wired through the SAME mechanism as sidewalk/wander (indices 43/27/139/
101/102/26, all in the identical table). **The specific runtime function that reads a record's
`State`/`Action` array and rolls the anim-delay timers was not found this pass** — out of budget, not
absent; the data-driven reading stands confirmed, the runtime consumer remains the one genuinely open
item here, most plausibly near the action-node loaders (`0x008c9760`/`0x008c87d0`/`0x008c9fd0`).

### 20.4 The real flee-or-react-to-gunfire chain

**Confirms the task's own hypothesis, with the mechanism pinned down precisely.** Flee/Cower are
genuinely what an ordinary ambient (non-combat) pedestrian does in response to nearby gunfire — but the
branch between "become combat-alert" (`Gunfire wary`, mode 17) and "panic/hide" (`Flee` mode 13/`Cower`
mode 9) is gated almost entirely by **the human's own team byte, `human+0x1ca8`, being exactly 7** — not
by any abstract "is this a civilian" flag, and not through the mode machinery's own already-documented
Flee/Cower begin functions (§13.1.1/§13.1.2) directly — those are the downstream machinery this
investigation did not need to re-read. **`Panic` (mode 24) is never requested anywhere in this entire
chain** — it is explicitly guarded against being overridden by a pending request, but nothing in the
gunfire/attack reaction path ever requests it.

**`Idle`'s own event handler does not special-case gunfire — a shared, Idle-private sink does.**
`0x00512680` (mode-table row 0) dispatches event kind 76 ("gunfire heard"/"shot at," already-documented
perception producer `0x005046e0`, §10.3) into a generic default case, tail-calling **`0x005113e0`** (824
instructions, confirmed by whole-binary caller census to have NO other caller — `Idle`'s own private
reactive-event dispatcher, not the generic cross-mode default handler `0x004ebc70`, which is very likely
never reached for an ordinary `Idle` human at all). Gated on non-player and class-descriptor checks, and
a secondary "has an ordered/scripted action pending" gate (`0x004dd460`, testing `human+0x44c` bits 2/3)
that narrows the handled kind range only when an order IS pending — no restriction for an ordinary
free-roaming pedestrian.

**Full confirmed chain for kind 76:**
```
kind 76 -> 0x00512680 -> 0x005113e0 -> case 0x00511b1f
  -> resolve shooter (0x004e4ce0) -> 0x00950550(human, shooter): same-entity guard
     FALSE -> 0x004ebb00(human, event)
        -> no-ops on down/dead/ignore_attacks/combat_disabled/dont_react_to_player
        -> 0x00943ea0(human): NOT-follower AND team==7 ?
             FALSE -> 0x004e8070 pre-hook + REQUEST MODE 17 "Gunfire wary" (0x004e4130(human,0x11))
             TRUE  -> crime/witness notify attempt; resolve weapon lethality (0x00b818b0);
                      -> 0x004eaf80(human, shooter, is_lethal, 0, 1)
```
`0x004eaf80` is ALSO the direct entry for event kind 12 ("attacked"/distress, via `0x004eb920`, always
lethal-flagged). It re-tests `0x00943ea0`: if team==7 and NOT lethal/attacked (heard distant non-lethal
gunfire), it only speaks a line and posts a 250-750ms delayed re-check (kind 67) — no mode change.
Otherwise it gates on flee/cower eligibility and calls **`0x004eaae0`, the real Flee/Cower chooser**:
guards against clobbering a pending `Panic` (the only `Panic` reference in the whole chain, never a
request), computes a "prefer Flee" boolean, and either **requests mode 9 "Cower" (`0x004e4130(human,9)`)**
gated by `0x004eaa40`'s full eligibility check, or **requests mode 13 "Flee" (`0x004e4130(human,0xd)`)**
gated by the same helper (`0x004d8ff0`) `Flee`'s/`Cower`'s own begin functions already use — a failed
Flee falls through to attempt Cower, not mutually exclusive.

**A second, parallel reactive-trigger family, found in the same shared sink, for event kinds not
currently in the project's catalogued perception-posted-kind list** ({0,2,11,12,18,71,76,78,86,87,100},
§8 item 2): kind 14 (`0x00511612`) gates on the same team-7 test but makes NO Cower call (a mis-
attribution from an earlier draft, corrected on cross-check and byte-exact re-disassembly); kind 38
(`0x005119a7`) calls `Cower`'s own begin function directly, bypassing the request layer; kind 48
(`0x005118d8`) calls `Flee`'s own begin function directly, same bypass. **The producers of kinds 14, 38,
and 48 were not found** — none is in the catalogued list, so whether they're genuinely gunfire-adjacent
or a different combat/threat stimulus is a real, honestly-flagged OPEN item, not assumed.

**Team byte value 7 — HIGH CONFIDENCE "ordinary civilian"/non-combatant, confirmed behaviourally at six
independent code sites** (the dismiss pass, the gunfire-perception producer, and four sites inside this
chain itself), but **not confirmed by name** — no string/line table was read to find an English label,
and a whole-`.text` scan for a literal `MOV byte [reg+0x1ca8], 7` found zero hits, meaning team-id
assignment is dynamic (through the already-documented team-registry write side, §12.7's `0x009b7130`),
not a compile-time literal in any spawn/placement path — not independently traced this pass.

### 20.5 Cross-references and summary of what remains OPEN

- **§9.9** (force-flag table): add real consumer citations for `sidewalking`/`wandering`/
  `sidewalk_leashed`/`dont_use_sidewalk` per §20.1 above (previously listed with no known reader).
- **§2.5** (sub-state table): the seven sidewalk/wander entries (§20.2) and their real owning tick
  functions now join the already-documented sleeping/sitting exceptions.
- **§14.2/§14.6**: the "human navigator chain" cross-reference is closed (§20.2); the parked navmesh
  saga's own `human+0xb40` open item is extended (two further readers, still no writer) not reopened.
- **§8 item 2**: event kind 81's producer is confirmed (§20.2); kinds 14/38/48/67's producers remain open
  (§20.4).
- **`spec-zone-data-format.md` §13.2**: the Sidewalks content kind's runtime consumer is now
  substantially detailed (§20.2) — a 24-byte-stride segment sub-array distinct from the already-
  documented 36-byte-stride one, plus the full query/steering-support cluster.
- **`spec-tables-traffic-ai.md` §13**: the data-driven idle/loitering reading (§20.3) stands confirmed;
  the runtime consumer of `action_node_npcs.xtbl`'s own anim-delay fields remains the one open item.
- Genuinely unresolved across this whole unit: why newly-spawned near-field pedestrians walk sidewalks
  without an explicit `sidewalking` force-flag call (§20.1); the caller of `0x006af664` (zero references
  anywhere in the PE image); the `action_node_groups.xtbl` `Convert_to_ped_*_delay` consumer
  (inconclusive); `0x00917d10`'s own distinguishing role; producers of event kinds 14/38/48/67; the
  English name for team byte value 7.

Full detail, every address, and the complete raw evidence for all four sub-parts:
`scratchpad/ambient_pedestrian_onfoot_behavior_2026-10-09.md` (parts b/c/d) — the spawn/placement
sub-investigation's own full report (part a) was delivered directly as this unit's own hand-back and is
not separately filed under `scratchpad/`, per the task's own file-count instruction; its content is
fully absorbed into §20.1 above.

### 20.6 Free-roam population constants (2026-10-14)

**Scope.** The numbers a port needs to populate streets the way the game does: the near-field
(fully simulated) ambient vehicle and pedestrian spawners and despawners, and the traffic lane
follower's speed and stop logic. Every constant below was read from the executable with `capstone`
(no Ghidra), or from the shipped tables in `misc_tables.vpp_pc`. The road and sidewalk graph is the
one in `spec-zone-data-format.md` §13.2a. Status tags: **FINAL** = read end to end; **PARTIAL** = the
named part is OPEN.

**Corrections to earlier text in this file:**
- §20.1 says peds are rejected "within ~90 units of the camera". **Wrong direction.** 90 m is the
  *far* limit. The near limit is 10 m, and a visible point closer than 70 m is also rejected (§20.6.2).
- §17.4c's "3-candidate-slot buffer" is really **3 pick attempts** over a list of up to **32** road
  candidates.
- §17.4c's "spawn rate formula" at `0x00910940` does not compute a rate. It computes the
  **target live count** (§20.6.1(a)). The rate is fixed: at most one vehicle per maintenance pass.
- The `Spline_Type` index → segment-flag table (`0x01308ad4`) settles the meaning of several §13.2a
  flag bits: bit 4 = `Highway Only`, bit 16 = `Offroad`, bit 17 = `Baggage`, bit 18 = `Boat`,
  bit 21 = `Taxi`, bit 22 = `Indoor`. Bit 10 = the ambient class (`All Roads`/`Surface Roads`, which
  store −1).

#### 20.6.0 Shared inputs

**(a) Time-of-day density index — FINAL.**
- `0x005a18d0` returns the active TOD segment index (`0x014032e0`).
- The segment start times come from `default_global.xtbl` `tod_segments`: 500, 930, 1430, 2100
  (HHMM). The loader is `0x00bb65cb`, the array is at `0x029152fc` and the count is at `0x02915304`.
- `0x005a1d10` finds the previous and next segment and the blend t between their starts (the day
  wraps). `0x005a2440` then selects **prev when t < 0.5, otherwise next**.
- So the index switches at the midpoints between starts:

  | Clock time | Index |
  |---|---|
  | 01:00–07:15 | 0 |
  | 07:15–12:00 | 1 |
  | 12:00–17:45 | 2 |
  | 17:45–01:00 | 3 |

- The density functions map the index to the category's day and night values as
  {0: Night, 1: (Day + Night)/2, 2: Day, 3: Day}. The pair is CarDay/CarNight `+0x20/+0x24` or
  PedDay/PedNight `+0x28/+0x2c`. The element→offset order was checked in the loader at
  `0x00be56e0`–`0x00be5712`.
- The byte `0x026099c6` (setter `0x009108a0`) forces index 2.
- Note: the evening segment uses the Day value. That is what the code does.

**(b) Day/night index for chances and caps — HIGH.**
- `0x00ba2d20` returns 0 for day and 1 for night (it is the negation of `0x00ba2840`).
- `0x00ba2840` compares the clock with `default_global` `day_begin` 630 / `day_end` 2200. The
  comparison body was not read to its last instruction.
- This index selects DayChance/NightChance, DayCap/NightCap and VehicleDayCap/VehicleNightCap.

**(c) Notoriety scale — FINAL.**
- `0x00605540` returns the highest current level over the 4 notoriety tracks (§19.1).
- Vehicle table `0x01308b10` and ped table `0x01308af8` are identical: levels 0..5 →
  **1.0, 1.0, 0.75, 0.5, 0.25, 0.1**.

**(d) Weather — FINAL (peds only).**
- The ped density is multiplied by the current weather stage's `PedDensity`. That is `weather.xtbl`
  `+0x14`, reached through `0x005a6920` (`spec-tables-environment.md` §2).
- Shipped values: Clear Skies 1.0, Overcast 1.0, Post-Storm 0.75, Light Rain 0.5, Heavy Rain 0.1.

**(e) Category = district — PARTIAL.**
- `0x00911770` (player) returns the category used everywhere below (current category `0x026099cc`).
- Cheat modes 1/2/3 select the Ho/Mascot/Zombie categories.
- Otherwise it takes the hood under the player (`0x0084bb20`), then that hood's spawn category
  (`0x0084aed0`). It falls back to `Default Spawn` (`0x00be47f0`).
- **OPEN:** the hood → `sp_cat_*` mapping data (the hood definition table, §18.4). The test vectors
  below therefore name categories directly.

**(f) Spawn-distance scale — FINAL.**
- For the local player: `0x0130af7c` = tan(default FOV/2) ÷ tan(current FOV/2), clamped to [1, 11]
  (`0x009d9fe0`). It is 1.0 at normal FOV and grows when zoomed.
- For other players: player `+0x1fac`.
- Every radius marked "×scale" below uses this value.

**(g) Viewer distance — FINAL.** `0x009dd9b0` measures from the *camera* position
(`0x013c8740`; a remote player's `+0x1f74`). With its last flag set it measures horizontal (x-z)
distance.

#### 20.6.1 Ambient vehicles (near-field)

**(a) Target live count `0x00910940` — FINAL.**
```
base   = 0x00907530():
           activity mode (0x00614d00) == 5         -> 0
           else trunc(Nbase * S)                    Nbase = 0x013089a8, S = 0x013089ac (default 1.0)
           player in a vehicle (0x009e82f0)         -> 18 if 0x026093b8 else 12
           co-op                                    -> 0x00906290(base, 130 m) split (below)
d      = category CarNight/avg/CarDay by the TOD index (§20.6.0(a))
s      = min(d * notorietyScale[maxLevel], capV)     capV = 0x01308ab0, default 1.0
s      = capV if activity mode is 2 or 5
s      = 0x01308af4 if that value > 0                (default −1, no writer: dead debug override)
target = max(1, trunc(base * s))                     (x87 truncate)
```
- Nbase is 12. It becomes 18 while the local-player hook 0x13 state is set; the enter/leave callbacks
  are `0x009073e0`/`0x00907440`, registered at `0x00908f8b`. HIGH: that state is "player driving".
- S is set in [0, 1] by `0x00905d30`. Callers are mission/reset code `0x0063f67f` and a script
  native at `0x00a5ffd3`.
- `0x00910a80` sets capV.
- **The hard ceiling stays 48** (`0x0090ee10`, §17.4c). It is never reached by the formula's maximum
  of 18.
- Co-op split (`0x00906290`, R = 130 m for vehicles, 70 m for peds): if the two players are closer
  than R, each gets base/2; if farther than 2R, the full base; in between, interpolated. **PARTIAL:**
  the interpolation tail was not read.

**(b) Maintenance pass `0x0090f2c0` — FINAL.**

Gates:
- the `os_vehicle` array is on;
- the 200–300 ms random timer `0x013089ec` has expired;
- the road graph is loaded (`0x00a69720`);
- `0x0092b2c0`/`0x0092b2d0` are false.

It sets the heavy-pass latch `0x026093b0` and runs:
1. `0x00905b40`, re-arming the timer.
2. `0x0090eaf0`, the `vehicle_despawn.xtbl` schedule (§17.4c).
3. `0x0090f060`, the distance despawn, (d) below.
4. `0x0090ef90`, the per-vehicle visibility bits.

Then it spawns, **only when** target − live ≥ 1 (live = `0x02609120`) and the human headroom
`0x009086d0` (150 − humans) is > 0:
- The camera speed is |camera displacement this frame| ÷ (1/30 s) (`0x0132a0ac` = 0.0333).
  "Moving" means the speed is above **2.2352 m/s** (5 mph, `0x0111f730`).
- A candidate list of up to 32 entries is built by `0x0090e1e0` (c).
- Up to **3 attempts**: each picks a uniform index (`0x00dab660`) and tries `0x0090dd40` →
  `0x0090d4c0`. The first success ends the pass. A failed candidate is removed (swapped with the last).
- Net: **at most one vehicle per pass, one pass every 200–300 ms**, i.e. at most 3.3–5 spawns/s.
- While a player chase is active (`0x00b2db20`, kind 4), the pass also needs the 4000–6000 ms timer
  `0x013089f4`.

**(c) Candidate generation `0x0090e1e0` — FINAL except where marked.**
- **Centre** = camera position (`0x013c8740`, filled by `0x00908df0`).
- **Ring radius R = 130 m × scale** (`0x0116ef70`; scale from §20.6.0(f), normally 1.0).
- Road segments are gathered with the spatial query `0x00a69cd0` (max 128) and filtered with
  `0x00a6d550`/`0x00a6d050`:
  - include mask = `0x400` | the flag bits of every streamed vehicle slot's `Spline_Type` (built by
    `0x00917190` from table `0x01308ad4` = {0x10, 0x40000, 0x10000, 0x400000, 0x20000, 0x200000}) |
    `0x10000`;
  - exclude mask = `0x0080010c` (bits 2, 3, 8, 23).
- **Highway rule.** If the player's current segment (player `+0x20dc`) has flag bit 4, and fewer than
  trunc(0.75 × target) near-field vehicles are on bit-4 segments (count `0x0090e120`, 0.75 at `0x01308998`),
  then only bit-4 segments are accepted.
- **Segment full.** A segment is skipped when its occupancy list (§13.2a(j)) already holds **3** live
  vehicles, or **5** on a bit-4 segment (`0x0090e050`). This is the only car-to-car spacing rule at
  spawn time.
- **Spawn points** are where the segment's chord (node A → node B) crosses the circle of radius R
  around the centre (`0x00dbd630`).
- **Inner ring.** If no crossing exists at R, the code retries at **R/2 = 65 m**. Points found at
  65 m are kept only if a ±1.5 m box (`0x012a2d30`) around them is **not visible** to the local camera
  (`0x005dcb20` → frustum `0x00a0e720` + occlusion `0x005dcad0`).
- **Zone loaded.** The zone at the point must be streamed in (`0x00861aa0`/`0x00861ec0`).
- **Moving cone.** While moving, a point is kept only if
  dot(direction from camera, camera velocity direction) ≥ minDot.
  - minDot = 0.5 × min(s, 13.4112)/13.4112. Here 13.4112 m/s = 30 mph (`0x0116ef68`), and the second
    term uses 0.0 (`0x012a2d28`).
  - Stationary: minDot = −1 (all directions).
- **Closing test.** Reject if dot(segment tangent × lane speed − camera velocity, direction from camera)
  > 0. A car is only spawned if it moves toward, or not away from, the camera.
  - The lane speed is `0x00a6baa0`, (§20.6.3(a)).
- **Obstruction points.** Reject if a registered obstruction point (list `0x02898240`, count
  `0x0289821c`, 32-byte records) lies on the same segment or its single successor/predecessor, within
  **40 m** (`0x0130899c`/`0x013089a0`, test `0x00b2d760`). **PARTIAL:** the identity of these points
  is OPEN (HIGH: roadblocks/blockages).
- **Co-op partner.** In co-op, reject if the partner's camera sees the box and is within 130 m, or
  does not see it and is within 80 m (`0x00907a90` vehicle box (−4, −1, −4)…(4, 2, 4)).

**(d) Distance despawn `0x00907160` (applied by `0x0090f060`) — FINAL.**

A vehicle passes the eligibility predicate `0x00906b00` (§17.4c) and is then removed when it is
farther than D from every player.

| Case | D |
|---|---|
| Default | **140 m** (`0x01117b6c`) |
| Not visible, camera moving, vehicle behind the camera (dot < 0) | **65 m** (`0x012a2f78`) |
| Activity mode 2, camera still | **90 m** |
| Currently visible | at least 140 m |
| Special classes (`0x00ad3160`/`0x00ad31a0` + `0x00ad4040`), or mode 6 when visible | **250 m** (`0x011175d0`) |

- **[2026-10-15 clarification: the "behind the camera" test of the 65 m row is not traced to its reference vector. Team B (e11f57f) uses dot(vehicle offset, camera velocity) < 0. Treat that as CHOSEN until the vector is read; for a moving camera it equals the cone axis of (c).]**
  **[2026-10-15 closed: FINAL. In `0x0090f060`, delta = camera position `0x013c8740` minus previous-frame camera position `0x013c8800`; fwdSpeed = dot(camera forward `0x013c8780`, delta) x 30 (divide by 0.03333, `0x0132a0ac`; fixed 30 Hz, not the real frame time). "Moving" means fwdSpeed > 2.2352 m/s (5 mph, `0x0111f730`); only then is the delta vector passed to `0x00907160` (else null, which gives the 90 m / 140 m still-camera cases). In `0x00907160` (test at `0x009072a4`): off = vehicle position (+0x40) minus camera position `0x013c8740`; if not visible and dot(off, delta) < 0.0 (`0x0125e270`) then D = 65 m, otherwise 140 m. The reference vector is the camera displacement over the last frame (unnormalised, so only the sign matters), i.e. camera velocity direction, but the moving gate uses its component along camera forward. Port rule: moving = dot(camFwd, camPos - prevCamPos)*30 > 2.2352; behind = dot(vehPos - camPos, camPos - prevCamPos) < 0. Team B's test matches except it lacks the 2.2352 m/s forward-speed gate (and a sideways or backward-moving camera counts as still when the forward component is small). Test vector: camFwd (0,1,0), camPos (0,0,0), prev (0,-0.1,0) gives fwdSpeed 3.0 (moving); vehicle at (0,-50,0) gives dot -5 (D = 65 m, removed beyond 65 m); at (0,50,0) gives +5 (D = 140 m); with prev (0,-0.05,0) fwdSpeed 1.5 is below the gate (still camera, D = 140 m).]**
- Check `0x009ea700`: D × scale for a player on foot (3-D distance). For a player in a vehicle it is
  **(D + 60 m)** × scale, horizontal distance (60.0 at `0x0111e478`).
- A visible vehicle fades over 500 ms (`0x00a846f0`); an unseen one is deleted at once (`0x0090ccd0`).
  A towed trailer goes with it.

**(e) Model choice — PARTIAL.**
1. *Streamed set.* For each streamed character slot (lists `0x01308b8c`/`0x01308b3c`), for each of its
   groups that has vehicles and whose `Spline_Type` road class exists, one vehicle is drawn uniformly
   from the group's `Vehicles` list (`0x00911a60`). Duplicate rows therefore weigh double.
2. *Per spawn* (`0x009177c0`):
   - Candidates are the streamed vehicle slots (`0x01308bdc`) that pass `0x00916af0`. The model must be
     streamed, its live count must be below the group's **VehicleDayCap/VehicleNightCap** (slot
     `+0x10 + 4·dayNight`), and a driver character must be available (`0x009160a0`).
   - `0x00917470` picks among them. A candidate whose group record has flag bit 0 set is taken at once.
     Otherwise it keeps the models with the fewest duplicates among nearby live vehicles (`0x00913f20`;
     a model with ≥ 4 nearby, or ≥ 7 when the caller flag is set, is excluded). It breaks ties by the
     fewest live near-field copies, then picks uniformly.
   - There is **no probability weight** at spawn time.
3. Occupants: 1..4, clamped. Vehicle-info `+0x4c4` == 10 forces 1.

**OPEN:** the re-roll cadence of the streamed set (the 20 s timer `0x01308c1c` is the HIGH
candidate) and the slot-count budget.

#### 20.6.2 Ambient pedestrians (near-field)

**(a) Target live count `0x009106c0` — FINAL.**
```
base   = 0x009074b0(): 40 (0x28); +21 (0x008f12d0) when player +0x1d9a bit 1 is set;
         co-op -> 0x00906290(base, 70 m)
d      = category PedNight/avg/PedDay by the TOD index (§20.6.0(a))
d      = d * weather PedDensity (§20.6.0(d))
s      = min(notorietyScale[maxLevel] * d, capP)      capP = 0x01308ab4, default 1.0
s      = 0x01308af0 if that value >= 0                (setter 0x009108b0, reset 0x009108d0 to −1)
target = min(base, max(1, trunc(base * s)))
```
**PARTIAL:** the meaning of player `+0x1d9a` bit 1 is OPEN.

**(b) Two spawn paths — FINAL for the dispatch, PARTIAL for the placement strategies.**

*Bulk fill* `0x0090c6d0`:
- It runs only on a pending populate request. `0x0090fa60` with flag 0 stores the request
  (`0x026093b1`); the next frame runs it once streaming is ready (`0x009149a0`).
- It spawns n = trunc(**0.33** × min(target − live, headroom)) peds (0.33 at `0x012a2e00`).
- Each ped gets up to **3** attempts: a **length-weighted** pick of a candidate sidewalk edge
  (`0x00dab5e0` over the cumulative lengths), then `0x0090b840`.
- It is skipped while mission `m03` is active (`0x006d7240("m03")`).

*Regular maintenance* `0x0090c910`, every frame when no populate request is pending:
1. It culls first (d).
2. It counts the visible ambient peds.
3. It spawns at most one ped through a strategy picked from the camera-motion mode `0x01308994`
   (`0x00908a70`). The mode is a turning sign over ±0.02 rad/frame, moving forward/back (cos > 0.707),
   or **4 = default**; mode 3 or 4 randomises among the strategies.
   - Strategies: `0x0090bae0(1, 0/1)` and `0x0090c3f0(1, …)`. They search a 90 m × scale ring
     (`0x0116a3e8`) and end in the same creation routine `0x0090b0a0`.
   - The strategy itself checks live < target (`0x0090c349`/`0x0090c354`).
   - In modes other than 4, it also needs visible-count ≤ (target + 1)/2.
   - It also needs the resource gate `0x00916680` (`0x008f1220`) and headroom > 0.
4. **PARTIAL:** the placement geometry inside the strategy functions is OPEN.

**(c) Candidates, placement and rejects — FINAL.**
- **Candidate edges** (`0x009085f0` → `0x00bc0520`): up to **100** sidewalk edges within
  **70 m × scale** of the camera (`0x0116ef78`). The list is rebuilt when the 500 ms timer
  `0x013089b8` expires or the camera has moved more than **30 m** (900 = 30², `0x01117780`) since the
  last build.
- **Point** (`0x0090b840`):
  - uniform along the edge;
  - a lateral offset uniform in **±width/2** perpendicular to the edge (width = edge `+0x04` ÷ 256 ×
    rand[−0.5, 0.5], rotation π/2 at `0x01115234`), i.e. ±1.5 / 2 / 3 m on the shipped 3/4/6 m
    walkways;
  - a 50 % choice of which end node the ped heads for.
- **Rejects in the creation routine `0x0090b0a0`**, in order:
  1. Farther than **90 m** from every viewer (8100 = 90², `0x0116a368`).
  2. Closer than **10 m** (100, `0x0111645c`). The bulk path's flag skips this and step 3.
  3. Visibility (`0x00907a90`, ped box ±0.425 m): rejected if the local camera, or a co-op partner's,
     sees the point and it is within **70 m** (4900, `0x01117788`), or does not see it and it is
     within **10 m**.
  4. Resource gate `0x008f1220`. Its third argument is 10 in activity mode 5. This is the "difficulty
     gate" of §20.1, but it is a creation-budget check.
  5. Local crowd: more than **0.005 peds/m²**, i.e. at least 7 near-field ambient peds within
     **20 m** (`0x009079f0`: count within 400 = 20² ÷ 1256.64 = π·20²; threshold `0x01153b04`).
  6. The point's zone is not loaded.
  7. Ground: a ray from +0.5 m to **−20 m** must hit (`0x0075df80`). The hit becomes the spawn point.
  8. Clearance: a box ±**0.425 m** in x-z and **1.85 m** high (`0x013089b4`) must be empty
     (`0x0075d4b0`).
  9. The navigation query `0x00a12180` (radii 0.5 / 2.0) must succeed.

  The character is then created through `0x00bb9070`.

**(d) Despawn `0x00908930` + `0x00908740` — FINAL.**

A ped is eligible only if all of these hold:
- the generic gate `0x008addb0` passes;
- it is not important (`0x0097e280`), not `0x009b9160`;
- `+0x1da8` == 0, and `+0x1da5` bit 3, `+0x1da4` bits 5 and 7 are clear;
- it is team 7, or its team's notoriety level is ≤ 0.

It is removed when, for every player, its horizontal distance from the camera is more than **90 m**.
- In spawn mode 2, a ped behind the local player uses **50 m** (2500, `0x011173a4`).
- When player `+0x1d9a` bit 1 is set and the height difference is more than **8 m** (`0x01117a54`),
  the ped is removed as long as it is not visible.

A visible ped fades over 1000 ms (`0x0094fb60`); an unseen one is deleted (`0x00853ea0`).

**(e) Character choice — FINAL for the rolls, PARTIAL for the streaming budget.**
- *Slot kind* (`0x009158f0`): for each kind (General ped / Special ped / Special vehicle) the
  category's normalised slot weight (`+0x30/+0x38/+0x40` + 4·dayNight, §10.6 normalisation) is
  compared with that kind's current share of streamed slots. Kinds below their share are eligible,
  and one is drawn in proportion to its weight (`0x00dab6a0`).
- *Group* (`0x00913640`): among the category's groups with that `Group_Category` and at least one
  character still available (`0x009134c0`), a roulette pick weighted by **DayChance/NightChance**
  (entry `+0x04` + 4·dayNight; draw r = rand × Σw). Then a uniform pick of one available character in
  the group (`0x00dab660`).
- Activity mode 0x11 bans `spawn_Civ_Genki`.
- *Per spawn*, `0x009160a0` filters the streamed characters (team, sex and the request flags).
  **PARTIAL:** its final tie-break was not read.
- **OPEN:** how many slots of each kind are streamed (`0x00916ef0`/`0x00911e40`), and walk/flee
  speeds. The speeds are not set by the spawner; they are animation-driven and untraced.

#### 20.6.3 Lane follower (ambient traffic)

**(a) Cruise speed source — FINAL.**
`0x00a6baa0(seg)` returns the segment's speed limit:
- flag bit 19 set → **1000**;
- else class = segment byte `+0x30` **>> 4**: class ≥ 3 → **10.0**; else `traffic_lanes.xtbl`
  `Speed[class]` (`0x02705420`).
- Shipped rows: 0 "Default" 10, 1 "Highway" 20, 2 "Regular Traffic" 10 (m/s per the editor schema).
- Every shipped segment has `+0x30` ∈ {1, 3}, so class 0. All signal writers (`0x00a69ed3`,
  `0x00a69ffd`, `0x00a6aa5f`) keep the high nibble. **Every road's cruise limit is therefore 10 m/s**
  (bit 19 is never set on disk).
- The route builder stores this per route entry (`0x00a1f10f`/`0x00a1f127`).

Mode-specific desired speed (`0x00b1e840`, by directive mode byte `+0xc68`):
- mode 6 (rejoin ambient): the route entry's speed, else 10, **+10** when directive `+9` bit 0x40;
- mode 15: lane speed **+5**, or 20, or the directive cap `+0x9c`, or 1000;
- mode 5: 15;
- mode 13: 40;
- mode 3: 0 (planning stop);
- modes 8–12 and 14: the route value computed by `0x00b2aa70`/`0x00b28150` below.

**(b) Speed planner `0x00b28150` and controller — FINAL except where marked.**
- Braking capability a_b = 0.8 × directive `+0x530` × vehicle `+0xc58`.
- Lateral capability a_lat = 0.8 × directive `+0x52c` × vehicle `+0xc5c` (0.8 at `0x01116448`).
- Vehicle `+0xc58`/`+0xc5c` = `AI_Max_Braking_Decel`/`AI_Max_Radial_Accel` (vehicle info
  `+0x770/+0x774`), set at `0x00b21533`. They are scaled by {1.0, 0.8, 0.7, 0.6, 0.5}
  (`0x0130ebe0`/`0x0130ec08`) by the vehicle condition level `0x00ad3dc0` (0..4).
- **PARTIAL:** the directive scale fields `+0x52c`/`+0x530` are OPEN.
- Above 5 m/s (`0x01187ecc`), the planner looks ahead over the stopping distance v²/(2·a_b) at the
  route pieces (`0x00b1e220`, lengths from segment `+0x20`/`+0x22`, §13.2a(c)). Its output is the
  largest deceleration required to reach any slower piece. **PARTIAL:** the per-piece formula's exact
  terms are OPEN.
- Curve limit: v_curve = sqrt(a_lat / κ) when the curvature κ at the route position (`0x00a1e880`) is
  > 0.001. Otherwise 1000. It is clamped to [**7**, 1000] m/s (`0x0113e6d0`, `0x012a2d90`).
- If the current speed exceeds 1.2 × target, brake demand = (v/target − 1.2) × vehicle `+0xc58`
  (`0x00b2ab96`, factor 1.2 at `0x01117ae0`).
- Throttle law `0x00b207d0(directive, vt, v, flag)`:
  - vt < 1 → throttle 0.001;
  - otherwise k = max(2, (vt/5)²), vt clamped to [1, 100], and throttle = k·(1 − v²/vt²), clamped to
    [0.001, cap];
  - cap = 1.0 when the flag is set; otherwise `0x00b1f980`(v) = 0.7 + 0.3·clamp((v − 5)/5, 0, 1)
    (`0x00dadd90`, 0.7 at `0x011175a0`, 0.3 at `0x012a2db8`). The cap is 1.0 in modes 2/4/7 and for
    3 s after a stuck event.
- When deceleration is required: brake = min(1, a_req / vehicle `+0xc58`) and throttle 0.001
  (`0x00b28614`).
- The §18.6 "close/aligned" branch (cos 30°, 5.0 m) uses the same law with **vt = 3.5 m/s** and the
  flag set, so it arrives at a crawl.

**(c) Signals — FINAL.**
- Lights are obeyed only in directive modes 0, 7, 8 and 15 (§18.7.1).
- `0x00b52bc0`: if the next route segment's `+0x30 & 0xf` is **2 (amber) or 3 (red)**:
  - d = distance to the stop (vehicle `+0x11dc`, floored at 0.001);
  - **stop when v²/(2d) ≤ vehicle `+0xc58`**, otherwise drive through;
  - while stopped (speed ≤ 0) or within **3.0 m** (`0x01116444`), set `+0xc70` bit 2 (waiting). That
    bit feeds the honk/wait responses of §18.7.2.
- The stop target comes from `0x00b51950`. **PARTIAL:** its stop-line geometry is OPEN.
- Signal state 1 (green) never stops a car.

**(d) Re-query and re-plan — FINAL.**
- The cached-target re-query threshold `0x01117a60` is a **double 15.0 m**, linear (not squared)
  (`0x00b4cb65`). A re-plan is also forced when the drift exceeds (… + 20 m) (`0x01116998`).
- The ambient route search box is 200 m (`0x012a30d4`) with 1500 (`0x011327e4`), up to 20 candidates
  and flags 0x100. The walk lookahead is **5000 m** (`0x0111d490`, the param block `+0x1c` of §18.1).
- Re-plan timer `+0x450`: **2000–2500 ms** when speed² < 0.25, otherwise **300–450 ms**
  (`0x00b4ce41`–`0x00b4ce8b`).

**(e) Following gap and lane change — PARTIAL.**
- No time-headway or gap constant was found. Car-following comes from two sources: the occupancy and
  conflict lists (§13.2a(e)/(j), consumer `0x00b5e6c0`) and the §18.7.2 lead-handle path (15 m).
- Lane changes exist only as path-search edges to the `+0x3a`/`+0x3c` neighbours. Flag bit 0 forbids
  `+0x3c`, the oncoming lane. The §18.6b overtake gate sits on top.
- **OPEN:** the yield/following distance law inside `0x00b5e6c0`, and when the planner chooses a
  neighbour edge.

#### 20.6.4 Test vectors

All assume: single player, on foot unless stated, notoriety 0, Clear Skies, activity mode none,
scale 1.0. TOD index from §20.6.0(a) (checked: 03:00 → 0, 08:00 → 1, 14:00 → 2, 23:00 → 3). Values are
computed in f32 with truncation.

| Category | Time | Vehicles (on foot / driving) | Peds |
|---|---|---|---|
| `Default Spawn` (Car .8/.5, Ped .8/.4) | 03:00 | 6 / 9 | 16 |
| `Default Spawn` | 08:00 | 7 / 11 | 24 |
| `Default Spawn` | 14:00 | **9 / 14** | **32** |
| `Default Spawn` | 23:00 | 9 / 14 | 32 |
| `sp_cat_Yearwood` (.8/.6, .8/.4) | 03:00 / 08:00 / 14:00 | 7 / 8 / 9 (driving 10 / 12 / 14) | 16 / 24 / 32 |
| `sp_cat_CutterAirport` (1/.5, 1/.3) | 14:00 | 12 / 18 | 40 |
| `sp_cat_RosenOaks` (.5/.3, .5/.3) | 03:00 | 3 / 5 | 12 |

- `Default Spawn` at 14:00 with notoriety level 3: vehicles 4, peds 16. At level 5: 1 and 3.
- `Default Spawn` at 14:00 in Heavy Rain: peds 3.
- Fill rate: a still camera on a road-rich tile reaches the vehicle target at one spawn per 200–300 ms
  pass, i.e. about 9 vehicles in ≈ 2–3 s. The 48 cap is never the binding limit.

**Spawn and despawn radii (scale 1.0):**

| What | Distance |
|---|---|
| Vehicle spawn ring | 130 m (65 m fallback, out of view only) |
| Vehicle despawn | 140 m; 200 m when the player drives; 65 m behind a moving camera; 250 m for special classes |
| Ped candidates | edges within 70 m |
| Ped accept band | 10–90 m; visible points < 70 m rejected |
| Ped despawn | 90 m horizontal |

**Moving cone (minDot / half-angle):**

| Camera speed | minDot | Half-angle |
|---|---|---|
| ≤ 2.2352 m/s | none | — |
| 5 m/s | 0.1864 | 79.3° |
| 10 m/s | 0.3728 | 68.1° |
| ≥ 13.4112 m/s | 0.5 | 60.0° |

**Throttle, vt = 10 m/s (every shipped road):**

| v (m/s) | 0 | 5 | 8 | 9 | 9.5 | 10 |
|---|---|---|---|---|---|---|
| Throttle | 0.70 | 0.70 | 0.88 | 0.76 | 0.39 | 0.001 |

- Arrival law, vt = 3.5, flag set: v = 0 → 1.0; v = 3 → 0.5306.

**Minimum gap:** OPEN (see §20.6.3(e)).

**Model weights, `Default Spawn`, General Ped groups** (DayChance, Σ = 70.0):

| Group | Day weight | p (day) | p (night) |
|---|---|---|---|
| `spawn_Civ_Normal_MW` | 5.25 | 0.075 | 0.175 |
| `_FW` | 12.25 | 0.175 | 0.075 |
| `_MB` | 5.25 | 0.075 | 0.175 |
| `_FB` | 12.25 | 0.175 | 0.075 |
| `_MH` | 2.1 | 0.030 | 0.070 |
| `_FH` | 4.9 | 0.070 | 0.030 |
| `_MA` | 8.4 | 0.120 | 0.280 |
| `_FA` | 19.6 | 0.280 | 0.120 |

The night column is computed from NightChance, which also sums to 70.

- Slot-kind weights for `Default Spawn`: 70/15/15 → **0.70 / 0.15 / 0.15**, day and night.
- Special Vehicle day weights (Σ = 15.1): Luxury 0.2318, Taxi 0.1987, Trucker 0.1325; Bus, ExSport,
  Motorcycles and Moving 0.0662 each; Moped and Offroad 0.0331; Rescue 0.0265; Limo 0.0199; Armored,
  CityMaint, Hearse and News 0.0132; Gatmobile 0.0066.

**Streamed-vehicle draw from `spawn_Civ_Normal_MW`** (31 `Vehicle` rows, uniform per row):
- 2/31 = **0.0645** each: `car_2dr_standard03`, `car_2dr_standard06`, `car_4dr_compact01`,
  `car_4dr_sports03`, `car_4dr_standard03`, `car_4dr_standard07`, `suv_4dr_04`;
- 1/31 = 0.0323 each: the other 17 models.

#### 20.6.5 Status summary

| Item | Status |
|---|---|
| Vehicle target formula, TOD/notoriety inputs, pass cadence, 1 spawn per pass, 3 attempts | FINAL |
| Ring 130/65 m, cone, closing test, segment capacity 3/5, masks | FINAL |
| Obstruction-point identity | PARTIAL |
| Vehicle despawn radii | FINAL |
| Vehicle model rules | PARTIAL (streamed-set cadence and budget OPEN) |
| Ped target formula, weather, bulk 0.33, candidate edges, all `0x0090b0a0` rejects, despawn | FINAL |
| Regular-path placement strategies, `+0x1d9a` bit 1 | PARTIAL |
| Ped slot-kind and group rolls | FINAL; streaming budget OPEN; per-spawn tie-break PARTIAL |
| Hood → category mapping | OPEN |
| Cruise source 10 m/s, throttle law, curve floor 7 m/s, signal stop rule, re-query 15 m, re-plan timers | FINAL |
| Look-ahead decel formula, directive `+0x52c/+0x530`, stop-line geometry | PARTIAL |
| Following gap, lane-change choice, walk/flee speeds | OPEN |

Scratch scripts (read-only, deletable): `tools/scratchpad/spawn_lib.py`, `spawn_dis.py`,
`spawn_scan.py`, `spawn_scan2.py`, `spawn_jt.py`, `spawn_extract.py`, `spawn_cats.py`,
`spawn_vectors.py`. The extracted tables are in `tools/scratchpad/spawn_xtbl/`.

#### 20.6.6 Team B wiring

| Team B CHOSEN constant (file:line) | Replace with |
|---|---|
| `tools/sr3_viewer.cpp:18170-18172` near-field spawn annulus 25–90 m, ±70° heading | Road crossings of the 130 m × scale circle around the camera (65 m fallback, hidden only) and the speed-dependent cone of §20.6.1(c) |
| `tools/sr3_viewer.cpp:18241-18243` ped ring 90.5–160 m, 6 m stand-in segments | Real sidewalk edges within 70 m (§13.2a(l)); accept 10–90 m; reject visible < 70 m |
| `near_field_vehicle_population.h:125` one attempt per tick | Keep one spawn per pass, but add up to 3 picks, the 200–300 ms pass timer, and the target formula §20.6.1(a) |
| `near_field_vehicle_population.h:174` cap 48 | Keep as the ceiling. Fill toward target = max(1, trunc(12 or 18 × min(1, density × notorietyScale))) |
| `near_field_vehicle_population.h:176` 3 candidate slots | 3 attempts over ≤ 32 road candidates |
| `near_field_vehicle_population.h:242` "gate does not apply outside the cell" | Distance despawn of §20.6.1(d): 140 / 65 / 90 / 250 m, +60 m when driving |
| `near_field_ped_population.h:186` near reject 90 | Near 10 m; 90 m is the FAR reject |
| `near_field_ped_population.h:190` difficulty gate passes | Resource gate `0x008f1220` plus the crowd cap of 7 peds within 20 m |
| `near_field_ped_population.h:197` far reject 0 (off) | 90 m, horizontal, from every viewer |
| `near_field_ped_population.h:216` ground always valid | Ray +0.5 → −20 m must hit; clear box ±0.425 m × 1.85 m |
| `near_field_ped_population.h:221` sideways offset 0.0 | Uniform ±width/2 (±1.5 m on 3 m walks) |
| `near_field_ped_population.h:250` half-FOV 50° | Real frustum + occlusion; reject when visible and < 70 m, or hidden and < 10 m |
| `near_field_ped_population.h:787/797/811` flee 5.0, walk 1.4, advance 0.3 | No constant exists to copy: walk and flee requests pass the speed sentinel −1, so the speed is the locomotion clip's root motion × human `+0xa64` (§20.6.9(a)). Keep 1.4 / 5.0 as CHOSEN until a clip root-speed is measured **[2026-10-14: measured, §20.6.9(a). Walk is 1.386 m/s (M) / 1.368 m/s (F); run is 6.373 / 4.433; cower run is 4.554 / 5.745]**; replace the 0.3 "advance" model with the sidewalk path of §20.6.9(b) |
| `near_field_ped_population.h:732` flee destination | Still CHOSEN (not traced) |
| `traffic_lane_follower.h:743` arrivalRadius 1.5 | The §18.6 close gate 5.0 m with cos 30°; arrival law vt = 3.5 m/s |
| `traffic_lane_follower.h:744` lookahead 15 | Route walk lookahead 5000 m; re-plan every 300–450 ms. The obstacle probe is separate: L = v²/(2·k·`+0xc58`) + 2.5 m, k = 0.75 (1.0 above 20 m/s), §20.6.7(a) |
| `traffic_lane_follower.h:750` re-query 3.0 | 15.0 m, linear (`0x01117a60`) |
| `traffic_lane_follower.h:756` throttle 0.5 | Throttle law §20.6.3(b) with vt = 10 m/s and the 0.7–1.0 cap |
| `traffic_lane_follower.h:121` geometric lane adjacency | Use the on-disk `+0x3a`/`+0x3c` neighbours; a lane change is a route-search edge costing 6 m, never two in a row, `+0x3c` forbidden when flag bit 0 (§20.6.7(d)) |
| `traffic_lane_follower.h` following gap (none) | Car-following law of §20.6.7(b): 5 m standstill gap, comfortable decel 0.75·min(`+0xc58`, 10), 300 ms brake latch at 0.5 |
| `ambient_population.h` (distant tier, items 1–8) | Not changed by this pass (§17) |
| `police_roadblock_spawn.h:308` forward offset 50 m | Sites are where a road's shape polyline crosses the **135 m** circle (`Notoriety_roadblock_spawn_dist`) around the player, in his front half-plane (§20.6.10(b)); objects then sit at their layout `X_offset`/`Z_offset` |
| `police_roadblock_spawn.h:100` direction along heading | Road direction at the crossing site, facing the player (§20.6.10(b)) |
| `police_roadblock_spawn.h:331-333` generic vehicle / spike strip / case-4 prop | Composition comes from the `roadblock_layouts.xtbl` row (§20.6.10(c)); `spike_strip` is an `Item` row; dispatch cases 0–4 follow the layout `Type` enum (HIGH) |
| `police_roadblock_spawn.h:352` lifetime 45 s | No lifetime: removed when no player is within 270 m (2 × 135, squared 72 900) and the instance reports itself idle (§20.6.10(d)) |
| `police_roadblock_spawn.h:553` arm hook "not tied to notoriety" | Tie it: track level 1–5, `roadblock_notoriety.xtbl` row, timer rand[`Min_time`, `Max_time`], `Max_active_roadblocks`, global cap 3, player driving on a non-water road (§20.6.10(a)) |

Clean-room check: addresses are plain hex. Table, element and string names are shipped data. No
decompiler tokens are used.

#### 20.6.7 Car following, junction yield and lane changes (2026-10-14, tail pass)

**Correction first.** `0x00b5e6c0` is **not** the car-following law. It is the cross-traffic
**junction yield** test (b). Car following is a separate obstacle probe, `0x00b54cb0` →
`0x00b54450`, whose per-obstacle law is `0x00b51950` (a). All of them feed the same per-frame
combiner (function at `0x00b57ab0`, calls at `0x00b57c21`–`0x00b57d79`). The combiner takes the
**largest** braking demand and the **smallest** speed cap of all sources (signal, junction yield,
obstacle probe, destination stop) and hands the pair to the speed controller `0x00b283d0`, which
applies §20.6.3(b) (brake = min(1, demand ÷ `+0xc58`)).

Notation: v = own speed along own heading (vehicle `+0x64`), u = the obstacle's velocity projected on
**own** heading, d = gap (below), a_b = vehicle `+0xc58` (`AI_Max_Braking_Decel` × condition scale,
§20.6.3(b)), a_lim = min(a_b, 10) (20 for `Vehicle_Type` 2 Airplane, test `0x00ad3160`).

**(a) Obstacle probe `0x00b54cb0` — FINAL except where marked.**
- Re-run timer vehicle `+0x1118`: re-armed with trunc(max(0, (15 − v)/15 × 140)) + 60 ms (15.0 at
  `0x01117a60`, 140.0 at `0x01187b08`). So every 200 ms when stopped, 106 ms at 10 m/s, 60 ms at
  15 m/s and above. Between runs the previous result is reused.
- Probe length L = v² / (2·k·a_b) + 2.5 m, k = 1.0 when v > 20 m/s, else 0.75 (2.5 at `0x01117590`).
  **+6 m** while the remembered lead (vehicle `+0x1128` or `+0x1130`) still has a live 300 ms memory
  entry (6.0 at `0x012a2f7c`).
- The probe starts ahead of the vehicle at (box front extent − 0.3 + 3.75) m along the heading
  (`0x00b21b90`, 0.3 at `0x012a2db8`, 3.75 at `0x01186f98`; HIGH that the box value is the front
  extent), and is swept in ⌈L/8⌉ pieces (32 m pieces for Airplane), at most 10 (`0x00b53810`).
- Hits (up to 32, physics query `0x00b25c50`) are sorted by class in `0x00b54450`:
  - vehicles (class descriptor byte `+6` bit 7) → `0x00b52690`: gap d = the smallest distance along
    own heading from own front point to the four footprint corners of the other vehicle
    (`0x00b51db0`); then the law (b) with kind 0;
  - humans and other movers (class `+0xa` bit 0, not the player in activity mode 2 under flag 0x21)
    → `0x00b52880`, same law with d from the centre-to-centre projection;
  - two special object kinds (`0x00853b30` with 5 and with 10) set a flat result: brake 0, cap
    **50 m/s** and latch flags `+0xc70` 0x41/0x21. **OPEN:** which world objects those kinds are.
- The probe also records the nearest blocking vehicle in `+0x1128` and sets `+0xc70` bits 0, 3 and
  bit 1 ("lead is a human-driven vehicle") for the honk/wait logic of §18.7.2.

**(b) Following law `0x00b51950(vehicle, kind, target, d, targetVelocity, &brake, &cap)` — FINAL.**
```
a_c   = 0.75 * a_lim                       (0.75 at 0x012a2ed8)
dEff  = d - 5        (d - 3 when kind == 4)  (5.0 at 0x012a2f20, 3.0 at 0x012a2e18)
if dEff < 0.5 and u < 2:   dEff = 0
if dEff >= 0.01:
    vs  = u + sqrt(2 * a_c * dEff)
    req = (u >= v) ? 0 : (u - v)^2 / (2 * dEff)
else if kind == 1 and u > v:  req = 0, vs = u
else:                         req = a_b, vs = 0          (full braking, stop)
f = 0.5 if this target braked us within the last 300 ms, else 0.75     (memory list +0xee8)
if req > f * a_lim:          brake = req, cap = vs, remember target for 300 ms   (0x00b51860)
else if dEff < 6:            brake = 0, cap = max(u - 0.5, 2)
else if d - 4 > 0.01 and u + sqrt(a_b * (d - 4)) > v:   brake = 0, cap = u + sqrt(a_b * (d - 4))
else:                        brake = 0, cap = max(v, 2)                    (hold, do not accelerate)
```
The memory list (`+0xee8`, 24-byte entries, count `+0xfa8`, at most 8) gives the hysteresis: the
brake decision is taken at 0.75·a_lim and released only below 0.5·a_lim.

Kinds: 0 = obstacle from the probe; 1 = destination stop (`0x00b57c21`); 2 = traffic signal
(`0x00b52bc0`); 3 = junction yield (`0x00b5dec0`); 4 = mode-8 stop point (`0x00b52740`, 3 m
stand-off).

Consequences a port must reproduce:
- **Stop distance behind a stopped car: 5 m** from own front point to the other car's nearest corner.
- **Steady following gap ≈ 11 m** (dEff = 6) at any speed: below it the cap is u − 0.5, above it the
  cap rises above u + 9 m/s and the lane speed governs. There is no time headway.
- The probe only sees ≈ 3.45 + L m ahead, so at low speed the law only reacts to close obstacles.

**(c) Junction yield `0x00b5ed50` → `0x00b5e6c0` — FINAL for the gates, HIGH for the time windows.**
- Runs only when its timer `+0xfc0` expires; re-armed 150–300 ms. Skipped when the vehicle has a live
  memory entry of kind 4 or kind 2. Result bit: `+0xee0` bit 3 ("must yield").
- Look-ahead D = v² / (2·f·a_lim) + 5 m, f as in (b). The route entries ahead (route object
  vehicle `+0x11a4`) are walked, adding chord lengths, until a segment with a non-empty conflict list
  (§13.2a(e) `+0x38`) is found within D.
- No yield if the vehicle could not stop before it anyway: v²/(2·a_b) > distance to that segment.
- For every vehicle on each conflicting segment's occupancy list (§13.2a(j)), and on that segment's
  single-predecessor chain, `0x00b5dad0` predicts the entry and exit times of both cars with a
  constant **3 m/s²** acceleration (constants 6 = 2·3 and 3 at `0x01115250`/`0x012a2e18`) up to the
  target speed, then constant speed. The car yields when the two occupancy windows are **not**
  separated by more than **2 s** (±2.0 at `0x01116458`/`0x01187ebc`).
- The yield is applied as kind 3 of (b) at the start of the conflict segment (`0x00b5dec0`), so the
  car stops 5 m before it.

**(d) Lane changes — FINAL for the search, OPEN for any occupancy check.** **[2026-10-14: occupancy check RESOLVED, see the note at the end of (d).]**
- The planner never decides a lane change on its own. Lane changes come only from the route search
  (`0x00b1c380`–`0x00b1cc20`):
  - at the start, every lateral lane reached through the `+0x3c` (left) and `+0x3a` (right) chains
    is seeded with cost + **6 m per lane step** (6.0 at `0x01115250`);
  - during expansion a `+0x3c`/`+0x3a` edge costs **6 m** (**3 m** in search mode 1 when the target
    is a flag-bit-4 highway segment), and is **not** allowed from a node that was itself reached by a
    lane change (record `+0x16` bit 2), so two lane changes are never consecutive;
  - the `+0x3c` edge is skipped when flag bit 0 is set (oncoming lane), and `+0x3a` when bit 1 is set,
    unless the search mode allows it;
  - successor edges cost the chord length `+0x20`, halved in mode 1 on bit-4 segments, 0 on bit-31
    segments.
- So the car changes lane exactly when the cheapest route to its target turns from another lane:
  the change is worth it when it saves more than 6 m.
- Ambient branch choice at a fork, `0x00b28ac0`: one successor → take it. Otherwise, with probability
  0.8, drop the candidates `0x00b1d490` rejects. Then candidates are grouped by the number of live
  vehicles queued on them (`0x00a6df90`: occupancy lists along up to 10 single-link segments ahead),
  the least-queued group first, uniform inside a group, each pick confirmed by `0x00b27270`. In
  directive mode 8 with `+0x278` bit 0 the candidate pointing most toward the local player wins
  (queues of 3 or fewer preferred).
- **OPEN:** no check of the target lane's occupancy list was found on the lane-change edge; the
  physical merge is the ordinary lane follow of the next route entry. The §18.6b pass-side gate is a
  lateral offset, not a lane change.
  **[2026-10-14 RESOLVED: there is a target-lane occupancy check, and there is a runtime lane change
  outside the route search. The sentence above, "the planner never decides a lane change on its own",
  is wrong for highway segments.]**
  - **Trigger `0x00b55c50`** (vehicle as `this`), called from the per-vehicle tick at `0x00b58629`.
    It runs every 400–600 ms (directive timer `+0x34c`, armed with `0x00d9e180(0x190, 0x258)`). It
    only acts when the current route entry's segment has flag `+0x1c` bit `0x10` (the search's
    "bit-4 highway" flag) and the speed is at least 10 m/s (`0x00b55d7c`, HIGH). Lane pick:
    - if a right neighbour exists (`+0x3a`, `0x00677800`), try the right side, `0x00b55b90`
      (vetoed by lane flag bit 1);
    - else, if there is a left neighbour (`+0x3c`, `0x00677830`) and lane flag bit 0 is clear, try
      the left side, `0x00b55a00`;
    - on failure, fall through to `0x00b52e60`.
  - **Right side.** It first runs a presence sweep of the right lane beside the car, `0x00b557c0`. The
    probe record is built with flags 3 (count only), and `0x00b54450` sets record `+0x25` when the
    swept boxes hit anything. A hit sets `+0xee0` bit `0x40`, and the next pass does the full check
    below. HYPOTHESIS: why a hit, rather than a clear lane, arms the attempt was not decoded.
  - **Left side (overtake).** It needs all four of:
    - the probe holds a blocking lead (`+0xc70` bit 3, lead handle `+0x1128`/`+0x112c`);
    - `0x00ad2030(self)` is false;
    - the lead resolves to nothing, or to a vehicle in directive mode 8 whose `+0xfac` is lower
      than ours;
    - the gap (centre distance − `0x00a78d30(self)` − `0x00a78ca0(lead)`) is ≤ **20 m**
      (`0x01116990`).
  - **Occupancy check `0x00b555d0(vehicle, targetLane)`.** Both sides use it. A third caller sits in
    the combiner `0x00b57ab0` (`0x00b58264`), which moves right at once, with no delay, when the
    current segment lacks flag bit `0x4` and directive `+0x278` bit `0x20` is clear. The check:
    - is refused unless the directive mode is 8;
    - is refused when less than **30 m** (`0x012a2d58`) of route remains within a 50 m look
      (`0x00b1efb0` + `0x00b51490`);
    - then builds the path the car would take in the target lane (`0x00b53da0`) and runs the **full
      obstacle probe `0x00b54450` along it**, saving and restoring the `+0x1100…` lead block and
      `+0xc70`, followed by the following law (`0x00b510f0`);
    - refuses the change when that law would brake harder than **0.5 × `+0xc58`**
      (`0x00b55785`–`0x00b557a5`), or when a distance test with a 5 m margin fails
      (`0x00b55758`–`0x00b55783`; operands not fully decoded).
  - **Execution.** On the first pass that clears the check, `0x00ad5460(handle, side, 1)` is called
    once (side 1 = left, 0 = right; HYPOTHESIS: turn indicator). `+0xee0` bit `0x10` is set and timer
    `+0xfb0` is armed for **1200–1800 ms** (`0x00d9e180(0x4b0, 0x708)`). The occupancy check runs again
    on every later pass. When the timer expires, `0x00b53270(lane)` re-plans from the target lane
    (`0x00b1b200` clears the route, `0x00b50ea0` plans it again).

#### 20.6.8 Hood → spawn category (2026-10-14, tail pass) — FINAL

**Data.** The hoods are **`Hood` objects in the city's always-loaded zone** `sr3_city~fawld.czn_pc`
(objects section `0x2234`), not a separate table. `hoods.xtbl` only lists the names. 33 objects: 14
`Water_nn`, `cyberspace roads`, and 18 `HOOD_*`. Their properties (names are shipped strings,
descriptor list at `0x01301adc`):

| Property | Kind | Use |
|---|---|---|
| `hood_spawn_category` | string | category name → hood `+0x68` |
| `hood_alt_spawn_category` | string | alternative category → hood `+0x6c` |
| `hood_bsp_tree` | blob | the hood volume, below |
| `hood_island_type` | string | `mainland` / `ocean` |
| `hood_flags`, `hood_display_tag`, `hood_income`, `hood_takeover_areas` | | not used by spawning |

**Lookup.** `0x00911770` → `0x0084bb20(lastHood, position)`: test the cached hood first, then every hood
in the world object list (global `0x03171a64`, index list `+0x10c`, count `+0x114`, objects `+0x58`)
in list order; the first hood whose volume contains the point wins. `0x0084aed0` then returns the
global override category `0x0242ddd8` if set; else `+0x70` when hood `+0x64` bit 2 (per-hood override,
script `spawn_override_set_override_category_for_hood`); else `+0x6c` when bit 1 (alternate spawning,
`spawn_override_change_hood_alternate_spawning`, HIGH for the bit ↔ native pairing); else `+0x68`.
No hood → `Default Spawn`.

**Volume test `0x00dcda30` (FINAL).** The `hood_bsp_tree` blob is the tree object itself:
`u32 planeCount, u32 nodeCount, 8 runtime bytes (plane and node pointers), planes[planeCount]
{float nx, ny, nz, d}, nodes[nodeCount] {u16 plane, u16 childIfBehind, u16 childIfFront}`, padded to
4 bytes. Walk from node 0: s = n·p + d; if s < −0.0001 go to `childIfBehind` (0 → **outside**), else
`childIfFront` (0 → **inside**). y is up; coordinates are the absolute world metres of §13.2a(a).
Script: `tools/scratchpad/poptail_hoods.py` (extract) and `poptail_hoodtest.py` (test).

**Shipped mapping:**

| Hood | Category | Alternate |
|---|---|---|
| `HOOD_DT_01` | `sp_cat_HenryMills` | `sp_cat_HenryMills_Ball` |
| `HOOD_DT_02` | `sp_cat_LorenSquare` | `sp_cat_Loren_STAG` |
| `HOOD_DT_03` | `sp_cat_SunsetPark` | `sp_cat_Sunset_STAG` |
| `HOOD_NE_01` / `_02` / `_03` / `_04` | `sp_cat_CutterAirport` / `sp_cat_CamanoPlace` / `sp_cat_RosenOaks` / `sp_cat_Espina` | — |
| `HOOD_NW_01` / `_02` / `_03` / `_04` | `sp_cat_BurnsHill` / `sp_cat_Ashwood` / `sp_cat_Salander` / `sp_cat_Brickston` | — |
| `HOOD_ZOMBIE` | `sp_cat_DownersGrove` | `sp_cat_Arapice_Zombies` |
| `HOOD_DOWNERS_GROVE`, `HOOD_BRIDGEPORT`, `HOOD_NEW_BARANEC`, `HOOD_YEARWOOD`, `HOOD_POINT_PRYOR` | `sp_cat_DownersGrove`, `sp_cat_Bridgeport`, `sp_cat_NewBaranec`, `sp_cat_Yearwood`, `sp_cat_PointPryor` | — |
| `HOOD_SE_01` | `sp_cat_SierraPoint` | — |
| `Water_01`…`Water_14` | `Water` | `Water` |
| `cyberspace roads` | `CyberSpace_Ambient` | `CyberSpace_Ambient` |

**Test vectors (§13.2a(n) tiles):**

| Point | Hood | Category |
|---|---|---|
| tile 1215, node 36 (−203.5, 16.33, −511.5); tile centre (−320, 16, −560) | `HOOD_DT_01` | `sp_cat_HenryMills` (Car 1/.7, Ped 1/.5) |
| tile 1517, node 6286 (745.326, 2.975, −980.073); tile centre (640, 5, −1120) | `HOOD_NW_02` | `sp_cat_Ashwood` (Car .8/.5, Ped .8/.5) |
| (0, 0, 0) | `HOOD_DT_02` | `sp_cat_LorenSquare` |
| (−1500, 0, 2000) | `Water_03` | `Water` |

Targets with §20.6.4's assumptions (vehicles on foot / driving, peds):
- `sp_cat_HenryMills`: 03:00 → 8 / 12, 20; 08:00 → 10 / 15, 30; 14:00 → **12 / 18, 40**.
- `sp_cat_Ashwood`: 03:00 → 6 / 9, 20; 08:00 → 7 / 11, 26; 14:00 → **9 / 14, 32**.

#### 20.6.9 Ambient ped walking (2026-10-14, tail pass)

**(a) Speeds — FINAL for the mechanism; numbers FINAL per clip (2026-10-14, below).** The sidewalk walk request
(`0x0051bac0` → `0x00a1bc90(…, 1, −1)`) and the flee/cower requests pass the locomotion speed **−1**,
the "use the animation's speed" sentinel (`spec-player-runtime.md` §5.4.3 step 4). The velocity is
therefore the root motion of the playing locomotion clip, scaled by the human's speed multiplier
`+0xa64` and the stride-match rate `+0x1a90` (clamped [0.1, 1.5]). A scan of all calls of the
locomotion setter `0x00942110` finds literal speeds only outside the ambient ped paths (2.5 at
`0x009acd63`). **There is no walk, run or flee speed constant to copy.** The numbers need the root
translation per second of the civilian walk/run clips (`spec-anim-format.md`), not traced.
**[2026-10-14 numbers: FINAL for the clip speeds and for `+0xa64` = 1.0 on ambient peds; PARTIAL for which state
flee selects.]**

- **Formula.** The −1 branch gives s = (\|T\|/Δt) / `+0x1a90` × `+0xa64` (`spec-player-runtime.md` §5.4.3 step 4,
  code at `0x0099d4f8`). T is the per-frame root translation of the synchronised locomotion layer, which plays at
  rate `+0x1a90`, so the rate cancels. Averaged over a cycle, s = (clip root speed) × `+0xa64`. The clip root speed
  is the same quantity `0x004b49e0` reports for a plain clip: \|header `+0x1C` translation\| / (header word `+0x06` / 30)
  (`spec-anim-runtime.md` §9; `0x004bd8c0` duration = word `+0x06` / 30 s). Within one cycle the speed can vary with
  the root track; only the cycle average below is decoded.
- **`+0xa64` on ambient peds = 1.0.** The human constructor stores float `0x01117a4c` = 1.0 at `0x009544c5`. The
  other writes do not reach ambient population peds: the area spawn-group setup `0x006e7280` copies its
  group entry's float `+0x28` (`0x006e7469`); case 10 of the command dispatcher at `0x006ed9aa` (`0x006edb8f`) writes
  the command's float `+8` into the **local player**; and case 10 of `0x006ebc00` (`0x006ebdc1`) resets it to 1.0.
  There is no random range.
- **Clips.** The skeleton sets are from `anim_set_filenames.xtbl`. `PEDM` (parent `Life_Default` → `anim_Default`) is
  the male civilian. `PEDF` (parent `Life_Female`, whose parent is `PEDM`) is the female civilian. All clips are in
  `preload_anim.vpp_pc`; every one has Y = 0 and no root rotation, so the speed is all horizontal, along +Z.

| Set | State | Clip | Frames (word `+0x06`) | \|T\| m | Speed m/s (×1.0) |
|---|---|---|---|---|---|
| PEDM | `walk` | `ped_bs_walk` | 33 | 1.5250 | **1.386** |
| PEDM | `walk variant 1..12` | `ped_bs_walk_b` / `_c` / `_d` / `_e` / `_f` (and `ped_bs_walk` for variant 6) | 31 / 26 / 29 / 151 / 66 | 1.911 / 1.959 / 1.956 / 5.431 / 3.314 | 1.849 / 2.260 / 2.023 / 1.079 / 1.506 |
| PEDM | `run` | `ped_bs_run` | 33 | 7.0100 | **6.373** |
| PEDM | `run variant 1..4` | `ped_bs_run_b` (v1) / `ped_bs_run_e` (v2, v4) / `ped_bs_run_d` (v3) | 28 / 33 / 15 | 5.682 / 6.320 / 3.205 | 6.088 / 5.745 / 6.410 |
| PEDM | `cower run` (+ variants 1–5) | `ped_cwr_rn`, `ped_cwr_rn_d`, `ped_cwr_rn_e` | 45 / 47 / 45 | 6.830 / 8.160 / 8.717 | **4.554** / 5.208 / 5.812 |
| PEDF | `walk` | `ped_f_bs_walk` | 33 | 1.5050 | **1.368** |
| PEDF | `walk variant 1..12` | `ped_f_bs_walk_b` / `_c` / `_d` / `_e` / `_f`, `pfl1_bs_walk` (variant 10) | 33 / 31 / 53 / 119 / 120 / 33 | 1.589 / 1.602 / 3.614 / 2.764 / 6.546 / 1.650 | 1.445 / 1.550 / 2.046 / 0.697 / 1.637 / 1.500 |
| PEDF | `run` | `ped_f_bs_run` | 41 | 6.0590 | **4.433** |
| PEDF | `run variant 1..4` | `ped_f_bs_run_b` / `_c` / `_d` / `_e` | 80 / 20 / 51 / 30 | 10.180 / 2.554 / 4.928 / 6.374 | 3.818 / 3.831 / 2.899 / 6.374 |
| PEDF | `cower run` (+ variants 1–5) | `ped_bs_run_e`; variants `ped_f_bs_run_e`, `ped_bs_run_b`, `ped_bs_run_e`, `ped_bs_run_d` ×2 | — | — | **5.745**; 6.374 / 6.088 / 5.745 / 6.410 / 6.410 |
| both | `head cover walk` / `head cover run` | `ped_rn_hd_cvr_rn` | 20 | 2.3361 | 3.504 |

- **Test vectors** (ambient ped, `+0xa64` = 1.0, cycle average): male walk 1.525 m / 1.1 s = **1.3864 m/s**; male run
  7.010 / 1.1 = **6.3727 m/s**; male cower run 6.8304 / 1.5 = **4.5536 m/s**; female walk 1.505 / 1.1 = **1.3682 m/s**;
  female run 6.059 / (41/30) = **4.4334 m/s**; female cower run 6.320 / 1.1 = **5.7455 m/s**. Cross-check: two clips
  give round numbers exactly, `pfl1_bs_walk` 1.65 m / 1.1 s = 1.5000 and `hs_wlk_a` 8.25 m / 5.5 s = 1.5000.
- **Still OPEN.** (1) Which state the flee mode asks for: `run` (gait 2) or `cower run`. Both are tabled above;
  `panic_reactions.xtbl` names only `run on fire` (2.802 m/s, `ped_bs_run_onfre`) and none of its rows is a plain flee.
  (2) How a ped picks among `walk variant N` / `run variant N`. The names are literal strings in the exe and were not
  traced. A port can use the base `walk` / `run` row and treat the variant columns as the spread. There is no sprint
  state in `PEDM`/`PEDF` (only gang, `streaking` and weapon sets have one).
  **[2026-10-14 RESOLVED (both).**
  **State ids.** The exe's state-name table at `0x011507F8` (index 0 `custom state`, 2 `stand`) gives:
  - `walk` 26 (`0x1A`) and `walk variant 1..9` 27–35;
  - `run` 44 (`0x2C`) and `run variant 1..3` 45–47;
  - `cower run` 54 (`0x36`) and `cower run variant 1..5` 55–59;
  - `cower stand` 13 and its variants 14–17;
  - `stand ready` 60.

  The exe names only 9 walk variants and 3 run variants, so the higher variant columns of the anim-set table are not reachable through these names.
  **Per-ped slots, set when the human is initialised (`0x00956E50`).**
  - Gait `+0x1CC8` = 2.
  - Walk state `+0x1CCC`: 26, or with the flag argument clear `0x00956D50` picks it **uniformly at random among the states 26–35 that the ped's anim set actually has** (`0x004B1630(set, state) ≠ −1`, then `0x00DAB660(0, n−1)`). The pick is skipped (stays 26) for objects with flag `+0x48` bit `0x2000`, `0x00853B30(human, 0x21)`, `0x00921060`, `+0x2BC` bit 7, or `0x00720640()`.
  - Run state `+0x1CE0` = 44.
  - Cower-run state `+0x1CD4` = 54.
  - Cower-stand `+0x1CD8` = 13 + random(0..4), falling back to 13.
  - Stand-ready `+0x1CE4` = 60.

  **Run variant.** `0x00992219`: when a movement request with `+0xB35` ≥ 2 (HIGH: run gait) starts and the run slot is still −1 or 44, the ped takes `run variant 1 + ((handle low dword mod 3 + k) mod 3)` for the first k = 0, 1, 2 that its anim set has, written through `0x0094B750` into `+0x1CE0`. This choice is **deterministic per ped handle, not random**. If none exists the slot stays `run`.
  **Flee: run, and cower run only for a civilian close to the threat.** The run-state selector at `0x0095C495` returns the cower-run slot `+0x1CD4` only when all three hold: the AI mode `+0x501` = 13 (`Flee`), the team `+0x1CA8` = 7 (civilian), and the flee block's `+0x8D0` bit 0 is set. In every other case it returns the run slot `+0x1CE0`. A second site (`0x0095C99A`) does the same for sub-state `+0x500` = `0x72` with `+0xCCC` = 2.
  Bit 0 is managed every Flee update by `0x004D8100` (called from `0x004DA965`):
  - **Cleared** when the ped is not a civilian (`0x00943EA0`: team 7 and `0x0097E280` false), or when it is more than **30 m** (squared distance > 900, `0x01115340`) from the flee point `+0x8E8` (when block bit 5 is set) or from the threat (handle `+0x8D8`, via `0x004D4C70`).
  - **Set** on first entry within 30 m. At that moment the cower-run state is chosen: `run` (44) if human `+0x2BB` bit 7 is set; otherwise 54 + random(0..5) (`cower run` or one of its 5 variants), falling back to 54 when the set lacks it. It is stored in `+0x1CD4` through `0x0094B330` (skipped when `+0x317` bit 6), and only if that state's clip is resident (`0x004BF5E0`/`0x004BEBB0`).

  **Net effect.** A fleeing civilian runs with its `cower run` clip (4.554 m/s male base) while within 30 m of the threat, and with its run-variant clip beyond 30 m. Non-civilians always use run.]**

**(b) Path on the sidewalk graph — HIGH.**
- A ped on the sidewalk follows a **path to a destination**, not a node-by-node random walk. The path
  is a best-first search over sidewalk **nodes** (`0x00a1d410`): both ends of the start edge are
  seeded with their straight-line distance; edges cost their Euclidean length; the open list caps
  costs at 50 000 m (`0x0117835c`).
- The destination is the wander target chosen in sub-state 33 (§20.2). Event 81 is posted on commit.
- On every frame the navigator (`0x00a1bcd0`) checks the current edge's crossing.

**(c) Crossings and lights — FINAL for the test, HIGH for the state machine.**
- An edge is closed (`0x00bbf0e0`) when its crossing record has a light and that light's vehicle
  state is 2, 3 or 5, or its pedestrian state is 2 (§13.2a(i)). An edge without a light is never
  closed.
- Navigator state bits in human `+0xb68`: bit 2 = on a sidewalk route, bit 3 = waiting at the curb,
  bit 4 = committed to the crossing, bit 5 = past the far side.
- Closed crossing: if bit 4 is set the ped keeps crossing; if bit 3 is set it keeps waiting.
  Otherwise the ped is tested against the near and the far edge of the crosswalk quad
  (`0x00dbbaa0`): before the near edge → set bit 3 and **stop**; past the far edge → set bit 5 and
  carry on; between the two → set bit 4 and **finish crossing**.
- Open crossing: bits 3–5 are cleared and the ped walks on.
- Within 0.425 m of the crossing (0.180625 = 0.425², `0x01117f10`) `0x0051b960` reports arrival, and
  the sidewalk tick posts event 8 with a 1000 ms delay.

#### 20.6.10 Police and gang roadblocks driven by notoriety (2026-10-14, tail pass)

This closes the notoriety path of §19.7 (`0x0060a8d0`). Tables: `misc_tables.vpp_pc`
`roadblock_notoriety.xtbl` and `roadblock_layouts.xtbl` (schemas: `spec-tables-traffic-ai.md` §5, §6).

**(a) When — FINAL.** Every frame `0x0060a8d0`:
1. Gates: roadblocks enabled (`0x0092b2b0`, cvar §19.7), byte `0x012ed170` (default 1), live
   roadblock count `0x012ed180` < **3** (`0x012ed17c`), and `0x0060a490`: the target player is
   **driving** (`0x009e82f0` and `0x009b9160`) on a road segment (player `+0x20dc`) without flag bit 18
   (water). In co-op `0x0060a700` picks the target player: the partner when the two are within
   2·135 + 40 = 310 m and headings agree (dot > 0) and `0x0092c6d0` passes; otherwise the player with
   more qualifying world objects nearby, ties broken 50/50 (HIGH for the meaning of the count).
2. For each grid slot 0–4 (slot 3 = Police, slot 4 = STAG; `0x00702a10` picks which one of the two
   runs): track level L from `0x00605200`; skip unless 1 ≤ L ≤ 5; skip when the team's suppression
   object says so (`0x008ed220` → `0x008ecdc0`).
3. Record `0x014a50b8 + 0x44·(5·slot + L − 1)`. Skip when it has no layouts, or when the slot's live
   count (`0x014a5768 + 40·slot`) ≥ `Max_active_roadblocks`.
4. Slot timer (record `+0x0c`): not armed → arm with a uniform `Min_time`…`Max_time` ms and stop.
   Armed and expired → candidates = the layouts whose own `Cooldown` timer is unarmed or expired.
5. Place (b). On success: arm the chosen layout's cooldown with its `Cooldown` ms, re-arm the slot
   timer, add the instance to the slot's live list. On failure retry in **2000 ms**.

Shipped data has rows only for levels **2–5**, so **level 1 never spawns a roadblock** although the
code accepts it.

**(b) Where — FINAL for the rule, HIGH for the lane match.**
- Distance: tweak `Notoriety_roadblock_spawn_dist` = **135.0 m** (`0x014a50a0`, registered at
  `0x00ff38d0`).
- `0x0092e150` builds up to **8** sites: road segments from the spatial query, filtered like
  §20.6.1(c), at the points where a segment's shape polyline crosses the 135 m circle around the
  player (`0x00dbd630`). The highway bit (`0x00a6bb00`: flag bit 4 of the player's segment) selects
  highway sites.
- `0x0092e780` tries every (site, layout) pair from a random start of each: the layout must fit the
  site (`0x0092b130`, HIGH: lane count against `Num_lanes`) and the object budget (`0x0092ade0`), and
  the site must be in the player's front half-plane, dot(site − player, player heading) > 0. The
  first fit creates a class-`0x2c` roadblock instance at the site, oriented toward the player.
- Each layout object is then placed at its `X_offset`/`Z_offset` relative to the site and lane
  (`Lane_index`); this is the "forward offset" of §19.7 (HIGH).

**(c) What — FINAL (table rows).** The §19.7 dispatch cases 0–4 follow the layout object `Type`
enum: 0 `Vehicle`, 1 `Vehicle Group`, 2 `NPC`, 3 `Item` (the `spike_strip` and `construct_sawhorse01`
rows are Items, hence the `"Spike_strip"` name check), 4 `Action Node` (HIGH by enum match). Shipped
schedule:

| Team | Level | Max active | Interval (s) | Layouts (cooldown s) |
|---|---|---|---|---|
| Police | 2 | 1 | 15–40 | `police_lvl2_2lane` (10), `police_lvl2_2lane_2` (25), `police_lvl2_4lane` (0) |
| Police | 3 | 2 | 10–30 | `police_lvl3_2lane`, `police_lvl3_4lane` |
| Police | 4 | 2 | 5–20 | `police_lvl4_2lane`, `police_lvl4_4lane` |
| Police | 5 | 3 | 0–10 | `police_lvl5_2lane`, `police_lvl5_4lane` |
| Deckers / Morningstar / Luchadores | 2 / 3 / 4 / 5 | 1 / 2 / 2 / 3 | 20–40 / 15–30 / 10–20 (Morningstar, Luchadores 5–20) / 0–10 | `<gang>_lvl<L>_2lane`, `_4lane` |
| STAG | 2–5 | 1 / 2 / 2 / 3 | as Police | Police layouts at 2–3; `stag_lvl4_*` + `police_lvl4_*` at 4; `stag_lvl5_*` at 5 |

Script: `tools/scratchpad/poptail_rb.py`.

**(d) Despawn — FINAL for the distance, HIGH for the second test.** `0x0060a410` keeps an instance
while any player is within **270 m** (the squared value 72 900 = (2·135)² at `0x014a50a4`, test
`0x009ea700`). Outside that it is removed when `0x0092ec60` reports the instance idle (HIGH: no
member engaged). Removal is through `0x00853ea0`.

**Test vector — Police, level 3, player driving on a 2-lane road:** at most 2 roadblocks of this
track (3 in total), one every 10–30 s, 135 m ahead. `police_lvl3_2lane` holds 2 vehicles
(`van_2dr_riot02` lane 1, `car_4dr_police02` lane 2, about 9.5 m behind the site line), 2
`npc_cop_swat_riot`, 2 `construct_sawhorse01` each with 2 child `npc_cop` (4 cops), and 3
`spike_strip`. On a 4-lane road `police_lvl3_4lane`: 2 `van_2dr_riot02`, 2 `car_4dr_police02`, 6
`npc_cop_swat_riot`, 6 `npc_cop`, 8 `spike_strip`.

#### 20.6.11 Earlier PARTIALs resolved (2026-10-14, tail pass)

**(a) Obstruction points (§20.6.1(c), 40 m) — HIGH.** The list `0x02898228` (32-byte records, count
`0x0289821c`, pool of 0x4000 bytes) is rebuilt every **700–1300 ms** (timer `0x0130ec34`,
`0x00b2da80`) from every vehicle's own chain (vehicle `+0xc74`). A record holds the vehicle position,
its handle, the route segment and two flags; flag bit 1 also sets segment flag bit 20 and counts in
`0x02898220`. The chains are written by `0x00b2dd90`, called from the vehicle update (`0x00a86cf0`,
`0x00a87eeb`) every 700–1300 ms for AI-driven vehicles that are **slow** (velocity test against
4 m/s) in directive modes 0, 1 or 3. So the points are **stopped or crawling AI vehicles**, and the
40 m rule keeps new traffic from spawning right behind a jam.

**Correction to §20.6.1(b):** `0x00b2db20` is not a "player chase" test. It counts AI vehicles, other
than the local player's current and last vehicle, within **35 m** (1225 = 35², `0x01115208`) of the
camera whose velocity components are all below 2 m/s and whose directive mode is not 0. With the
argument 4 it is true for **4 or more stopped cars**. The extra 4000–6000 ms timer therefore gates
spawning next to a traffic jam (HIGH for the wording, FINAL for the test).

**(b) Player `+0x1d9a` bit 1 — PARTIAL.** Written only at `0x009ea4df`: set when the point 2 m above
the player (`+0x40`, y + 2.0) lies in a zone whose handle kind (bits 13–15) is 1 or 2 (`0x005812d0`
→ `0x00861aa0` → `0x0085be00`), and a second point from `0x009e2d00` does too. Kind 3 is the world
tile lattice (`0x6000`); kinds 1 and 2 come from the two volume-zone registries `0x02444fa4` and
`0x02444fa8`. HIGH: "player inside an interior-type zone". Effects already documented: +21 ped budget,
and an 8 m height rule for ped despawn (§20.6.2). **OPEN:** the name of zone kinds 1 and 2.

**(c) Stop-line geometry (§20.6.3(c)) — FINAL.** The distance `0x00b52bc0` passes is vehicle `+0x11dc`
= route object (`+0x11a4`) `+0x38`, the distance to the **end of the current route piece**. The
signal state is read from the current piece's own segment (`+0x30 & 0xf`), i.e. the signalised
approach lane. The car is stopped by law (b) with kind 2, so it stops **5 m before the end node of
the approach lane**. Correction: the waiting bit `+0xc70` bit 2 is set when the law returns a brake
demand > 0 **or a speed cap below 3 m/s**, not "within 3.0 m".

#### 20.6.12 Test vectors and status (tail pass)

**Car following, car_4dr_standard03** (`AI_Max_Braking_Decel` 12.06, condition 0 → a_b = 12.06,
a_lim = 10, a_c = 7.5):

| Case | Result |
|---|---|
| v = 10, lead stopped | brake onset at d < 11.667 m (100 / 15 + 5) |
| v = 10, lead stopped, d = 10 | demand 10.0 m/s² → brake 0.829; cap 8.660 m/s |
| v = 10, lead stopped, d = 8 | demand 16.67 → brake 1.0; cap 6.708 |
| any v, d ≤ 5.01 | full braking (12.06), cap 0: **stop 5 m behind** |
| v = 10, u = 8, d = 10 | no brake (0.4 < 7.5); cap 7.5 |
| v = u = 10, d = 15 | cap 21.5 (lane speed governs) |
| v = u = 10, d = 10 | cap 9.5 → steady gap ≈ 11 m |
| Probe length at v = 0 / 10 / 25 | 2.5 / 8.03 / 28.4 m (k = 0.75 / 0.75 / 1.0) |
| Probe period at v = 0 / 10 / ≥ 15 | 200 / 106 / 60 ms |
| Junction look-ahead at v = 10 | 11.67 m; re-check every 150–300 ms |

Other vectors: hood categories §20.6.8; roadblock composition §20.6.10.

| Item | Status |
|---|---|
| Following law, probe length and cadence, stop gap 5 m, hysteresis | FINAL |
| Junction yield gates and look-ahead | FINAL; time-window detail HIGH |
| Lane-change cost and rules in the route search; fork choice by queue length | FINAL |
| Occupancy check before a lane change | OPEN (none found) |
| Hood → category data, volume test, both test tiles | FINAL |
| Ped walk/run/flee speed numbers | OPEN (animation-driven, mechanism FINAL) **[2026-10-14: numbers FINAL per clip (§20.6.9(a)); PARTIAL only for which state flee picks, and for variant choice]** **[2026-10-14: both RESOLVED, §20.6.9(a). Flee uses cower run for a civilian within 30 m of the threat, otherwise run. The walk variant is uniform random at init; the run variant is handle mod 3]** |
| Ped sidewalk path search and crossing gate | HIGH / FINAL |
| Roadblock trigger, timers, caps, distance 135 m, despawn 270 m, composition | FINAL; lane-fit and object offsets HIGH |
| Special probe object kinds 5 and 10 | OPEN |
| Obstruction points | HIGH |
| `+0x1d9a` bit 1 | PARTIAL (zone-kind names OPEN) |
| Stop-line geometry | FINAL |

Scratch scripts (read-only, deletable): `tools/scratchpad/poptail_dis.py`, `poptail_run.ps1`,
`poptail_fstart.py`, `poptail_str.py`, `poptail_disp.py`, `poptail_locoargs.py`, `poptail_hoods.py`,
`poptail_bsp.py`, `poptail_hoodtest.py`, `poptail_rb.py`; outputs in `tools/scratchpad/poptail_out/`.

Clean-room check: addresses are plain hex; property, table and string names are shipped data; no
decompiler tokens.

#### 20.6.13 Traffic lights: cycle, phases, stop rules — FINAL

Static pass, 2026-10-17. Tags: **FINAL** = read end to end; **HIGH** = one inferred step; **GUESS** =
not proven. Record layouts are in `spec-zone-data-format.md` §13.2a(b)/(i); this section adds the
runtime. The data lives in the one whole-city Traffic section (`0x2236`) of `sr3_city~fawld.czn_pc`;
the per-tile Traffic-signals sections (`0x2241`, 152 files) carry only the 72-byte light visuals.

**(a) Controller structure — FINAL.** Singleton `0x02705fe0` (header of §13.2a(b)):

| Header pair | Array | Used by the controller as |
|---|---|---|
| `+0x50`/`+0x54` | junction, 20 B | the intersection: `+0x0c` first phase, `+0x0e` current phase (u8), `+0x10` timer |
| `+0x58`/`+0x5c` | phase, 6 B | `+0x00` first light-list index, `+0x02` first lane-list index, `+0x04` duration (u16, ms) |
| `+0x60`/`+0x64` | u16 light list | the lights shown green in that phase |
| `+0x68`/`+0x6c` | u16 lane list | the approach lanes released in that phase |
| `+0x48`/`+0x4c` | light, 16 B | `+0x0e` vehicle state, `+0x0f` pedestrian state |

- The intersection grouping is the junction record. Nothing is built at run time; the phase count is
  the next junction's `+0x0c` minus this one's (`0x00a68fb0`), and the list lengths come the same way
  (`0x00a69010` lights, `0x00a68fe0` lanes). No lane is listed twice (1,544 distinct).
- The timer is the engine's dword deadline on the millisecond clock `0x01320d94`: `0x00d9e140(ms)` arms
  it at now + ms, `0x00d9e1e0` = expired (now ≥ deadline; −1 = never), `0x00d9e210` = ms remaining.
- Lane state = segment `+0x30` low nibble: 1 proceed, 2 amber, 3 stop. Five writers, all keeping the
  high nibble: `0x00a69ed3` (old phase → 3), `0x00a69ffd` (new phase → 1), `0x00a6a0ad` (amber → 2),
  `0x00a6a223` (hold → 3, (g)), `0x00a6aa5f` (init). (`0x00a69d1d`/`0x00a69d85` write `+0x30` of
  synthetic route pieces, not of graph segments.) **The only reader of the nibble in `.text` is the
  car test `0x00b52c25`** (whole-section sweep for a `+0x30` byte load followed by `and 0xf`).
- Light writers `0x00a6f2f0` (vehicle) / `0x00a6f350` (pedestrian) store the byte and switch the glare
  effect when the light's zone is resident (HIGH that these are the `effects.xtbl`
  `Env_Stoplight_RedGlare`/`YellowGlare`/`GreenGlare` rows).

**(b) The tick `0x00a69da0(junction)` → bool "touched" — FINAL.**
```
cur = phase[j.first + j.current]
if lightCount(cur) == 0:                     return false   # lightless junction: frozen
if !zoneResident(light[cur.lights[0]].+0x0c): return false  # 0x00861e60; not streamed in
if expired(j.timer):                                        # switch
    for l in cur.lights: veh(l)=3; ped(l)=2
    for s in cur.lanes:  s.+0x30 = (s.+0x30 & 0xf0) | 3
    j.current += 1; if j.current >= phaseCount(j): j.current = 0
    nxt = phase[j.first + j.current]
    arm(j.timer, nxt.duration)                              # 0x00a69f35
    for l in nxt.lights: veh(l)=1; ped(l)=1
    for s in nxt.lanes:  s.+0x30 = (s.+0x30 & 0xf0) | 1
    touched = true
elif remaining(j.timer) < 3000:                             # 0x00a6a01f, immediate 0xbb8
    for l in cur.lights: veh(l)=2                           # pedestrian byte untouched
    for s in cur.lanes:  s.+0x30 = (s.+0x30 & 0xf0) | 2
    touched = true
else: touched = false
refresh every light of every phase (0x00a6f240)
return touched
```
- Old red and new green are written in the same call: **there is no all-red interval** (FINAL).
- **Amber is the last 3,000 ms of each phase, hard-coded**; green = duration − 3000 (FINAL).

**(c) Scheduling — FINAL.** The world update `0x00702a50` calls `0x00a6ab30` once per frame (call at
`0x00702c87`). It ticks junctions from the cursor `0x02705ffc` upward and **stops at the first junction
whose tick returns true**, leaving the cursor after it; at the end of the array it resets to 0 on the
next call. A junction in its amber window returns true on every visit, so a full lap takes about
(number of resident junctions in amber or switching) + 1 frames. Every switch and the amber onset are
therefore late by up to one lap, and the next phase is timed from the actual switch, so phases only
lengthen (HIGH for the size: a few frames with the handful of junctions resident around the player).

**(d) Phases and their order — FINAL for the code, HIGH for the geometry statistics.**
- Phases run in record order 0, 1, …, wrap. There are no turn arrows and no protected-turn phase; the
  turning connectors inside the junction are not in any list (they stay 1), so a turn across oncoming
  traffic is a permissive turn settled by the junction yield (§20.6.7(c)).
- Shipped classes (346 junctions; `tlight_stats*.py`):

| Class | Count | Behaviour |
|---|---|---|
| lit, 2 phases, 10,000 / 10,000 ms | 259 | each approach: 7 s green, 3 s amber, 10 s red; cycle 20 s |
| lit, 2 phases, 15,000 / 10,000 or 10,000 / 15,000 | 2 | 12 s / 7 s green; cycle 25 s (junctions 187, 231) |
| lit, 2 phases, 10,000 / 0 | 1 | junction 162: phase 1 is green for one controller visit, never amber |
| lit, 1 phase, 10,000 | 1 | junction 26: 7 s green, 3 s amber, repeating; the switch writes 3 then 1 in one call, so never red |
| lightless (no light in phase 0) | 83 | never ticks; every listed lane stays 1 after init ((e)) |

- Grouping of the 262 two-phase lit junctions: in 227 the two phases use disjoint road axes, and
  phase 0 holds an opposing pair in 239. Typical: **phase 0 = both directions of one road,
  phase 1 = the cross road** (one direction only in 127 junctions: T stems and one-way streets).
  Lanes per phase 0–6 (mode 2); lights per phase 1–10.
- Durations are data only: phase `+0x04`, read at `0x00a69f2c` (switch) and `0x00a6a93b` (init). The
  exe has no light-timing tweak string and no `.xtbl` holds one (only the glare effects). **No
  randomisation and no per-junction offset**: the controller's only random call is in the picker of (g).

**(e) Initial state — FINAL.** `0x00a6a8a0`, called from world start/load `0x00708330` (at `0x0070848b`):
1. every light: vehicle 3, pedestrian 2;
2. every junction: current phase 0, timer armed with phase 0's duration — **all junctions start in step**;
3. every phase p: lights 1/1 if p = 0 else 3/2; lanes: 1 if p = 0 **or the phase has no light**, else 3.
On disk every listed lane is 3 and every light (3, 2) (§13.2a(i)); without this init the 83 lightless
junctions would be red for ever. When a tile streams in, the `0x2241` loader matches its light records
and pushes the stored live states to the visuals (`0x00a6f3b0`, §13.2a(i)). A junction whose zone was
not resident keeps its absolute deadline; on the first visit after expiry it switches once and re-arms
from that moment (HIGH).

**(f) Per-car rules, consolidated — FINAL unless marked.**
- Where: the stop target is the end node of the approach lane; the car stops **5 m** before it
  (§20.6.11(c), law kind 2 of §20.6.7(b)).
- Amber and red are one rule (states 2 and 3 share the branch `0x00b52c2c`–`0x00b52c38`): with d = distance
  to the lane end (floored 0.001), **stop if v²/(2d) ≤ vehicle `+0xc58`, else drive through**
  (§20.6.3(c)). No random red-runner exists. The waiting bit `+0xc70` bit 2 is set when the law brakes
  or caps below 3 m/s.
- Go: state 1 removes the source at once. The 300 ms memory entry of kind 2 keeps the junction yield
  skipped for at most 300 ms; after that the yield (§20.6.7(c), every 150–300 ms) decides against the
  conflict lists; there is no extra start-up delay.
- Who obeys: the test needs `+0xc6c` bit 0 (`obey_traffic_lights`) and runs only inside the combiner
  `0x00b57ab0` (sole caller of `0x00b52bc0`). The mode setter `0x00b24e40` sets the bit for modes 0, 7, 8,
  15, keeps it for 11 and 14, clears it otherwise (jump table `0x00b24f84`, §18.7.1). The combiner runs
  for mode 8 (§22.1) and, HIGH, for mode 14 (`0x00b50df8`). So: **ambient traffic obeys**; a held
  car (mode 14) that came from mode 8 obeys; chase (4), flee (2), rejoin (6) and the other modes ignore
  lights; modes 0/7/15 set the bit but never run the test (HIGH). `vehicle_set_obey_traffic_lights`
  toggles the bit until the next mode change (`spec-lua-api-behaviour.md` §4.5a).
- No vehicle-type or siren exception: ambient police and emergency cars in mode 8 stop; police in
  pursuit (mode 4) do not. The player's car has no combiner, and with a single nibble reader there is
  no red-light crime (HIGH).
- `vai_force_flags` `no_signals` is **not** a traffic-light flag: it is `+0xc6e` bit 0x10, read only by
  `0x00ad5460`, where it clears vehicle `+0x16c0` bits 0x800/0x1000. It suppresses the turn indicators
  (class `vehicle_turn_signal_manager`), which settles the §20.6.7(d) guess for `0x00ad5460` (HIGH).

**(g) Unsignalised junctions and stop signs — FINAL (code), HIGH (naming).**
- Segment flag **bit 7 is a stop line**, not a "signalised approach" (corrects the §13.2a(d)
  HYPOTHESIS). Reader: `0x00b50f80` tests the sign of the flags' low byte (`0x00b50fca`, `0x00b51016`) via
  `0x00a6b0b0`, walking the route from the current entry and skipping entries already marked served
  (route entry `+8` bit 0x10).
- Rule `0x00b52740` (law kind 4), only when `+0xc6c` bit 0 and mode byte = 8:
  - approach: kind 4 with a **3 m** stand-off (stops 3 m before the lane end);
  - when the stop line is **< 4.0 m** away (`0x012a2f6c`) and speed **< 2.0 m/s** (`0x01187ebc`): mark
    the entry served, set `+0xee1` bit 0x10, start timer `+0xfbc`;
  - hold: while less than **1200 ms** has elapsed (`0x00b52792`, via `0x00d9e250`), brake demand
    **100.0** (`0x0111645c`), cap 0; then clear the bit and drive on. The junction yield then decides.
- Shipped: 195 bit-7 lanes, all in phase lists of junctions with no light in phase 0 except 2 (junction
  162 phase 1). 58 of the 83 lightless junctions have them (**53 all-way stops**, 193 of 209 listed
  lanes); the other 25 lightless junctions are uncontrolled (yield only). Unlisted lanes are always 1.
- Segment states 0, 4–15 are never written; state 1 never stops a car.

**(h) Pedestrians and crossings — FINAL (code), HIGH (geometry).**
- A crossing is closed when its light's vehicle state is 2, 3 or 5, or its pedestrian state is 2
  (`0x00bbf0e0`, sole caller `0x00a1bd01`). The controller gives **walk only during the 7 s green** of the
  light's phase: amber leaves the pedestrian byte at 1 but the vehicle byte 2 closes it; there is no
  separate pedestrian phase and no all-red walk. States 4 and 5 are visual slots (`0x00a6efd0`, 5 effect
  handles) the controller never writes.
- Peds wait at the curb and finish a started crossing (§20.6.9(c)). 818 of 876 lit crossings run
  parallel (≤ 20°) to the green lanes of their light's phase, so peds walk beside the released flow;
  319 crossings have no light and are always open.
- Cars have no crosswalk rule; a ped in the lane is an obstacle of the probe (§20.6.7(a)).

**(i) Overrides and queries — FINAL (code), GUESS (callers' purpose).**
- Hold `0x00a6a170(junction)`: current phase lights 3/2, its lanes 3, timer disarmed (−1) → all-red
  until released. Release `0x00a6a240`: arm 0, tick at once (switch to the next phase). A lightless
  junction released this way is not ticked, so its phase-0 lanes would stay 3 (FINAL, latent bug).
- The only holder is the flight-AI module (vehicle AI record `+0x100`, §22.8), `0x00b3cb90` called
  from `0x00b3d62f`: when the craft is within 25 m of its target point `+0x244` (squared 625,
  `0x01117c98`; |Δy| ≤ 25, `0x01115238`), it holds the nearest junction within **15 m** of that point
  (`0x00b34b10`, `0x00a6a260`; junction kept at record `+0x4ac`, record `+1` bit 0x40). Released by the exits of
  flight-AI modes 6 and 11 (`0x00b3cae0`, `0x00b3cb70`), `0x00b39f18` and `0x00abb555`. GUESS: a
  helicopter or plane setting down at a street junction.
- `0x00a6a2f0(pos, rMin, rMax, &out, needLanes, needByte0f)`: uniform pick among ≤ 40 junctions with
  rMin ≤ distance ≤ rMax. The flight AI uses 10–50 m with the `+0x0f` filter (`0x00b3e31e`), which
  names junction `+0x0f` as "eligible for that pick" (18 lit junctions have 0); `0x00694ce4` uses it
  unfiltered. `0x00a6a260(pos, r)` = nearest junction within r (`0x00624718` uses 50 m).

**(j) Port recipe.**
1. Load junctions, phases, both lists and lights from the fawld Traffic section; at world start run
   (e) exactly (phase 0, deadline = now + phase-0 duration, lightless phases released).
2. Each frame run the cursor walk of (c) over all junctions, ticking (b); gate by "first light of the
   current phase is in a resident tile" and skip junctions with no light in the current phase.
3. Expose lane state 1/2/3 on the approach segments. In the mode-8 car combiner add the signal source
   ((f), kind 2, 5 m) and the stop-line source ((g), kind 4, 3 m, 1.2 s hold), both behind the obey bit.
4. Close a crossing on vehicle state ≠ 1 or pedestrian state 2.
5. Provide hold/release for scripted flight AI; do not add an all-red gap, randomness or a ped phase.

**(k) Test vectors.** Car = `car_4dr_standard03`, a_b = 12.06, a_lim = 10 (§20.6.12); t from world
start, ignoring the cursor lag of (c).

| # | Case | Expected |
|---|---|---|
| 1 | Junction 2, tile 1215, (−222, 26, −516), t = 0–6,999 ms | lanes 218, 54 = 1; 192, 191 = 3; lights 936–939 (1, 1); 954 (3, 2) |
| 2 | same, t = 7,000–9,999 | 218, 54 = 2; lights 936–939 vehicle 2, pedestrian 1; crossing 10 (light 954) closed |
| 3 | same, first visit at t ≥ 10,000 | 218, 54 = 3; 192, 191 = 1; light 954 (1, 1), crossing 10 open until t = 17,000; next switch at 20,000 |
| 4 | v = 10 m/s, lane state 2 or 3, d = 4.0 m | 100/8 = 12.5 > 12.06 → drive through |
| 5 | v = 10, d = 4.2 | 11.90 ≤ 12.06 → stop (full braking, overshoots the 5 m line, stops before the node) |
| 6 | v = 10, d = 11 (dEff 6) | demand 8.33 > 7.5 → brake 0.691, cap 9.49 m/s |
| 7 | v = 15, d = 9 / 10 | 12.5 → drive through / 11.25 → stop |
| 8 | Junction 21, tile 1217, (−382, 14, −1023.3), lane 730 (flags `0x481`) | state 1 for ever; car stops 3 m before the end, holds 1,200 ms, then yields per §20.6.7(c) |
| 9 | Junction 187 (15,000 / 10,000) | phase 0 lanes: 12 s green, 3 s amber, 10 s red; phase 1: 7 / 3 / 15; cycle 25 s |
| 10 | Cursor: 5 resident junctions A–E, B and D in amber | frame 1 ticks A, B; frame 2 C, D; frame 3 E; frame 4 A, B → each visited every 3 frames |

Residual: whether the clock `0x01320d94` stops in pause menus; whether `0x00861e60` turns false as soon
as a light's tile streams out; the purpose of flight-AI modes 6/11 and of the callers `0x00694ce4` and
`0x00624718`; why lit junction 162 carries a 0 ms phase.

Scripts (read-only, deletable): `tools/scratchpad/tlight_stats.py` … `tlight_stats4.py`, `tlight_xref.py`,
`tlight_scan.py`, `tlight_scan2.py`, `tlight_rd.py`, `tlight_tbl.py`, `tlight_str.py`; listing in
`tools/scratchpad/tlight_out/`.

---

## 21. Combat and NPC behaviour, end to end (2026-10-15)

**Purpose.** One port-ready reference that strings the earlier sections together into the behaviour a
mission sees: how a human NPC decides, picks a target, takes cover, flees, and obeys a script order. It
does not repeat what is FINAL elsewhere; it cites it and fills the gaps. Everything below was read from
the executable (`SaintsRowTheThird.exe`, base `0x400000`) with `capstone`, or from the shipped tables
extracted with `tools/scratchpad/extract_one_xtbl.py` into `tools/scratchpad/combat_xtbl/`
(`ai_behavior.xtbl`, `ai_goals.xtbl`, `combat_actions.xtbl`, `ai_personalities.xtbl`). Scripts for this
section: `combat_scan.py`, `combat_evtab.py`, `combat_consts.py`, `combat_beh.py`. Status tags: **FINAL** =
read end to end; **PARTIAL** = the named part is OPEN.

**Vocabulary bridge.** The engine has no mode called "alerted", "attack" or "surrender". The words map to
the 44-row mode table (§6) and the goal layer (§9) as follows:

| Word | Engine form |
|---|---|
| idle | mode 0 `Idle`, goal 16 `idle`, action 69 `idle` |
| alerted (armed gang/cop) | still `Idle` or `Combat Movement`; the "alert" is a list-0 entry with flag `0x8` (§10.3) plus goal 14 `acquire target` |
| alerted (civilian) | mode 17 `Gunfire wary` (§20.4) |
| attack | modes 5 `Combat Movement`, 12 `Fire spray`, 15 `Grenade throw`, 23 `Melee`, 3 `Brute Melee`, 6 `Commando` |
| cover | modes 8 `Cover stay` and 7 `Cover popout` |
| flee / cower | modes 13 `Flee` and 9 `Cower` (no autonomous AI path ever requests 24 `Panic`; §20.4) |
| surrender | **no such state exists in the decision layer.** Closest forms: 19 `Holdup`, 10 `Dbno`, 20 `Hostage`. Their roles are PARTIAL (21.4) |
| stunned | mode 34 `Stunned`; forced from outside (damage/reaction), never chosen by a goal (§9.5) |

### 21.1 The per-frame decision loop for a human NPC — FINAL for structure, PARTIAL for two routines

**Frame order** (§1.4, restated): event delivery `0x004f5ae0`; perception tick `0x00507520`; event pump
`0x004f5b90`; decision step `0x004f5210`; apply `0x004e8310` (mode switch, same frame); mode tick (slot
`+0x0c`, skipped while `+0x44c` bit 0 "finished" is set).

**Decision step** (§9.2, FINAL). Gate `0x004f5180` first (refuses on stunned-idle, `always_cower`, a pending
`Stunned`/`Panic` request, down/dead, held-as-shield, player class, free-fall). It passes only when
`+0x44c` bit 0 (action finished) or bit 1 (re-evaluate request, raised by mode code through `0x004f28a0`)
is set. A running action is not re-evaluated unless its mode raises bit 1. A real action cannot be
displaced before its `No_interrupt_ms` (timer `+0x438`) has run out. The chooser `0x004f3750` then walks
goals 0..16 in fixed code order (§9.4), and inside a goal the action list in data order; **there is no
scoring**. The first action that passes all of: not already tried this pass; ability enabled in the
behaviour record; precondition mask (`on foot`, `in vehicle`, `in water`, `must have target`, `can do in
cover`); per-action repeat timer `+0x320+4a` expired; and its routine returned true — wins.

**What the shipped data makes of that** (`ai_goals.xtbl`, 17 goals, order lists as extracted; the full lists
are in `combat_xtbl/ai_goals.xtbl`). Goal test, in code order, with the record fields it reads:

| Goal | Test (short) | Action list head (data) |
|---|---|---|
| 0 change mode | always | roller blader change, zombie explode, move to post combat |
| 3 survive | health / max health < `Survive/low_health_pct` | zombie eat, cover fire, cover stay down, cover take def |
| 4 retreat | hostile closer than own weapon range value a | move to navmesh, cover take def, move retreat |
| 6 suppress | event kind 108 queued and cover-mode state | cover popout, fire sweep, fire suppress, cover fire, fire, throw grenade |
| 7 cover | see 21.3 | human shield grab, cover fire, cover stay down, cover take def |
| 9 advance | target known, not reloading, not too close to own cover | move to navmesh, move to post combat, ..., cover advance, move advance |
| 10 reload | clip fraction at or below `Reload/clip_pct` | human shield grab, melee, cover stay down, cover take def, reload |
| 11 kill enemy | target seen within `Kill_Enemy/seen_in_last` | quick kill ... cover popout, throw grenade, fire sweep, move close in, fire, fire suppress ... |
| 14 acquire target | no target, or its entry flagged lost; head entry hostile and seen within `Acquire_Target/seen_in_last` | move to navmesh, cover popout, pickup weapon, move to post combat, move sidestep, cover take off, follow acquire, move surround, move chase, move advance, taunt |
| 15 investigate | candidate exists, seen within `Investigate/seen_in_last`, nothing in view | move to navmesh, move to post combat, investigate peek/move/look, move surround, move advance |
| 16 idle | list 0 empty or head older than `Investigate/seen_in_last` | follow..., move to navmesh, follow browse, ..., idle |

**Time scales** (FINAL, `ai_behavior.xtbl` seconds are stored x1000 as ms, §10 of `spec-tables-traffic-ai.md`).
`Gang Average` (the common armed-gang behaviour; applies to `smg`/`rifle`/`shotgun`/`pistol` for Deckers,
Luchadores, Morningstar): `Kill_Enemy` 4 s, `Flush_Out` 5 s, `Acquire_Target` 10 s, `Investigate` 20 s
(already monotone, so no loader clamp), `low_health_pct` 0.4, `Reload/clip_pct` 0.2, dodge 80, can back-step
fire and fire while walking true. Cover block: `stay_down_time` 3 s, `pop_up_time` 5 s,
`find_new_cover_time` 15 s, `abandon_cover_time` 20 s, `advance_cover_time` 4 s, `squad_cover_pct` 60,
`take_cover_when` = {shot at, grenade noticed, damaged}. **Note:** the file also carries a second,
top-level copy of `stay_down_time` (3), `pop_up_time` (3), `find_new_cover_time` (10) and `squad_cover_pct`
(50); the loader reads only the `Cover/...` children (HIGH), so the top-level copies are dead data.
`Civilian Unarmed` (melee-weapon civilians): `Kill_Enemy` 2 s, `Acquire_Target` 15 s, `low_health_pct` 0.4,
abilities melee (repeat 2-4 s) and move rush (6-8 s); no cover block, so the loader defaults apply
(abandon 30 s, find-new 30 s, advance 30 s, stay-down 1 s, pop-up 30 s, `squad_cover_pct` 100).

**Action table timing** (`combat_actions.xtbl`, FINAL for values; units ms): `No_interrupt_ms` is 250 for
most actions, 500 for `cover fire`/`cover popout`/`cover stay down`/`cover take off`/`cover advance`,
750 for `move sidestep`, 1000 for `cover take def`, `hold position`, `move advance`, `move chase`,
`move retreat`, `move regroup`, `move surround`, `move to post combat`, and 0 for `idle`. `Min_repeat_time`
is non-zero only for `move to navmesh` (2000), `pickup weapon` (1000) and `cover popout` (2000).
`Auto_abort_ms` and `refresh_desire_interval` are loaded but have no reader (§9.7), so a port should ignore them.
Finishing an action rolls a cool-down between the ability's `Repeat_min` and `Repeat_max` and can only
lengthen the per-action timer (§4.1).

**Perception thresholds that start the loop** (FINAL, §10, §12): default view cone 45 m x 150 degrees x
80 degrees (35 m low variant is unused); spotted cone 90 m x 190 x 140; in cover 50 m x 220 x 180; zombie
20 m x 80 x 80. A target within the 25.0 proximity test is noticed without sight. The sense pass runs every
650-800 ms (350-450 ms for a non-civilian below activity level 6); line-of-sight is asked asynchronously with a
re-check of 700-4000 ms (+500 ms after a visible answer). Seen entries expire from list 1 after 40 s; list 16
(damage) lives 30 s.

**What is NOT an input (FINAL, negative).** Notoriety does not enter the NPC decision loop, the gate, any
goal test, perception, or the attitude function. Hostility is the team matrix `0x009b7100`
(`byte[0x0262d940 + a*8 + b]`; 9 gives 2; 0 hostile, 1 unfriendly, 4 allied) plus force flags
(`attack_player_on_sight`, `attack_non_player_enemies_on_sight`, `attack_pedestrians_on_sight`,
`dont_react_to_player`, `ignore_attacks`) and the provocation meter. Notoriety (19) only drives who is
*spawned* (police, roadblocks, §19.8). The force flag `ignore_notoriety` (`+0x2bc` bit 2) is read by the
notoriety code, not by AI. Ammo enters only through goal 10 `reload` (clip fraction) and the `reload`
action; there is no "out of ammo" retreat transition.

**PARTIAL.** The routines of the 70 actions are read only in the families of §11 (11.3); the exact first
action for goal 14 depends on `move to navmesh` (id 25), which is not read. The goal tests `suppress`
(`0x00540a70` cover-point test) and `advance` step 6 (`0x004e68e0`) are not read.
**[2026-10-15 closed: action 25 `move to navmesh` (`0x00538270`) is a four-instruction gate: it returns
bit 7 of human `+0xc38`. That bit is the "off the navmesh polygon" flag, rewritten once per call of
`0x00a124f0` (callers `0x005460e9`, `0x00950a93`, `0x00993a4b`) from `0x00a122b0`, which clears it while the
cached polygon still contains the position or a re-locate succeeds, and sets it only when both fail.
FINAL: an NPC standing on the mesh refuses action 25, so it never wins for a normal spawn. The goal 14 /
15 / advance lists then fall to their next entries (see TV1). `0x00540a70(human, snap, radius_vec)` is
FINAL as: true iff the human owns a cover object (`0x004e5e60`, node owner handle `+0x70/+0x74` equals the
human's handle) **and** the squared distance from the human (optionally snapped through `0x00992800` when
`snap` is set) to the node point (`+0x3c`, or a default point when node `+0x8d` bit 3 is clear) is at most
the squared radius held in the 16-byte vector at the third argument (`0x024fb1d0`, zero in the image, so
filled at run time; the take-def radius `0x024fb1c0` is the same kind). `0x004e68e0(human, handle, flag,
x)` is PARTIAL: it looks the handle up with `0x004fe390`, returns true (1) when unknown, and for a
vehicle/on-foot split compares positions in a box `0x01115d20` or defers to `0x004df080`; its result
feeds advance step 6, role not decoded further.]**
**[2026-10-15 closed: FINAL for advance step 6. `0x004e68e0(human, handle, melee_flag, out_point)` returns 1
when the human is not a live human-typed object or the handle has no actively known perception entry
(`0x004fe390(+0x510, &entry, handle, 0, 1)`). When the human is down or dead (`0x0096f540`) or in state
`0x009a1dc0`, it also returns 1 unless the entry position lies within 2.0 of the human on every axis (box
`0x01115d20` = 2.0). Otherwise it returns the weapon-band test `0x004df080(human, &human_pos, &entry_pos,
melee_flag, out_point)`: with a = minimum and b = maximum range from `0x004dd530` and d2 the squared
distance (`0x00da1330`), it returns -1 when d2 < a^2 (compare at `0x004df12a`; out = entry_pos +
unit(human_pos - entry_pos) * (a + 0.1 * (b - a))), +1 when d2 > b^2 (`0x004df1b7`; out at a + 0.9 * (b - a)),
else 0 (out = human_pos). So step 6 ("must return exactly 1") means: the target is beyond the maximum weapon
range b, or not known. This matches the §11 `0x004e68e0` bullet, which already resolved it. Consequence for
22.2: the chase sub-state 0x10 exit also fires when the target is merely beyond the driver's range b. Port
rule: `band(h, T) = !known(T) ? 1 : (downed_or_9a1dc0(h) && !within_axis_box(h, P, 2.0)) ? 1 : d2 < a*a ? -1
: d2 > b*b ? 1 : 0`; `advance` needs `band == 1`.]**

### 21.2 Target selection — PARTIAL (no scoring chooser exists)

FINAL, negative: **there is no function that ranks candidate targets.** Specifically:
- The target handle is `+0x1330/+0x1334`. Its only plain stores in the executable are inside the
  set-target routine `0x009659f0(human, target_or_null)` (stores at `0x00965a96`/`0x00965aa6`), which also
  arms timer `+0x13a8`, draws a weapon (`0x00600720` with 13,1,0,0 on `+0x1b78`) and, for a human-typed
  human, registers with perception. Called from ~50 sites, all in mode code, event handlers or the network
  reader.
- The perception target-refresh pass `0x00506660` (every 300-500 ms) only **re-validates** the current
  target through `0x004fc100` (still a list-0 entry with flag `0x8`, score below the dismissed sentinel
  `0x0137fa90`). A failed check clears the handle and posts event 78 (§12.5).
- The default event handler `0x004ebc70` (jump table at `0x004ec3f4`, byte index at `0x004ec454`) writes
  the target in two places: **kind 77** clears it (`0x009659f0(human, null)` then requests `Idle`);
  **kind 78** resolves the event's source handle and sets it as the target, unless the human is a civilian
  (team byte 7) and `0x004eb920` refuses; then runs the `Gunfire wary` pre-hook `0x004e8070`.
  Kind 3 (`0x004ebf5f`) calls `0x004dedb0(human, entity, 4 flag bits)`, which also sets awareness directly
  (`0x005023f0`, maximum score). It is skipped while `+0x76f` bit 1 is set.
- A follower copies its player's target every 2000 ms (the AI update, `0x004e8e98`): the player's
  `+0x1e30` target is set as the follower's when `0x009ba7e0` accepts it.

**What picks "the threat" in practice** (HIGH, from the goal tests, §9.6/9.8): the goals do not use a
chosen target alone; they read **perception list 0**, the known hostiles, whose head entry is the most
recently created sensed hostile. Candidates are therefore: every other live human the sense pass notices
(attitude 0, or the human's own target); score is not used for ordering; "player bias" is only the
`always_sees_player` flag (re-notices every player each pass), the `never_turn_on_player` flag, and the
provocation meter gate in the notice routine (a player at attitude 4+ is noticed only above the meter
threshold). Visibility: the view cone plus the asynchronous line-of-sight answer. Distance: only the
25.0 proximity notice and the ranged-weapon limits (second weapon range value b in `cover`/`advance`,
`retreat` uses a). Retarget interval: the 300-500 ms refresh for validation; a new sighting is created at
the sense pass (650-800 ms) subject to the LOS re-check interval above. **Open:** the writer that converts
a list-0 entry into `+0x1330` for an unprovoked gang NPC first seeing the player is not isolated (event
kinds 14/38/48 producers are also open, §20.4). Port rule: on the first list-0 hostile entry, post kind 78
to the NPC with that entity and let the default handler set the target.
**[2026-10-15 closed (static, HIGH; negative result, not FINAL for runtime): no code converts a list-0 entry
into `+0x1330`.** Evidence, all by exhaustive caller scan of the exe (`combat2_dis.py callers`,
`combat2_k78.py`): (1) the ~60 call sites of `0x009659f0` contain **none** in the sense, see, notice,
dismiss or refresh code (`0x00501xxx`-`0x00507xxx`); the perception code never calls the set-target
routine. (2) Event 78 has exactly six producers (a scan for a push of kind `0x4e` ahead of the three post
helpers `0x004f6010`, `0x004f6420`, `0x004f5f80`): the target-refresh pass `0x00506660` (post at `0x00506956`)
and the standalone validator `0x004fd7e0` (post at `0x004fd831`, called from `0x00505107`), both of which
pass the **null handle constant `0x01117488`** (`{0,0}`, never written; checked) as the lookup key to
`0x004fc100`, so the result is always null and the posted source is null; `0x004e53a0(victim, attacker)`
(posts 78 with the attacker as source, to the victim, after a random delay; callers only the streaking
bystander code `0x00536fbc` and the activity near-miss scan `0x006b9522`); the kind-77 case inside the default
handler (`0x004ebe87`); the passenger event `0x0051f737`; the vehicle-threat event `0x00524559`. So the
only 78 with a real, non-null source for an on-foot NPC comes from `0x004e53a0` (provocation by a
bystander activity). (3) In the default handler kind 78 does nothing for a null source (the resolver
`0x004d7f30` returns zero and the case jumps to the default return), while the mode event handlers of
Combat Movement, Cover stay/popout, Fire spray, Investigate, Melee and Taunt answer kind 78 with
`0x009659f0(human, 0x004e6bb0(human, null-handle global))`, i.e. they **clear** the target. (4) The
real non-null writers are: the damage/notice path (`0x004dedb0` then kind-3 default case), follower copy
(every 2000 ms, `0x004e8e98`), human-shield grab (`0x0050e735`), grenade throw tick (`0x0054ff18`),
mode events that carry a source (Commando `0x0052cdfd`, Riot cop `0x00530206`/`0x00530590`, Rollerblader
`0x0053227b`, Dog `0x0052ec6c`, Pickup item `0x004f078a`), script/mission code (`0x00730316`, the
spawner `0x006745ea` which targets the nearer of two players), and the player auto-aim
(`0x00950db0`). **Consequence for the port: do not model first sight as "post 78"; static evidence says
the unprovoked gang NPC's `+0x1330` stays empty until something above fires, and the goals that need a
target (`must have target` = `0x004e4e50` reads `+0x1330`) cannot pass until then.** What turns a
sighting into a target for an unprovoked NPC (probably the kind-3 default case through awareness
`0x005023f0` plus a mode event; not shown) is therefore still unproven; a live hook on `0x009659f0`
(arguments and return address) during a first sighting is the one test that settles it. The null-probe
reading of `0x00506660`/`0x004fd7e0` also means the "re-validation" of §12.5 never passes statically; its
runtime effect (clearing every 300-500 ms or not) needs the same live check.]**

### 21.3 Cover — FINAL for search and timers, PARTIAL for the cost weights

**Entry.** Goal 7 `cover` (21.1 table) passes on the ordered rules of §9.6 (target T seen within
`Flush_Out`; in-cover range check; `+0x76f` bit 3 with list 3 non-empty; stay-down timer test; **no cover
object and T holds a ranged weapon seen within `Flush_Out`: true at once**; then `take_cover_when` bits
`damaged` (accumulator > 10% of max health over 30 s), `shot at` (list 3 non-empty), `aimed at`). Its
actions: `cover take def` (gate `0x00540b90`: refused while stay-down `+0x968` is armed and less than
`pop_up_time` has elapsed on it; accepted once find-new `+0x964` has passed, or with no cover object, or
when not yet within radius `0x024fb1c0` of the cover point). For a freshly spawned NPC both timers are -1
and the cover handle (`+0x2b0`) is empty, so the gate is true. Mode `Cover stay` enter `0x00543480` runs
`0x00542bc0` then `0x005429b0` (scripted override first, else search) then the search below.

**Search `0x0050a690(human, radius)`** (FINAL, §15.7; 1,282 instructions): AABB around the human with
half-width 14.0 (15.0 for a brute) and fixed vertical half-height 4.0, clipped to the human's post area, widened
toward the group leader for followers; gather by linear scan of every live `cover_node` (`0x008cd960`),
keeping those inside the box and unclaimed; reject nodes in the human's failure list (perception list 20),
not "open", or refused by the evaluator `0x00509e50` (hard gate: if any perceived threat that resolves to
the local player is not shielded by the node, the node is disqualified); score = arc/exposure penalty +
path distance terms + group cohesion penalty, clamped to [0,100] and quantised; the multi-destination
query `0x00a12a80` picks the cheapest reachable one; claim by compare-and-set of the node's owner handle
(`+0x70/+0x74`); on contention or total failure all survivors go on the failure list and the caller backs off
a random 6-8 s. **Arc test** `0x008cd1d0(node, threat, pos)`: bearing from node to threat wrapped against the
node's yaw must lie inside `cover_angle_left/right` (bytes dequantised as `(b/255)*2*pi - pi`), and the
same test from a point 0.45 behind the node against `cover_def_angle_left/right`. Aerial-vehicle threats skip
it. **PARTIAL:** the exact weights in the cost sum were not extracted (only their sign and clamp).
**[2026-10-15 closed: FINAL for the evaluator and the threat term; HIGH for the geometry refinements.
Evaluator `0x00509e50(human, node, &any_primary, &count)` walks perception list 0 (head `+0x804`, next
`0x004fadc0`) and uses only entries with flag `0x8` whose handle resolves to a live object. For each it calls
`0x008cd1d0(node, threat, entry position)` (the stored position, not the live one): bit 0 (primary arc) is ORed
into `any_primary`; bit 1 (def arc, "shielded") is s (`0x00509f5d`). If s = 0 and the threat is the player
(`0x00853b30` type `0x21`) it returns 0 with count 0. Otherwise count += 1 and shielded += s, and when the
entry is also in list 16 (damage) or list 3 (shot at) it counts a second time (count += 1, shielded += s;
`0x00509f9c`-`0x00509fc2`). It returns shielded. Caller `0x0050a690`, after the post-area, failure-list 20 and
open-flag filters: for current action 8 `cover take def` without a forced point, a node with shielded = 0 is
rejected (`0x0050b011`), so with no hostile in list 0 no node passes take-def. Threat term (`0x0050b019`):
`cost = (any_primary ? 0 : wA) + (count - shielded) * wB`, with wA = 8.0, wB = 7.0 when personality
`combat_aggression` (record `+4`) is 2, else wA = 5.0, wB = 10.0 (`0x01117a54`, `0x01117a50`, `0x01187ecc`,
`0x01118d20`). Refinements added after it (branches read; their roles HIGH, detail in
`scratchpad/covernode_selection_2026-10-08.md` §2.3): with a forced point, +20 when the node's def arc does
not cover that point and + 20 * sqrt(d2) / D (0 below d2 = 16; D = 15.0, or a measured node distance when a
local flag is set); otherwise 0.75 per metre outside the weapon
band, +40 when the reachability pre-check fails, a held-cover continuity term (2.0 or 5.0), then the divide by
0.3 or 1.0, clamp to [0, 100] and the byte quantisation. Port rule: implement the evaluator and the threat
term exactly as above; take the refinements from the scratchpad list.]**

**Timers** (FINAL, §11.6). On taking cover `0x00541600` arms: find-new `+0x964` = `find_new_cover_time` x
U(0.8,1.25); stay-down `+0x968` = `stay_down_time` x U(0.8,1.25) (or 800 ms on the first entry while
`+0x315` bit 7 is clear and recent damage < 20%); time-in-cover `+0x96c` restarts at 0. Cover logic re-arms
`+0x968` to 10 s when the committed reason `+0x444` is 7. The abandon check `0x0050a000` (perception tick
step 1) drops the cover when `abandon_cover_time` is exceeded, or while the cover is
under attack (500 ms re-arm of `+0x964`/`+0x968` while `0x004dd640` holds).
**Fire cycle** (data of `Gang Average`): down for stay-down (2.4-3.75 s); once that deadline has passed the
`cover` goal turns false for `pop_up_time` (5 s; 1 s when `+0x76f` bit 3), which lets `cover popout` /
`cover fire` / `fire` win; after `pop_up_time` the goal is true again and the NPC drops back. `cover advance`
(`0x00540080`) becomes acceptable when `+0x96c` has reached `advance_cover_time` (4 s); the `advance` goal's
step 5 holds a non-aggressive NPC in cover while the target is within (2b)^2 of the node and the clock is
below that value. The in-cover range check of `cover` rule 4 uses b (second weapon range), doubled for
`combat_aggression` 2. `cover popout` (`0x00541230`) is refused while the `cover` test holds unless block bit
7 forces it, needs the node's `+0x8c` bit 1, and is the fallback when `cover fire` (`0x00540f30`, requires
`0x008cd1d0` = 0, i.e. the node does not shield the shooter) cannot accept.

### 21.4 Flee, cower, surrender and human shield

**Flee/cower chooser `0x004eaae0(human, threat, prefer-flag)`** (FINAL; reached from `0x004eaf80` for
attacked/lethal-gunfire events, and from the idle event sink `0x005113e0`; §20.4 has the front half).
1. Do nothing if a `Panic` request is pending, or if already in `Cower` with `+0x8d0` bits 4/5 set.
2. Compute `preferFlee`: true if already in `Flee`; else a uniform random in [0,1) compared with **0.5
   when currently in `Cower`**, **0.8 otherwise** (`preferFlee` = random <= constant). Then: `cant_flee`
   (`+0x2ba` bit 5) forces false; `cant_cower` (bit 3) forces true; a position test `0x005812d0(+0x40)`
   while cowering also forces false; `always_cower` skips the flee branch entirely.
3. If `preferFlee` and the flee gate `0x004d8ff0(human, threat, 2 (10 on the second variant))` holds: request mode 13
   `Flee` (storing the threat in `+0x2b0` via `0x004e3830`). The gate refuses: not human-typed, flee-from is
   self, `ignore_ai` (`+0x2bc` bit 1), special state `+0x1da7` bit 4, brute class (`0x0052a230`), already
   in a vehicle transition, `+0xec` bit `0x40000`, or no reachable flee-from reference.
4. Else, if the cower eligibility `0x004eaa40` holds (human-typed; no pending ordered action; not `cant_cower`;
   not the three special-state tests; for a follower additionally not following) and not already `Cower`:
   request mode 9 `Cower`, then speak a persona line (hash `0x1f2bb415`) if the threat is a character.
So a civilian flees about 80% of the time and cowers about 20%; a cowering civilian re-rolls at 50%.

**Flee tick `0x004da9f0`** (FINAL for the stop rule): while `+0x8d0` bit 0 is set (flee-from active) the
squared distance to the flee-from point/object (`+0x8e8` when bit 5, else the resolved handle `+0x8d8`)
is compared with the double at `0x01115340` = **900**; **beyond 30 m** the bit is cleared and the flee
destination logic stops. While the character is in state `+0xcd0` = 7 it re-targets with 0.2 (`0x01171540`).
Flee target selection (escape point) uses non-navmesh candidates in retail because the navmesh query is dead
(§14); the 6-way sub-state dispatch (`+0x500` minus 0x5b, 4 cases) is not decoded (PARTIAL).
`Cower` begin forwards to the owner machine, or refreshes the target handle if already in `Cower`
(§13.1.1). A flee order from a script (`flee`, `spec-lua-api-behaviour.md` §45.1) posts kind 0x30 with the
flag dword (2, or 3 / | 0x10) and is entered through the begin function with `+0x4a6` bit 2 selecting the
body (`0x004d91a0` for scripted, `0x004da0b0` for autonomous).

**Personality** (`ai_personalities.xtbl`, loader `0x00507AC0`): `Flee_health_pct` (`+0x05`) and
`flee_events` (`+0x06`: aimed at 1, melee hit 2, bullet heard 4, bullet hit 8, explosion heard 0x10) are
stored but their **decision consumer is not located** (PARTIAL; only the bit `0x08` "bullet hit" test
in the vehicle-occupant logic of §18.7 and §19 is known). `combat_aggression` is read only by the
cover/advance tests (`0x00507ee0`, value 2 only) and the assignment scorer `0x004f3470`.
**[2026-10-15 closed: FINAL. Record = `0x013b84d0 + 0x20 * index` (index `+0x314`, accessor `0x00507e30`;
the loader reads `Flee_tweaks/Flee_health_pct` into `+5` and `Flee_tweaks/flee_events` into `+6`). The on-foot
consumer is `0x004ead80(human, bullet)`, "flee rather than fight", called by the attacked / lethal-gunfire
handler `0x004eaf80` (`0x004eb077`, `0x004eb0cf`) and the idle gunfire sink `0x00512be0` (`0x00512c68`, bullet
= 0). Its order: (1) flee eligibility `0x004ead10` fails and `cant_cower` (`+0x2ba` bit 3) is set -> false;
(2) `+0x317` bit 6 -> true; (3) the fight-capability test `0x004e9110` fails -> true; (4) `always_cower`
(`+0x2b9` bit 7) -> true; (5) `Flee_health_pct` (integer 0-100) > health fraction `+0x1cb8 / +0x1cac` (0..1,
`0x004d31f0`; compare `0x004eaded`) -> true; (6) `flee_events` bit `0x10` and perception list 15 non-empty
-> true; (7) else `flee_events` bit `0x08` (bullet hit) when `bullet` is set, else bit `0x02` (melee hit).
Step 5 compares a percent with a fraction, so a record with `Flee_health_pct` >= 1 passes at any health.
Shipped `Flee_tweaks` values: `civilian cowardly` 80, `civilian normal` 50, `Genki` 50, `civilian tough` 20,
`activity hitman target` 20, `activity ho` 20: these always flee when attacked; every gang and police record
has 0 and no flags, so this test never makes them flee. In `0x004eaf80`: attacker farther than 20 m (squared
400) on a qualifying human -> event 67 after 250-750 ms carrying the result (TV4); otherwise true ->
`0x004eaae0` flee/cower chooser (skipped during a vehicle enter/exit), false -> the attacker is noticed at
maximum awareness (`0x005023f0`) and the human fights. In `0x00512be0` true (or `+0x2b8` bit 4) selects
`0x00512000` instead of `0x004e8070`. Other bits: `0x04` (bullet heard) is read only by the gunfire-hearing
routine `0x005046e0` (`0x00504783`); `0x08` also by the vehicle code at `0x00b34014`; `0x01` (aimed at) has no
reader. Port rule: `flee = (!eligible && cant_cower) ? false : flag317_6 || !can_fight || always_cower ||
pct > health_frac || (ev & 0x10 && list15) || (bullet ? ev & 0x08 : ev & 0x02)`; keep `pct > health_frac`
(percent against fraction) to match retail.]**

**Surrender / hands up** — PARTIAL. No path in the decision layer or in the 70 actions produces a
"surrender". Modes 19 `Holdup` (`0x004dc530` enter, tick `0x004dc610`, event `0x004dca40`, shared net
write `0x00523ca0` with `Cower`) and 10 `Dbno` (down but not out) exist and are in the "ignored by apply"
group (§6), so they are entered only by direct scripted/begin calls. I did not decode `Holdup`'s tick; port
it as a scripted-only state.
**[2026-10-15 closed: FINAL for `Holdup` (19), HIGH for `Dbno` (10) past its entry. Correction: `Holdup` is
not scripted-only; it is the **mugging** reaction, data `mugging_diversion.xtbl` (loader `0x006b7900`,
singleton `0x012f04c0`): Mugging_Start_Time 0.2 s, Mugging_Complete_Time 3 s, Max_Distance 10 (stored
squared), Barter_Line 0.5 s, Cower_Line 2 s, Default_Min_Cash 5, Default_Max_Cash 35.
Entry 1, the mugging manager (tick `0x006b8600`, idle `0x006b7cf0`, active `0x006b81a0`): it takes the human
the local player aims at (`0x006b5820`) when `0x006b7ba0` holds: the player is aiming (`0x009da830`), the human
passes `0x00943ea0`, `0x007044f0` is non-null and `0x007c3420` is 0, neither is entering or leaving a vehicle,
the human is in `Idle` (or already `Holdup` with manager state above 1), **every notoriety track 0-3 is at
level 0** (`0x006b7c62`), the squared distance is at most Max_Distance^2, and the player's weapon passes
`0x00b81440` with type `+0x4c` not 20. State 1 lasts Mugging_Start_Time. Then a human in a vehicle bails
(`0x008c0240`), a human with `+0x1e01` bit 3 cowers if eligible, else `0x004dc880(human, player, 0)` begins
`Holdup` and state 2 arms the complete, barter-line (`0x6b47cafd`) and cower-line (`0x1f2bb415`) timers. When
the complete timer expires (and `0x0070a440` is false) state 3 pays out (`0x006b7e90`): cash = round(U(min, max))
from the character's cash words (descriptor `+0xf0`, `+0x38`/`+0x3a`; the defaults replace values below 1)
through `0x00974880`; then the human is requested to `Flee` from the player and gets event 96. If the player
stops qualifying while the human is in sub-state 0x7b, the same routine runs without payout and raises the
police track (3) to 1.0 when it is 0 and `0x006063f0(3) > 0`; any other abort (`0x006b7d50`) posts event 65 to
the human after 500 ms and raises the police track to 1.0 when it is below 1. Entry 2, the hostage diversion
(`0x006b45e0`, `0x006b59b0`; singleton `0x012f02f8`) calls `0x006b4430(human, threat)`, which begins `Holdup`
only for team 7 or 9.
State machine (sub-state `+0x500`, jump table `0x004dc860` = 0x79 `0x004dc693`, 0x7a `0x004dc74f`, 0x7b
`0x004dc7a4`, 0x7c and 0x7d both `0x004dc81c`; threat handle `+0x8d8/+0x8dc`). Enter `0x004dc530` (human-typed
only): 0x79; when `+0x8d0` bit 0, event 63 after 5000 ms; reaction call `0x004e98d0`; threat put in perception
list 2. Tick `0x004dc610`: threat not a live human -> request `Idle`. 0x79: while animation states
`0x211`-`0x215` are active it calls `0x004bddb0`, else -> 0x7a. 0x7a: -> 0x7b once state `0x21e` is inactive or
`0x004bddb0` returns true. 0x7b (hands up): stays while `0x004dc350` holds (the threat aims at the human,
`0x0050cdb0`: player aim target `+0x1e30` = human, or the threat's aim line passes the human within weapon
range `+0x120`; or `0x009422b0`) or state `0x1d52` is active, and then runs a deferred re-check when `+0x8d0`
bit 1 is set; otherwise -> 0x7c, cancels queued event 64 (`0x004f6420`) and posts 64 after 300 ms. 0x7c/0x7d:
aim resumed -> 0x7a and cancel queued 65 and 64. Events (`0x004dca40`): 63 = re-check `0x004dc3d0`: every
5000 ms, with probability 0.1 start the fidget state `0x1d52` and re-check after its length + 10000 ms; defer
(set `+0x8d0` bit 1) while another animation state is current or the sub-state is 0x7d. 64 -> 0x7d, play
`0x21f`, post 65 after its length. 65 -> request `Flee` from the threat (`+0x2b0`) when `0x004ead10` holds, the
human is not a shield hostage (`0x00a00560`) and the threat is valid (`0x004dcac7`), else `Idle`. Port rule:
hands up while aimed at; 300 ms after the aim leaves, lower the hands (`0x21f`), then flee; aiming again during
the grace or the lowering puts the hands back up.
`Dbno`: begin `0x0050bd40` requires local ownership (`0x008ae480`) and downed (`0x009715d0`, `+0xcc8` = 6). No
code requests mode 10 (all `0x004e4130` and `0x004dd380` callers scanned); it is entered only through apply's
`Veh threat` -> `Dbno` fall-through (§3.2) and the network handler around `0x0050c7b9`. Enter `0x0050c2b0`:
sub-state 9, clears perception. Tick `0x0050c130`: no longer downed -> `0x004e8070(human, 0)`; while downed,
not `0x00956390`, and `0x009735d0` non-zero -> `0x0050bef0`; a brute whose state `0x1d60` reaches its tag and
whose melee routine `0x00551e20` accepts -> `0x005246a0`. `0x0050bef0` (also event 54): nearest human within
20 m (400 squared) passing `0x00950550` and `0x00973770`; a call-for-help line (one of `0x3bf0449e`,
`0x2bd444da`, `0xb0c5139a`, random start); state `0x338`; the helper noted in list 2 for that animation's
length; event 117 posted to the helper after 1000-2000 ms. HIGH: event 117 = "come and revive" (its handler
was not read).]**
**[2026-10-15 closed (second pass): PARTIAL, HIGH for the tick; the bleed-out kill is not in the Dbno mode.
Dbno (10) is a pure waiting state: it has no timer, no bleed counter and no death of its own. Per tick
`0x0050c130`: (1) `0x0095d720(human)` true -> return (nothing). (2) If the human's `+0xd24` object passes
`0x004b4ce0(tag 0x1d60)` and the brute test `0x004b4320(tag 0x1d60, -1)` and `0x0052a230` and melee `0x00551e20`
accept -> `0x005246a0` (brute melee), else return. (3) Not downed (`+0xcc8` != 6) -> request `Idle` via
`0x004e8070(human, 0)`; this is the only exit, so revive (or any cause that moves the stance word off 6, including
death) ends the mode. (4) Downed and not `0x00956390`: `0x004de1f0` (clears ordered/cover flags unless state 0x21 is
active), then if the classifier `0x009735d0` is non-zero (it returns 0 only when the human is blocked by
`0x0097e280`; otherwise 1-4 = reason class, -1 = none of those) it runs the call-for-help `0x0050bef0`, EVERY TICK
(the animation-state and helper checks inside gate the repeat; event 54 also reaches it, `0x0050c210`). Mode events
other than 54 return 1. The downed stance itself is owned by `0x00973dbb`: it picks a downed type (`+0x18e0`),
calls the type's enter hook (table `0x01309f00`, stride 0x18, hook at row+0xc), sets stance `+0xcc8` = 6 through
`0x00942110(human, 6, 0xff, 1.0)` and clears `0x00502e40`. Per-type hooks (rows 0-5; row 0 also holds the two
constants 20000 and 2500 ms at row+0 and row+4): enter `0x009720f0`, `0x009723f0`, `0x00972d30`, `0x009730d0`,
`0x00973140`, `0x009734f0`; bleed/update hooks at row+0x8..+0x14 (`0x00971fb0`, `0x00972790`, `0x009714c0`,
`0x00971650`, `0x009716e0`, `0x00973a90`, ...) are called from the stance wrappers `0x00971a80`, `0x00971ad0`,
`0x00971c60`. I did not decode them, so the per-type bleed-out timing and the revive hand-off (event 117 handler)
are OPEN. Shipped DBNO tunables (tweak_table.xtbl; these are the 16 names with no registry entry, so they are read
by name from the table, not by the image): `DBNO_bleed_out_delta_per_second` 10 (bleed-out hit points lost per
second while bleeding, gained per second while recovering), `Enemy_revive_percent_hit_points` 0.25,
`Human_revive_percent_hit_points` 1.0, `HD_revive_hold_time_sec` 2.5, getup `DBNO_getup_*` (press speed +1.5, drop
500 ms, anim speed 0.25-2.5, decay 4/s), screen tint values 0.2/0.3/0.7/0.8; `DBNO_Extra_Time` ms = 1
(spec-tables-progression.md); per-type `Bleed_Out_Hit_Points` is a u16 in the character record (+0x0C).
Port rule: while downed (stance 6) an NPC in Dbno does nothing but repeat the call-for-help (nearest human within
20 m, line, state `0x338`, event 117 to the helper after 1-2 s); leave to `Idle` the first tick the stance is not
6. Bleed-out: lose `DBNO_bleed_out_delta_per_second` (10) bleed-out HP per second from the record's
`Bleed_Out_Hit_Points`, die at 0, regain at that rate while being revived, then revive to
`Enemy_revive_percent_hit_points` x max HP (NPC) -- HIGH by name/description, not by decoded code.
Test vector: NPC downed (+0xcc8 = 6) -> mode 10 sub-state 9, call-for-help fires; set +0xcc8 = 0 -> next tick
mode becomes Idle; with Bleed_Out_Hit_Points 100 the bleed lasts 10 s.]**
**[2026-10-15 closed (third pass): PARTIAL, HIGH for the type-0 timer and the revive heal, event 117 NEGATIVE.
Correction to the pass above: the per-type table is a stride-0x18 array of six dwords starting at `0x01309f08`
(fields at +0 predicate `0x01309f08`, +4 enter `0x01309f0c`, +8 and +0xc two bool hooks `0x01309f10`/`f14`, +0x10
update with float step `0x01309f18`, +0x14 tick `0x01309f1c`; selector `0x009735d0` -> `0x00973d00` calls the
predicate, `0x00973dbb` calls the enter, then `0x00942110(human, 6, 0xff, -1.0)`). Type 0 = `0x00971fb0` /
`0x009720f0` / `0x00971650` / `0x00973a90` / none / `0x009723f0`. The words 20000 and 2500 at `0x01309f00` and
`0x01309f04` have NO code reference (dead data; not the bleed-out time) and `DBNO_bleed_out_delta_per_second` has
no string or reader in the image either (xtbl-only, unused by retail code). The real bleed-out is a deadline
timer: type-0 enter `0x009720f0` sets `human+0x18e8` = now (`0x026252d8`) + duration, duration = 30000 ms
(`0x116e240`) x `[difficulty row +0x14]` (`0x005f4a50`, rows of 40 bytes at `0x014a10b8`/`0x014a1130`), or a fixed
15000 ms when the game mode is 5 (`0x006725c0`); it also arms flags in `+0x1905` (bit 3 = no timer: set when
`0x0097e280` is non-null and the string at `0x011169a0` matches; bit 4 = marker handle `+0x18fc`; bit 7 = expiry
pending for a non-owner) and calls the stance helper `0x004e4130(human, 0x22)`. Pause/resume `0x00971ea0` (re-arms
from the saved remaining time `+0x18f8`). Type-0 tick `0x009723f0` (via `0x00971a80`): when bit 3 is clear and the
deadline has passed (`0x00d9e1e0`) and the human is locally owned (`0x008addb0`), it releases the marker, runs
`0x009723c0`, then **`0x009a7610(human, 0, 0)`** -- the revive/stand-up routine -- and a knock-down/ragdoll call
`0x009a48e0`. So type 0 does NOT kill at expiry: the downed NPC gets back up. `0x009a7610`, when
`0x009715d0` (downed) holds, clears the downed flag `+0x1c98` bit 5 and the bleed value `+0x1cb8`, then heals via
`0x0094cfc0(human, int(+0x1cac) x pct)` and requests `Idle` (`0x004e8070`); pct = `Human_revive_percent_hit_points`
(storage `0x0262d1c4`, 1.0) when byte `+0x1ca8` is 0, else `Enemy_revive_percent_hit_points` (storage `0x0262d1c0`,
0.25); registrations `0x01006860`, `0x01006880`. (So the two revive percents ARE in the image; the earlier "no
registry entry" remark covers only the other DBNO names.) Event 117 (`0x75`): posted by `0x0050bef0` through
`0x004f5f80` with a 1000-2000 ms delay; every kind switch I could find is bounded below it (`Idle`
`0x005113e0` byte table `0x00511e20` covers kinds 0-0x73, generic `0x004ebc70`, `0x005112d7`, `0x00519750` cover 1-0x72,
`0x0051f63a` 1-0x71) and no code compares a kind against 0x75, so the event falls to each default (ignored): HIGH
that NPC helpers never run a revive on event 117; the revive of a downed human is the timer expiry above or a
player action (not decoded here). Other types' kill paths (rows 1-5) were not read, so a bleed-out death for those
types stays OPEN. Port rule: on entering downed type 0 set deadline = now + 30000 x difficulty_scale (15000 in
mode 5; none when the no-timer flag holds); when it passes on the owning machine, stand the human up and heal
`int(max_hp_field) x pct` (0.25 enemy-side, 1.0 human-side), then Idle; ignore event 117.
Test vector: enemy (+0x1ca8 != 0) with +0x1cac = 200, difficulty scale 1.0, downed at t = 0 -> at t = 30000 ms it
stands, heals 50, mode Idle; same human-side (+0x1ca8 = 0) heals 200.]**
**[2026-10-16 CORRECTION (FINAL, see 21.10.1): `0x009a7610` is the downed KILL, not a revive. When downed it sets
HP `+0x1cb8` = 0, clears `+0x1c98` bit 5, unlinks the partner, releases the marker and calls the death finalizer
`0x00952760(human, 1)` (stance 5). The heal described above (`int(+0x1cac)` x pct, then `Idle`) is the body of
`0x009a78d0`, reached through the networked get-up `0x009a7c90`. So type-0 expiry is a bleed-out DEATH; a type-0
human is revived only by a player interaction or a script. The pass above and its port rule/test vector are
superseded by 21.10.1.]**

**Hostage / human shield.** Actions: `human shield grab` (id 46, routine `0x0050dfe0`) is in goals 2, 7,
9, 10, 11, 12, 14 (`kill near player`, `cover`, `advance`, `reload`, `kill enemy`, `frustrated`...) with a
`Gang Average` repeat of 10 s. Grab routine gates (FINAL structure): `0x009aa520` (can grab), not
a hostile-on-hostile case (`0x009a9610`) for non-brutes on a team, the group leader (if any) within
`seperated_distance` (`+0x1dc`, `+0x1e0` interior) squared, a threat from `0x004e6bb0(human, own target)`
that is not in a vehicle, and **seen within 10 s** (`0x2710` ms); then a position/aim check against the
threat's own target. Success writes the pair links and state (`spec-lua-api-behaviour.md` §13.14: holder
state 2 then 1, hostage `+0x18dc` bit 0). Mode 21 `Human shield` (holder; tick `0x0050ed90`, event
`0x0050ee50`, begin-group B) and mode 20 `Hostage` (hostage; tick `0x004dd050`, event is the constant-1
stub) are the two sides. Hostage tick (PARTIAL): when its sub-timer `+0xa04` expires it speaks persona
line `0x2bd444da` and re-arms `+0xa04` with a random 1500-5000 ms; if the holder is in a vehicle it
compares a distance against 15.0 (10.0 for the variant selected by `0x00a77a10`) before releasing
(unclear which release; PARTIAL). The release/end states 3-6 are in §13.14 and §18.9.
**[2026-10-15 closed: FINAL. Correction: mode 20 `Hostage` is the **vehicle** hostage (HIGH: a passenger who
stays in a car someone else drives), not the human-shield victim. Its tick reads the human's own vehicle
handle `+0x16c0/+0x16c4`; there is no holder in it. Enter `0x004dcf70` (sub-state 0x7f): vehicle = the
`+0x16d8` pair, else `+0x16c0`; plea timer `+0xa04` = 1500-5000 ms; exit timer `+0x460` = U(D, D + 100) ms with
D = tunable `RC_hostage_initial_exit_delay_ms` (`0x0135766c`) when vehicle byte `+0x1dec` is set, else
`Hostage_initial_exit_delay_ms` (`0x01357674`). Tick `0x004dd050`: no live vehicle -> request `Idle`; plea
line `0x2bd444da` each time `+0xa04` expires (re-armed 1500-5000 ms). Release: speed = round(|velocity| *
2.236936) mph (`0x00ad4160`; 0 when vehicle `+0xbd0` is not 1; scale written at `0x0100a7a0`), limit = 10.0
(`0x012e1128`) when `+0x1dec` is set, else 15.0 (`0x012e1124`). While speed >= limit or the vehicle is
airborne-class (`0x00ad4810`), `+0x460` is re-armed to `RC_hostage_general_exit_delay_ms` (`0x01357668`) or
`Hostage_general_exit_delay_ms` (`0x01357670`). When the car has stayed below the limit until `+0x460` expires,
the hostage gets out (`0x004dccd0` default exit block, then `0x00af3cd0`, call at `0x004dd1a0`). The four
delays are named tunables registered by `0x0071b530`; they sit in zero-filled data (0 in the image), and an
override source was not traced. Entry: `0x004dcd50` from passenger code: `0x0051f15c` (vehicle `+0x1dec` set,
vehicle `+0xc6c` bit `0x20` clear, human `+0x1c9c` bit 8 clear, speed > 20 mph, `0x01116990`), `0x00b29fc3`
(same flags, human in `Passenger` 25), `0x0051f71c` (a team-7 passenger). Port rule: `if (!vehicle) Idle; if
(mph(vehicle) >= (rc ? 10 : 15) || airborne(vehicle)) exit_at = now + general_delay; else if (now >= exit_at)
leave_vehicle()`. A human in mode 20 without a vehicle drops to `Idle` on its first tick, so the grab path's
victim side is not this mode's tick (the grab path itself was not re-read here).]**
**[2026-10-15 closed (second pass): FINAL for the delay values. The four settings are `Tweak_Table_Entry` rows of
`tweak_table.xtbl` (category `Entries:AI:Hostage`), registered in the tunable registry by `0x0071b530` with a zero
default and filled from the table at load, which is why the image holds 0. Shipped values:
`Hostage_general_exit_delay_ms` 2000 (`0x01357670`), `Hostage_initial_exit_delay_ms` 5000 (`0x01357674`),
`RC_hostage_general_exit_delay_ms` 4000 (`0x01357668`), `RC_hostage_initial_exit_delay_ms` 6000 (`0x0135766c`).
Description: the hostage waits this long under the exit speed limit before trying to exit (initial = first time).
Companion rows: `Hostage_exit_speed_limit` 15 and `RC_hostage_exit_speed_limit` 5 mph. CORRECTION to the
  **[2026-10-15 RC speed-limit conflict, HIGH: `RC_hostage_exit_speed_limit` is registered by the static initialiser at `0x00fec506`, which pushes the 10.0 constant `0x012e1128` as its DEFAULT, into variable `0x01357684`. `tweak_table.xtbl` then overrides it to 5 at load. So the earlier '10.0' was the registration default, not the runtime value. **Port rule:** RC hostage exit speed limit = 5 mph, normal hostage = 15 mph (table values). The direct reader of `0x01357684` was not located (it is probably read through the tweak registry), so this stays HIGH.]**
note above: the RC limit shipped in the table is 5, not the 10.0 constant at `0x012e1128`; the table row (registered
tunable) is the more likely live source, so the port should read the two `*_exit_speed_limit` rows (15 / 5) and keep
10.0 only if a decode of the compare at the release test shows it reads the constant (not re-verified here).
Port rule: exit_at = now + U(D, D+100) at entry with D = rc ? 6000 : 5000; while speed >= limit or airborne,
exit_at = now + (rc ? 4000 : 2000); leave the vehicle once now >= exit_at. Test vector: non-RC car held at 20 mph
for 10 s -> never exits; drops to 10 mph at t0 -> exits at t0 + 2000 ms; a freshly grabbed non-RC hostage already
below 15 mph exits 5000-5100 ms after entry.]**

### 21.5 How mission natives drive it — FINAL

All `ai_do_scripted_*`, `ai_suggest_action` and friends write one **staged order** into the character's
`+0x4a8` block (`0x58` bytes, template `0x012e14e0`, first byte `0x47` = none), via two setters
(`spec-lua-api-behaviour.md` §7.28):

| Setter | Used by | Effect |
|---|---|---|
| `0x004f4d00(char, action, no_id)` | `ai_do_scripted_advance` (action 18 `move advance`), `ai_do_scripted_rush` (22 `move rush`), `ai_do_scripted_take_cover` (8 `cover take def`), `ai_do_scripted_fire`, `ai_do_scripted_move` (24 `move scripted`, flags in `+0x4fd`), `character_take_human_shield_do` (46) | Commits at once. With `no_id` 0, allocates a kind-3 ticket id into `+0x4aa` (freeing any earlier kind-3 ticket of the owner) and sets `+0x4fe` bit 6. Not owned: forwards. Owned: copies block to `+0x450`; `+0x44c` = bit 3 (ordered) set, bits 0, 2, 4 cleared; restarts `+0x440` and `+0x438`; reason `+0x444` = `0x12`; current action `+0x445` = action; requests the action's category mode, ignoring refusal |
| `0x004f4b90(char, action, handle, flag, track, ...)` | `ai_suggest_action`, `force_throw_*`, `ai_set_in_scripted_cover` | Stages only. `ai_suggest_action` allocates a ticket; the AI commits it through the chooser's ordered re-assert |

**Override of the autonomous loop** (FINAL, §4.4, §9.4). While `+0x44c` bit 3 is set: the chooser's
step 3 re-issues the staged block as soon as timer `+0x440` has expired, before `ignore_ai` and before any
goal; if the staged action has a routine and `+0x44c` bit 4 is set, the routine is re-run and a refusal
re-arms `+0x440` for 250 ms and falls through to the goals. The helper `0x004f28a0` clears bit 1 while the
staged id equals the current id, so goals never pre-empt a running order. The scripted action's own
`No_interrupt_ms` still applies.

**Completion and return of control** (FINAL, §4.1): the action's mode calls the finish routine `0x004f4f50`.
It rolls the cool-down, sets `+0x44c` bit 0, restarts `+0x43c`, and **only if** bit 3 is set, the staged id
equals the finished action, and that action's category mode equals the current mode: clears bits 3 and 4,
sets bit 2, completes the ticket (`0x006f66d0(id, owner)`, sets node bit 0 and runs the callback, which is the
script wake-up), and resets `+0x4a8` to the template. The next frame the gate passes (bit 0) and the goal
layer is back in charge. `ai_scripted_action_is_complete` (`0x004f2880`) = `0x006f63c0(owner, +0x4aa) != 0`:
status 2 (no ticket) or 1 (done; ticket consumed, a second read gives 2) both read as complete; only
status 0 (pending) reads as not complete. `ai_clear_scripted_action` = cancel `0x004f4c90`: clears bits
2-4, sets bit 0 if the staged order is current, frees the ticket, clears `+0x4a6` bit 7 and `+0x4aa`.
**Failure modes to port:** a refused mode request leaves the order current with an open ticket until
cancelled; an order whose mode is replaced by a reactive mode (`Flee` from damage) is never completed by
that mode's finish. `ai_do_scripted_take_cover` returns true for any resolved NPC whose gate
`0x00540b90` passes (fresh NPC: true); false only for a name that does not resolve or an NPC inside its
stay-down window. `ai_set_in_scripted_cover` raises force flag `in_scripted_cover` (`+0x2bc` bit 0), which
sets the `can do in cover` state bit and makes goal 7 true unless `0x00540a70(human, 0, 0x024fb1d0)` says the
NPC is already at its cover point.

### 21.6 Test vectors — FINAL unless marked

All use `Gang Average`, data of 21.1, a Deckers `smg` NPC, hostile to the player, `Idle`, full health,
full clip, no cover object, no post, ability list as extracted.

**TV1: sees the player at 20 m, notoriety 2.** Notoriety is not an input; the result is identical at
notoriety 0 and 5. Player inside the default cone (45 m, 150 x 80 degrees) and not crouched-reduced.
1. t=0..0.45 s: the sense pass fires (350-450 ms interval, activity level 5 because the player is within the
   near test), runs the vision test, queues a line-of-sight request.
2. The LOS answer is "visible" (within one LOS interval: 0.7-4.0 s worst case): `0x005013b0` creates the list-0
   entry with flags `0x558` (`0x8|0x10|0x40|0x100|0x400`) and a list-1 entry.
3. Goal 14 `acquire target` is the first goal that passes with no target (goal 11 needs a target, goals 2-10
   fail on their own conditions). Its first enabled action whose routine accepts is chosen (PARTIAL: `move to
   navmesh`, id 25, unread; `pickup weapon` only if a weapon is near; `follow acquire` needs an ally in combat).
   Mode `Combat Movement` (5) is entered; commit sets reason 14, action id, `+0x438` restarted.
   **[2026-10-15 closed (HIGH): action 25 refuses an NPC that is on the navmesh (see §21.1 note). With no target,
   `must have target` (reads `+0x1330`, `0x004e4e50`) removes cover popout, cover take off, follow acquire,
   move surround, move chase and move advance from goal 14, leaving in data order: pickup weapon (only if a
   weapon candidate is near), move to post combat (`0x0053b080`; refuses a free NPC, it needs a leash radius
   `+0x2f0` of at least 0 and refuses when the group leader is the player), move sidestep (`0x0053e870`, not
   read) and taunt (`0x00552c30`, mode 35, which accepts on foot, in a vehicle or in water). So the first action
   is `move sidestep` if its routine accepts, else `taunt` **[2026-10-15 read: `move sidestep` `0x0053e870` first
   calls `0x004e4e50(human)`, which returns null when `+0x2ba` bit 2 is set or the target pair
   `+0x1330/+0x1334` is empty, and then fails (`0x0053f4ec` returns false). So **sidestep REFUSES a target-less
   NPC** (FINAL). `taunt` `0x00552c30` also loads `+0x1330` and validates it through `0x004e4ce0`, so it needs a target
   too (HIGH). Net: an unprovoked, target-less armed NPC has no viable goal-14 action except `pickup weapon`. It
   stays idle until one of the §21.2 target writers fires (damage, script, spawner, auto-aim).]**; this stays HIGH not FINAL because of those two
   unread routines and because the target is empty (§21.2 note), which is why a port should give the NPC
   its target at first sighting through its own rule.]**
4. Event 78 (target set) is delivered; the NPC's target becomes the player (21.2) and a weapon is drawn
   (`0x00600720(13,1,0,0)`).
5. Next re-evaluation (`No_interrupt_ms` 250-1000 ms after commit): the target is seen within 4 s, so goal 11
   `kill enemy` is eligible. Armed player seen within 5 s and no cover object: goal 7 `cover` is true (rule 7)
   and sits before goal 11, so the first accepted action is `cover take def` (gate true, no cover object)
   or, if cover search fails and backs off 6-8 s, goal 9/11 actions (`move advance`, `fire`).
6. Mode sequence: `Idle` -> `Combat Movement` (or directly `Cover stay` 8) -> `Cover stay` -> `Cover popout`
   / `Fire spray` cycles. Timer values: see TV2.

**TV2: cover timers.** Cover claimed at t0 with `Gang Average`: stay-down expires at t0+2.4..3.75 s
(800 ms on the first entry if undamaged and `+0x315` bit 7 clear); pop-up window 5.0 s after that (1 s when
being shot at, i.e. `+0x76f` bit 3 and list 3 non-empty); `cover advance` eligible from t0+4 s; find-new
cover eligible from t0+12.0..18.75 s; cover dropped by the abandon check at about 20 s of attack.

**TV3: cover search geometry.** NPC at (0,0,0), brute false. Search box x,z in [-14,14], y in [-4,4]. Nodes
at (10,0,10) (inside; euclid 14.1), (13,0,13) (inside the box, euclid 18.4), (15,0,0) (outside), (5,5,0)
(outside by height), a node already claimed by another NPC (rejected), a node in failure list 20 (rejected).
Survivors are scored and the cheapest reachable chosen; the claim is a compare-and-set. If no survivors the
caller retries after a 6-8 s back-off. (Cost values: PARTIAL.)

**TV4: civilian hears lethal gunfire** (team byte 7, `Idle`). Event 76 -> `0x00512680` -> `0x005113e0`
-> `0x004eaf80` (lethal) -> `0x004eaae0`: 80% `Flee` (mode 13, threat stored), 20% `Cower` (mode 9). While in
`Cower` a new lethal event re-rolls at 50%. `Flee` ends when the distance to the flee-from point exceeds 30 m
(squared 900). Non-lethal distant gunfire only speaks a line and posts a 250-750 ms delayed re-check (kind 67).
A civilian with `cant_flee` always cowers; with `cant_cower` always flees.

**TV5: script drives an NPC.** `ai_do_scripted_take_cover("npc")` on a fresh NPC: gate `0x00540b90` true ->
stage action 8 -> `0x004f4d00(npc, 8, 0)` -> ticket id `1..0xfff | (peer << 12)` in `+0x4aa`,
`+0x44c` = ordered, `+0x444` = `0x12`, `+0x445` = 8 -> request `Cover stay`. `ai_scripted_action_is_complete`
returns false (pending) until the cover mode finishes (then true, ticket consumed). A second call before
completion frees the earlier kind-3 ticket. `ai_clear_scripted_action` returns control at once.

### 21.7 Open items for the next pass

1. The writer path from the first list-0 hostile to the target handle for an unprovoked NPC (21.2).
   **[2026-10-15: narrowed, see the 21.2 note. All six event-78 producers and all ~60 set-target callers
   enumerated; none is driven by a sighting. Needs one live hook on `0x009659f0`.]**
2. Action routines 25 `move to navmesh` and 33 `follow acquire` for the exact first action of TV1.
   **[2026-10-15: 25 closed (on-navmesh gate on `+0xc38` bit 7); 33 not needed (needs a target); TV1's first
   action is now `move sidestep` or `taunt`, see TV1 step 3. `move sidestep` `0x0053e870` and `taunt`
   `0x00552c30` routines remain unread.]**
3. Cover cost weights inside `0x0050a690` (21.3).
   **[2026-10-15 closed: FINAL for evaluator `0x00509e50` and the threat term, HIGH for the refinements; see the
   21.3 note.]**
4. Consumer of personality `Flee_health_pct` / `flee_events` for on-foot NPCs (21.4).
   **[2026-10-15 closed: FINAL, `0x004ead80` "flee rather than fight"; percent-vs-fraction health compare; see the
   21.4 Personality note.]**
5. `Holdup` (19) tick and its entry condition; `Hostage` (20) release rule.
   **[2026-10-15 closed: FINAL. `Holdup` = the mugging reaction (state machine and entry, 21.4 Surrender note);
   `Dbno` HIGH in the same note; `Hostage` = vehicle hostage, released when the car stays below 15 mph (10 for
   the `+0x1dec` variant) for the exit delay (21.4 Hostage note). Advance step 6 / `0x004e68e0` closed in the
   21.1 note.]**
6. The flee sub-state dispatch of `0x004da9f0` (`+0x500` minus 0x5b).

### 21.8 Mission stall map (2026-10-15)

Source: Team B `results/mission_sweep_49_20261013_posteefbbd9/*_stdout.txt`, the line
`script thread '<name>' (id 1) still suspended at [string "<file>"]:<line> (in <function>)`, read together with
the Lua in `scratchpad/lua_dumps/<mission>.lua`. 49 missions: 14 finished cleanly (dlc1_mm_01..06, m09,
mm_m16_5, mm_p_01/02/04/05/06/07; none need AI), 3 ended with an error that already
carries an OPEN citation (m06 navmesh dispatcher, m10 and m16 checkpoint vehicle), and **32 stall at a wait
with no OPEN citation** (the task text said about 31; m21 has two suspended threads and is counted once).
Every wait below is a plain `thread_yield()` polling loop; the number is the loop's yield line from the sweep
log. "Verified" = the setter of the polled flag was read in the Lua; "site only" = only the loop was read.

| Cluster | Mission : Lua line (function) | Polled condition | Engine behaviour that must happen |
|---|---|---|---|
| K1 kill / destroy completion (11) | m08:988 (kill_snipers) | `M08_status.targets_left > 0`; decremented in `m08_sniper_died_cb` and the chopper-destroyed callback | each target's death fires its `on_death` / `on_vehicle_destroyed` thread (Lua spec 17453 section A/B) |
| | m13:3546 (wait_for_waves_to_complete_state) | `wave.state <= state`; state 5 set when the wave's members are dead | a wave's members die; spawned wave members engage (21.9) |
| | m15:728 (run) | wave threads `M15_waves.lobby_stag/lobby_decker.thread ~= INVALID` | same, wave thread ends when its group is dead |
| | sh02:968 (run) | `sh02_guys_killed > 0`; decremented in `sh02_enemy_killed_cb` (on_death) | every Morningstar NPC dies |
| | dlc2_m03:2167 (process_transmitter_sequence) | `num_shields_destroyed < num_shields` | destructible shield objects take damage and fire their destroyed hook |
| | m23:2508 (kill_cyrus) | `cyrus_vtol_destroyed` (hook) or `escape_timer_expired` | the VTOL vehicle dies (vehicle damage runtime); or the escape timer elapses |
| | m18:1593 (cargo_plane_open_cockpit) | `cabin_door_destroyed`, set in the damage handler of the door mover | a mover's health reaches 0 (`on_mover_destroyed`) |
| | m19:1053 (do_containers_checkpoints) | `m19_get_num_destroyed_containers() < size` | container objects destroyed by player damage |
| | m20:971 (objective_crash_peds) | `rc_car_peds_hit` counter, then a threshold | RC car collision with a ped fires the damage hook |
| | m11:1385 (distract_stag) | `num_bases_destroyed < goal` (site read) | base objects destroyed |
| | sh04:773 (wait_for_slots) | `SH04_slot_obj` set in `sh04_slot_died` once `SH04_destroyed_slots >= SH04_TOTAL_DAMAGE` | slot objects die; first death also flips `SH04_GO_HOSTILE` and starts the attack trigger |
| K2 scripted NPC completion (5) | m01:1392 (kill_lobby2_guards) | `M01_josh_grandstanded`, set at line 5515 right after `action_play(...)` of the homie, which follows `ai_do_scripted_move(gat, "grandstand_nav")` (line 1398) | scripted move reaches its nav, then the action finishes (22.11) |
| | sh03:1683 (kinzie_hack_tower) | `get_dist(KINZIE, tower) < 2.0` after `ai_do_scripted_move(KINZIE, tower, true, false)` | scripted move ends within 2.0 m of the nav point (22.11) |
| | m24:980 (hack_terminal) | `homie_death_thread_handle ~= INVALID` (thread ends when the homie's scripted death scene finishes) | scripted-death thread completes; needs action/anim finish (22.11) |
| | sh01:578 (run) | `SH01_HACK_PROGRESS < 600`; the hack thread increments it once per yield and resets it when `human_is_downed(homie)` | the helper thread must run, and the homie must not read as downed |
| | dlc2_m02:1402 (handle_machine) | `explosion_timer_complete` (set at line 2552) | a timer thread elapses; the homie "safe" check is `get_dist(nav, player) < 10` |
| K3 player grab / shield (2) | m05:1209 (get_info) | `character_has_specific_human_shield(player, target)` stays true until released | shield grab (21.4 human shield) and throw/release clears the shield state |
| | m07:1630 (zimos_part_one_run) | `club_owner_grabbed`, set after `m07_cleanup_player_and_victim_for_interrogation` | the grab action completes on the owner NPC |
| K4 vehicle entry / drive goal (4) | dlc3_m02:3831 (get_in_truck) | `character_is_in_vehicle(player, truck) == false` | player seated in the truck (22.7, spec-player-runtime 7) |
| | m17:1224 (hijack_heli) | `M17_has_reached_goal` set by a trigger callback (lines 2374, 2383) | the player (in the heli) enters the goal volume |
| | m04:572 (go_to_mechanic) | tutorial store-reach state (site only) | player arrives at the mechanic |
| | mm_p_03:479 (go_to_flashpoint) | flashpoint counter (site only) | player reaches flashpoint volumes |
| K5 trigger reach (3) | m12:1290 (goto_a_trigger) | `M12_trigger_reached` (line 2124) | player enters the trigger volume |
| | m21:1847 (run_goto_angel) | `player_reached_angel`; second thread `m21_player_reached_angel` waits at 3093 | player enters the Angel trigger; then the talk conversation completes |
| | m22:1593 (process_battle_zone_sequence) | `Current_bz.zone == NO_ZONE` | player enters a battle-zone volume |
| K6 conversation / camera (4) | m14:2390 (convo_play) | `m14_convo_is_playing(convo)` | conversation reaches its end |
| | dlc2_m01:1916 (armory_take1_wrap) | `dlc2_m01_helper_dialog_check_done(...)` | conversation reaches its end |
| | dlc3_m01:1118 (camera_look_through, game_lib) | site only | camera move reaches its end |
| | dlc3_m03:114 (delay, system_lib) | `delay(n)` inside the start function (site only) | game-time clock advances |
| K7 UI / store / door (3) | m03:1279 (weapon_upgrade_tutorial) | `M03_status.item_purchased` set by the `on_purchase` callback | store purchase event |
| | mm_m1_5:100 (run) | `customization_screen_is_ready() == false` | customization screen reports ready |
| | m02:1502 (go_to_cargo_hold) | `door_to_cargo_bay_opened`, set in the `on_door_opened` callback | the mover opens (player interaction or scripted open) |

**Cluster counts:** K1 11, K4 4, K6 4, K2 5, K5 3, K7 3, K3 2 = 32 (sum 11+5+2+4+3+4+3).

**What to implement, by cluster (Team B):**

| Cluster | Spec to implement | Status |
|---|---|---|
| K1 | 21.9 (engagement, damage, death, hook order), Lua spec 17453 (death hooks), `spec-weapons-runtime.md`, `spec-vehicle-damage-runtime.md` | engine side FINAL for the hook firing; **engagement of an unprovoked group member is PARTIAL (21.2, 21.9)**. Nothing in a headless sweep shoots, so K1 needs a harness stand-in as well (21.9 recipe, CHOSEN) |
| K2 | 21.5 (staged order, ticket, finish), 22.11 (move-scripted completion, follower rules) | ticket/finish FINAL; arrival radius of mode 24 OPEN, stand-in CHOSEN |
| K3 | 21.4 human shield + Lua spec 13.14 | gates FINAL; the grab action driver is player input |
| K4 | 22.7 (enter/exit), `spec-player-runtime.md` 7-8; Team B's own checkpoint-vehicle work | FINAL for NPC enter; a player entering needs the harness (like the existing `auto-objective`) |
| K5 | none new; extend the existing `--auto-objective` teleport to battle-zone and Angel triggers (it already handles named triggers, see m07 log) | harness only |
| K6 | Lua spec 2.10 / 8.x conversation natives and the per-conversation length from data | conversation-length table; no AI |
| K7 | store/customization specs (`spec-store-runtime.md`, `spec-cellphone-menu-runtime.md`) | not AI |

Honest scope note: about 60 percent of the stalls (K1, K3, K4, K5 and the player half of K2) wait for the
**player** to act (shoot, grab, drive, walk into a volume). No AI behaviour makes those complete; the harness
must supply that input. The engine behaviour that is still needed from the AI side is 21.9 (hostiles shoot
at the player and die when damaged, with correct hook timing) and 22.11 (scripted NPC orders finish).

### 21.9 How a mission-spawned enemy engages and dies — PARTIAL (structure FINAL, first-sight trigger OPEN)

What is settled, with the sections that hold it:

1. **Spawn.** `group_create` instantiates the group members at their authored positions with the authored
   behaviour and attitude; this is specified on the Lua side (`spec-lua-api-behaviour.md`, group natives).
   A member is a human with the §21.1 decision loop running from its first update.
2. **Target.** Per §21.2 no code converts a sighting into the target handle. The target handle is written by:
   the damage and notice path (`0x004dedb0` through the kind-3 default case), mode events that carry a
   source, the follower copy (2000 ms), the human-shield grab, the grenade throw, scripted orders, and the
   spawner routine at `0x006745ea`, which (case 3 of its switch, loop at `0x006745a8..0x006745e2`) takes the
   **nearer of two player characters** (distance routine `0x0046c9d0` against `+0x40` position, strictly
   smaller wins, start value constant `0x12a2ea0`) and calls `0x009659f0(human, nearer)`. That routine's
   other cases build the human first (`0x00a33b80`, request class `0x2a` into pool `0x2442750`, `+0xa8` set,
   flag `+0xad` bit 3, then `0x4dff80`..`0x4e0870` ability enables, `0x946020`), so the whole routine is the
   **ambient attacker spawner** (case index 0..3 chosen by the caller) rather than the mission `group_create`
   path (HIGH; the mission path was not matched to it).
3. **Damage and death.** A hit on a human goes through the damage path to kind-3 default handling, which
   sets awareness and the target (so a shot enemy always turns on its shooter); health reaching zero marks
   the human dead; the death hook thread is queued at the next per-human update and runs after older
   threads in table order (Lua spec 17453 A.2, A.3, B).
4. **Kill-all completion.** There is no engine "group dead" event. Every kill-all wait in 21.8 is a Lua
   counter or flag decremented inside an `on_death` callback (m08, sh02), a per-wave thread that ends
   when its members are gone (m13, m15), or a polled helper. Port rule: implement the per-member hook
   thread and nothing group-level.

**Port recipe for a headless run (CHOSEN, not engine behaviour, must be labelled as a stand-in in the port):**
for a hostile group member whose list-0 perception holds the player and whose target is empty, set the target
to the nearer player with the §21.2 nearer-of-two rule once per 2000 ms (the same cadence as the follower
copy); then let the §21.1 loop choose shoot/advance/cover. To end a kill-all wait without input, apply the
kill through the damage path (not by deleting the object), so the hook ordering of Lua spec 17453 B still
holds: the member dies on the next per-human update and the callback thread runs after the yield of the
calling thread.

Test vectors (derived from the Lua, FINAL for the counters, CHOSEN for the damage source):
- TV-K1a m08: set `targets_left` = N; kill one sniper; after the calling thread yields, one callback thread
  runs and the value is N-1; the wait at m08.lua:988 returns when it reaches 0.
- TV-K1b sh02: `sh02_guys_killed` decrements once per `sh02_enemy_killed_cb`; the wait at sh02.lua:968 exits at 0.
- TV-K1c ordering: `character_kill(x)` then `thread_yield()` in the same thread: x's callback runs only
  after that yield and before the caller resumes the next frame only if it was created earlier in the table
  (A.3(c)).

OPEN: the first-sight target writer (live hook on `0x009659f0`, §21.7 item 1); ~~the mission-group spawn path
versus `0x006745ea`~~.

**[2026-10-15 closed: mission group spawn path versus `0x006745ea` — FINAL for "no target, no order".
Static, instruction stream only.]**
- `group_create_do` (`0x00a4c880`) resolves the group name, resolves the group object through the
  singleton method `0x005eab60` (singleton `0x02442750`), requires it alive (`0x00853b10`), then calls
  `0x00677b40(group)` and `0x00a2bb50(group, hidden=0)` (`group_create_hidden_do` differs only in that
  literal). Creation is once only: both routines test and set group `+0x3a` bit 1 first.
- `0x00677b40` walks the group's two member lists (npc list head `+0x40`, link `+0x9c`; vehicle list
  head `+0x48`, link `+0xa0`) and posts one prototype-load request per member (`0x00bbb4a0` for
  `"group_npc"`, `0x00bbb500` for `"group_veh"`, priority 5). Loading only.
- `0x00a2bb50` walks the member list (head `+0x3c`, link `+0x88`): member `+0x90` bit 2 = hidden (from
  the argument); if member bit 0 (created) is clear it calls the member's create slot (`+0x54`), and if the
  has-entity slot (`+0x68`) still says no it queues an asynchronous poll (`0x00894330`) with ready test
  `0x00a2ba50`, finish `0x00a2bae0` and a **5000 ms** (`0x1388`) limit; member bit 1 = pending, cleared by
  the finish callback.
- The creating member class (vtable `0x0117ec5c`, slot `+0x54` = `0x00a2e0b0`) looks the prototype up from
  member `+0xac`, builds a creation request at the authored position `member+0x40` / orientation
  `member+0x4c` with flags `0x60021` (`0x00b84ec0`, `0x008fbbc0`), posts event `0x1f` to the singleton
  (`0x00456e30`; `0x1e` when the prototype is missing), copies the member pair `+0xb0/+0xb4` into the new
  object's `+0x1ac/+0x1b8` (only when the class descriptor byte `+9` has bit `0x80`; no AI code reads
  those two fields), sets the hidden bit through `0x008ccb90` (a generic object visibility setter) and
  marks the member created. The human then exists as a normal object whose §21.1 loop runs from its first
  update.
- **None of these routines writes the target handle, any ticket or order block (`+0x450`/`+0x4a8`), or
  calls `0x009659f0`.** The function `0x006745ea` belongs to the ambient attacker spawner only.
- **Port rule (FINAL for the engine, the nearer-of-two stand-in stays CHOSEN):** a mission-spawned enemy
  starts with an empty target and acquires the player only through perception, damage, events or scripted
  orders (21.2). Do not give group members an initial attack target at `group_create`. The group is
  usable for script waits after `group_create_check_done` (`0x00a4c810`) reports every member created;
  allow up to 5000 ms per member before treating a creation as finished-with-failure (the finish callback
  just clears the pending bit; the member stays without an entity).
- Residual: the vtable `0x0117ec5c` class was identified by its create slot, not by a typed name; the
  vtable `0x0117ea54` class has a no-op create slot and a has-entity test on `+0xb0`, so members of that
  class are filled by another path (not traced).

### 21.10 Open items added by the stall map

1. Which mode owns action 24 `move scripted`, and its arrival radius (22.11). **[2026-10-15 closed: mode 5
   `Combat Movement`; its tick `0x0053fe30` dispatches action 24 to `0x0053e600`; arrival = squared
   distance from the human to the target object's local box below 2.0 (about 1.414 m); see 22.11.]**
2. Whether the sh01 hack counter (`SH01_HACK_PROGRESS`) stalls because its thread is blocked behind an
   `action_play` that never finishes (K2); the two lines at sh01.lua:1535-1545 show a plain per-yield counter
   needing 600 passes of the scheduler (1500 for the full hack), so the stall is upstream.
3. Sites read only (not verified against the polled setter): m04, mm_p_03, dlc3_m01, dlc3_m03, m11.
4. Downed NPC types and exits: closed, see 21.10.1.

#### 21.10.1 Downed NPCs (Dbno): types, how the state ends, test vectors — FINAL

Static reading, 2026-10-16. Labels: FINAL = read in the disassembly; HIGH = one link inferred; GUESS = meaning not
proven. Downed = stance word `+0xcc8` == 6 (`0x009715d0`); dead = 5. The AI mode `Dbno` (10, 21.4) only waits and
leaves to `Idle` the first tick the stance is not 6; everything below is owned by the stance layer. This section
supersedes the type-0 expiry claim and the misplaced 2026-10-16 type 1-4 note.

**Three routines that end the state (FINAL).**
- `0x00971d10` (human in ESI): unlink the partner pair `+0x18f0/+0x18f4` (restore the partner's saved weapon slot
  `+0x1904` through `0x00600720` when its `+0x1905` bit 6 is set), owner side `0x0094ce30(0)`, `0x00946470(0)`,
  timer `+0x19c0` = 0, release the marker `+0x18fc` (`0x008f7870`, `+0x1905` bit 4), stop the QTE HUD handle
  `0x01309fa4`, reset the type `+0x18e0` (`0x00942920`). It does not change the stance.
- Kill `0x009a7610(human, send_if_remote, no_send)`: network message 0x41 (unless suppressed), then if downed:
  `+0x1c98` bit 5 cleared, HP `+0x1cb8` = 0, partner unlinked, `+0x1900`/`+0x15f8` released, marker released,
  `0x00973700` (`0x00971d10` + stance 0), then the death finalizer `0x00952760(human, 1)` -> stance 5.
- Get-up `0x009a7c90(human, helper, relocate, no_send)`: message 0x41 (unless suppressed), release handles and
  marker, owner: `0x00973700` (end, stance 0 through `0x00942110(human, 0, 0, -1.0)`), optional relocation, then the
  revive body `0x009a78d0`: clear `+0xe8` bit 0, heal `0x0094cfc0(int(+0x1cac) x pct)` with pct =
  `Human_revive_percent_hit_points` 1.0 (`0x0262d1c4`) when team byte `+0x1ca8` is 0, else
  `Enemy_revive_percent_hit_points` 0.25 (`0x0262d1c0`); put out fire if burning (`0x0097cf40`); request `Idle`
  (`0x004e8070`).

**Entry (FINAL).** The HP setter `0x0094cfc0` (this = human, one float) clamps to [0, `int(+0x1cac)`]; the owner
writes `+0x1cb8`; a pending hit (`+0x1584` != 0) goes to the damage event `0x0096f5e0`, whose last step is the
downed damage hook `0x00971ad0`; then, if HP <= 0, `0x00973d40` tries to down the human; if it refuses and the human
is not already down or dead (`0x0096f540`), the normal death `0x0096f8c0` runs. `0x00973d40` refuses when the stance
is 5 or 6 or `+0xe3` bit 0 is set, or the selector returns -1, or `0x0052a2e0` holds (it then calls `0x0052aa90`),
or (owner only) the type predicate fails; else `0x0094fa00`, enter hook (false = refuse), stance 6 through
`0x00942110(human, 6, 0, -1.0)`, `+0x18e0` = type; owner: `0x004de1f0`, clear `0x00502e40` (`+0x510`),
player-class -> `0x007f0140(1)`, follower -> `0x00bdadf0`. Script entry `human_enter_downed` (`0x00a4ea10` ->
`0x00973ee0`) runs the same `0x00973d40`, then overwrites the deadline with its duration when > 0.

**Selector `0x009735d0`, first match wins (FINAL; mode-gate meanings HIGH).** -1 if `+0x1c9c` bit 17. 0 if
`0x0097e280` (a non-player follower) is non-null. 0 if player-class (kind bit 0x21) AND a co-op partner exists
(`0x009df3d0`) AND (owned or the handle matches `0x024d85b8`) AND `0x006a3910()` != 14 AND `0x006d2910()` is not 7
or 18 (`0x00614d50`) AND `0x00682ad0()` is false. 2 if the character record (`+0xf0` -> `+4`) is
`npc_Killbane_wrestling` (`0x0149f928`). 3 if it is `npc_Killbane_scifi` (`0x0149f92c`). 4 if avatar
(`0x005e6ce0`: player-class in stance 16, or 12 with `+0x15c5` bit 2, or record `npc_Matt_Miller_Avatar`
`0x0149faa4`) and not player-class. 1 if brute (`0x0052a230`) AND gang member (`0x00943190`) AND not `0x009422d0`
AND not `+0x1c9c` bit 22. Else -1 = no downed state (plain death). A single-player main player has no partner, so
it never goes down.

**Hook columns (FINAL).** +8 can-use(downed, actor); +0xc use(downed, actor); +0x10 damage(downed, attacker,
damage); +0x14 tick. The tick wrapper `0x00971a10` runs only for the owner, stance 6 and a valid type. Use comes
from the interact-prompt action `0x009da510` (nearest downed within 3 m, radius squared 9, via `0x00971bb0` + the
+8 test) or the network receiver `0x008b5a50`; a non-owner forwards the request (1000 ms wait, `+0x1aea`). Prompt
text (`0x005b7e90`): type 0 `HUD_DOWNED_REVIVE` (+ `HUD_HOLD_BUTTON` unless instant), 1 `HUD_FINISH_BRUTE`,
2 `HUD_JUMP_ON_KILLBANE`, 3 `HUD_ATTACK_KILLBANE` (only while flag `0x0149f924` is set), 4 `HUD_ATTACK_MATT`.

| Type | Who (predicate) | Enter: timer | Expiry (deadline `+0x18e8` passed, owner) |
|---|---|---|---|
| 0 | followers (homies); co-op players whose partner is standing and within 1200 m (`0x00971fb0`, squared 1.44e6 at `0x02625310`); not stance 14/18 | 30000 ms x difficulty, 15000 in mode 5; none for m24 followers (21.4) | **death** `0x009a7610` (bleed-out) |
| 1 | gang brutes, not burning, overkill within `Bleed_Out_Hit_Points` (below) | `HD_brute_down_time_ms` 7000 (`0x026252c0`); if HP > 0 at entry: anim `0x33b` length + 1500 ms | **gets up**: `0x009a7c90(h, 0, 0, 1)`, heal 0.25 or 1.0 by team |
| 2 | M21 Murderbrawl Killbane | `HD_killbane_down_time_ms` 10000 (`0x026252b8`) | host: `m21_killbane_damaged` (value 6) to object `M21_Killbane` (`0x005e53f0`); then gets up healed |
| 3 | m24 Mars Killbane | `HD_mars_killbane_down_time_ms` 8000 (`0x026252b4`) | plays `m24 getup`; then HP +30 points of percent; Lua `m24_killbane_on_undowned`; stands |
| 4 | Mission 16 Matt Miller avatar | `HD_avatar_down_time_ms` 10000 (`0x026252b0`); none when the stun count is 5 | `0x005e59f0(-1, 0, 0, 1)`: next cheat round in 5000 ms if fewer than 4 stuns; stands, no heal |

**Type 0 detail (FINAL).** Can-use `0x00971650`: not already being revived (`+0x1905` bit 0); a player actor always
may revive a follower; otherwise the actor must be player-class or pass `0x0052ede0` (descriptor `+0x48` bit 4)
and the team relation `0x009b7100(actor +0x1ca8, downed +0x1ca8)` must be below 4. Use `0x00973a90`: instant
get-up `0x009a7c90(downed, actor, 0, 1)` when the actor's `0x009aa130` object has `+0x1c98` bit 22, when
`0x009b5690(downed)`, or when either side is an avatar. Otherwise a paired animation (`DBNO revive`;
`Brute DBNO revive` for a brute; `M24 Revive Fail` when the no-timer bit 3 is set) whose state `0x1d54` is scaled
to last `HD_revive_hold_time_sec` 2.5 s (`0x026252c4`, x 1.0 at `0x01309fa0`); the deadline pauses (remaining time
saved in `+0x18f8`, deadline = -1), bit 0 set, both sides cross-linked. Tick `0x009723f0`, in order: (1) Whored
mode (`0x006e44f0`) and a player-class downed that fails `0x006e8c40` -> immediate death. (2) Revive running:
helper let go (bit 1, not bit 2) -> resume the saved timer (`0x00971ea0`); the downed's `0x1d54` ended -> get-up
`0x009a7c90(downed, helper, 0, 1)` = revived (heal per team); helper no longer in `0x1d54` -> set bit 1 (except
no-timer), else bit 2. (3) Deadline passed (no-timer bit clear) OR, for a player-class downed, every player in the
list is down or dead -> owner: death `0x009a7610(h, 0, 0)`; non-owner: bit 7.

**Type 1 detail (FINAL).** Predicate `0x00972790` is false when: the attacker (`+0x1570`) is a live brute and
`+0x1568` bit 7 is set; owner and `+0x1588` < -threshold (threshold `0x00972740` = instance override word
`+0x1da8`->`+0xb8` when set, else `Bleed_Out_Hit_Points`, record `+0x0C`); burning (`0x0097cdd0`, `+0xe8` bit 8);
`0x009b9180`; Whored mode; or `0x0095d720` with a vehicle passing `0x0052a590`. Enter `0x009728c0`: buffer word
`+0x18e4` = `int(+0x1588)` + threshold; marker `0x008f96f0(h, 7, 0x13, 1, 100.0)`; state `0x2a5`. Damage hook
`0x00972bf0` (owner, HP <= 0, timer armed): buffer -= `int(damage)`; buffer < 0 OR damage == -1.0 (the lethal
constant) -> death `0x00952760(h, 0)` + ragdoll; else remaining = min(remaining + 50 x damage, 7000)
(`HD_brute_damage_to_timer_ms` 50, `0x026252bc`) and a hit reaction `0x2a6` when state `0x1d5c` > 0.7. Can-use
`0x009716e0`: not ragdolled, neither side in `0x009634f0`, timer armed, partner empty or the actor. Use (finisher)
`0x00972a70`: actor must be player-class; animation alternates by toggle `0x02625368`: 0 = `Player finish dbno
brute`, 1 = `BRUTE_Execute_Head` (owner player is handed `Pistol-Gang` for it); success sets the brute's deadline
to -1. Tick `0x00972d30`: burning -> death; ragdolled -> re-arm 7000 and replay `0x33b`; timer armed -> drive the
get-up state `0x1d60` (HIGH: starts when remaining seconds <= the `0x33b` length; rate 0.3 or 1.0) and at expiry
get-up `0x009a7c90(h, 0, 0, 1)`; deadline -1 (finisher) -> death `0x00952760(h, 0)` when the brute's `0x1d54` ends
or the player's `0x1d54` passes the event at `0x0153be20`.

**Types 2-4 use (FINAL).** Type 2 `0x00973090`: get-up `0x009a7c90(killbane, player, 0, 1)`, end, then
`0x005e4420` -> `0x009a87d0(player, killbane, 0)` (HIGH: start of the Killbane ride). Type 3 `0x009717d0`
(player-class actor, attack flag `0x0149f924` set, `0x0095d8c0`): beatdown `m24 beatdown 1/2/3` chosen by
clamp(floor((1 - HP/maxHP) x 3), 0, 2); local player gets the `QTE` HUD; deadline -1; flag cleared. Its tick while
the beatdown runs: HP% <= 12.001 -> at progress 0.75 the host calls Lua `m24_killbane_on_death` and the owner kills
him (`0x00952760`); otherwise at progress 1.0 `m24_killbane_on_undowned` and he stands with no heal. Type 3 expiry
heal (only while the flag is still set): HP = floor(HP% + 30) / 100 x maxHP. Type 4 `0x009734b0`: end, stance 0,
then `0x005e6d40`: cheats reset `0x005e5e40` and sync action `Mission16_StunQTE` (`0x0060e740`, callback
`0x005e5960`); the stun count `0x012ec720` is raised by `0x005e76a0` (limit 4). Type 4 starts its get-up 2.8 s
before expiry. `0x004bf810` = name -> action id lookup; `0x00a275e0` = play that action on the human (HIGH).

**How a downed state ends, ordered (FINAL unless marked).**
1. Timer expiry: per the table. Only type 0 dies; 1-4 stand up.
2. Another character uses the downed human (3 m prompt): type 0 revive (2.5 s hold, or instant); type 1 finisher
   (death); type 2 get-up + ride; type 3 beatdown (death at HP <= 12%, else stand); type 4 stun QTE (stand). NPC
   buddies never revive: the only callers of the use wrapper `0x00971b10` are the player prompt and the network
   receiver, and event 117 is ignored (HIGH).
3. Damage while downed: the HP setter cannot kill (stance 6 counts as down in `0x0096f540`) and the sink's kill
   test `0x0096f990` excludes `+0x1c98` bit 5. So types 0, 2, 3, 4 ignore further damage; type 1 dies when its buffer
   runs out, on the -1.0 lethal hit, or when burning. There is no NPC finisher on downed humans (HIGH).
4. Co-op: all players down -> each dies at its next tick; partner more than 1200 m away (`0x007049c0`, squared
   1.44e6) -> the downed local player dies; Whored mode without a revivable partner -> immediate death.
5. Scripts: `player_revive` (`0x00a599b0`) and `npc_revive` (`0x00a55680`) -> `0x009a7c90(h, 0, 1, 0)` (stand,
   relocated, healed); `character_kill` (`0x00a41a60`) with arg 4 = false -> `0x009a7610(h, 1, 0)` (death); with the
   default arg 4 its -1.0 damage kills only a type-1 brute (HIGH); `human_enter_downed` forces entry.
6. Engine: cutscene start `0x00726570`, teleport `0x009e69d0` and restart/respawn `0x006d3780`/`0x006d4a10` call
   `0x009a7c90` (stand); `0x006d89a0`, `0x008b7a40`, `0x008b8980`, `0x009e2bc0` call `0x009a7610` on a downed local
   player (death; HIGH: respawn/mission-state handlers). Replicas replay kill/get-up from message 0x41
   (`0x009a81c0`).

**Port rule (FINAL).** Down instead of dying only when the selector picks a type and the owner's predicate passes.
Type 0 counts down and kills at the deadline unless a player holds revive for 2.5 s (pause, resume on release).
Types 1-4 stand up at their deadline (type 1 healed by team pct, type 2/3 as above, type 4 no heal). Ignore damage on
downed types 0/2/3/4; apply the brute buffer and timer extension for type 1. Never let NPCs revive.

**Test vectors.**
1. Follower, team 0, `+0x1cac` 200, scale 1.0, HP -> 0 at t = 0: type 0, stance 6, deadline 30000. No help: at
   t = 30000 stance 5, HP 0 (FINAL).
2. Same, player uses it at t = 10000: saved remaining 20000, deadline -1; revive state ends at t = 12500: stance 0,
   HP 200, mode `Idle` (FINAL).
3. Same, player releases at t = 11000: bit 1 -> timer resumes with 20000 left -> death at t = 31000 if not
   restarted (HIGH: resume re-arms from `+0x18f8`).
4. Gang brute, `Bleed_Out_Hit_Points` 300, killing hit leaves `+0x1588` = -50: type 1, buffer 250, deadline 7000.
   Hit for 100 at t = 1000: buffer 150, remaining min(6000 + 5000, 7000) = 7000. Hit for 200: buffer -50 -> dies
   (FINAL).
5. Same brute, killing hit leaves -400: -400 < -300 -> no downed state, plain death (FINAL).
6. Brute left alone, `+0x1cac` 400, enemy team: at t = 7000 stands, HP 100 (FINAL).
7. Two finishers in a row on two brutes: first plays `Player finish dbno brute`, second `BRUTE_Execute_Head` with
   `Pistol-Gang` handed over; both brutes end in stance 5 (FINAL).
8. Mars Killbane, maxHP 1000, HP 300 at entry, flag set, no attack: at t = 8000 `m24 getup`; after it HP = 600,
   `m24_killbane_on_undowned` (FINAL). Beatdown at HP 100: `m24 beatdown 3`, at progress 0.75
   `m24_killbane_on_death`, stance 5 (FINAL).
9. Matt avatar, stun count 1: deadline 10000; at expiry `0x012ec724` = 1, cheat deadline now + 5000, stance 0.
   Stun count 5 at entry: no deadline (FINAL).
10. Co-op: both players downed -> both die on their next ticks (FINAL).

Residual: names of the selector mode gates (`0x006a3910` == 14, `0x006d2910` in {7, 18}, `0x00682ad0`) and of
`0x0095d720`/`0x009b9180`; what the m24 follower's `M24 Revive Fail` knock-down (`0x009a48e0` at progress 0.8)
looks like; whether a `0x0052ede0`-class NPC ever reaches the use wrapper (no static caller found).

## 22. Mission-driving AI (2026-10-15)

Scope: what a mission script can make an AI vehicle or an NPC do with a vehicle, and what the engine
does about it. Static analysis only (capstone listings of the shipped exe; no Ghidra, no live game).
Builds on §17-§20 (lane follower, §20.6.3 speed controller, §20.6.7 follow law `0x00b51950`, junction
yield `0x00b5e6c0`, lane change `0x00b555d0`), §18.7 (directive modes) and the Lua-side facts in
`spec-lua-api-behaviour.md` (§9.4 `vehicle_chase`, §9.10a pathfind completion, §21.11/§24.9a exit, the
"Engine callback dispatch: vehicle entry/exit" section) and `spec-player-runtime.md` §7-§8 (the seat
state machine). Addresses are plain hex. Clean-room check: no decompiler tokens.

Status legend: FINAL = read from the instruction stream and consistent; HIGH = read, one link inferred;
PARTIAL = structure and constants read, a role is a hypothesis; OPEN = not decoded.

| # | Item | Status |
|---|---|---|
| 1a | Chase (`vehicle_chase`): directive, flags, speed law, steering, hand-off, give-up | FINAL for the speed law, the planner cadence and the flags; PARTIAL for the flank manoeuvre and sub-states |
| 1b | Follow | No native exists (see 22.4); chase with a vehicle target is the follow behaviour. FINAL |
| 1c | Drive-to: `vehicle_pathfind_to_do` (road-graph route) and `vehicle_pathfind_navmesh_do` (waypoint directive) | FINAL (arrival radii, flags, completion) |
| 2 | NPC enter/exit: seat-record fields, timeline, predicate reads, group straggler rule | FINAL for the fields and writers; PARTIAL for animation-driven durations (data) |
| 3 | Speed and aggression parameters, off-road, stuck | FINAL for constants listed; stuck recovery PARTIAL |
| 4 | Helicopter fly-to basics | PARTIAL |
| 5 | Test vectors | 22.9 |

### 22.1 What the natives do to the vehicle directive

All of them write the **directive** at vehicle `+0xc68` (mode byte at directive `+0`, sub-state at `+1`;
§18.7.1). The ambient lane follower is mode 8 (§20.6); a script directive replaces the mode, so the
ambient combiner `0x00b57ab0` (which runs only for mode 8) stops driving the car and the mode's own tick
(table `0x01186da0`, 20 bytes per mode, +4 = tick) takes over. Changing the mode runs the old mode's exit
slot, then the new mode's enter slot (`0x00b24e40`). Traffic lights are obeyed only in modes 0, 7, 8, 15.

| Native | Directive written | Tick |
|---|---|---|
| `vehicle_chase` (`0x00a65ac0`) | mode 4, sub-state `0x0e` | `0x00b4d8f0` |
| `vehicle_pathfind_to_do` (`0x00a680c0`, helper `0x00b2b140`) | mode 8 with a scripted route (`0x00b57680`) plus a move-to request at directive `+0x18` = 1 | lane follower, plus `0x00b2e720` for completion |
| `vehicle_pathfind_navmesh_do` (helper `0x00b24fa0` then `0x00b4f150`) | mode 7 | `0x00b4f900` |
| `vehicle_speed_override` (`0x00a63630`) | directive `+0x9c` = speed (m/s, capped at 1000.0), `+0xa` bit `0x10` = 0 | read by the speed model |
| `vehicle_stop_do`, `vehicle_disable_chase`, `vehicle_set_no_chase` | see `spec-lua-api-behaviour.md` | |

Chase ends by returning to ambient: `0x00b63ac0(directive, 0, 0, 0)` (mode 6, which rejoins the lane
graph, or clears the directive when `never_go_ambient` or no road graph; §18.7.1).

### 22.2 Chase (mode 4)

**Entry.** `vehicle_chase(vehicle, target = "#PLAYER1#", a = true, b = false, c = true, d = false)`:
1. If the vehicle is already in mode 4 or 3 (`0x00ad31a0`), only the target position is pushed
   (`0x00b3c260`, opcode `0x42`). Otherwise a block is built by `0x00b47110(block, targetHandle,
   not b)` and committed by `0x00b4d260(directive, block)`.
2. Block layout: `+0` target handle (8 bytes), `+8` byte = 3 when `b` is true else 0, `+0xc` = 1 when `b`
   is false else 0, `+0x10` flags dword, `+0x14` word 0. Initial flags `0x12`.
3. Flag bits written by the native: bit 0 = `a`, bit 3 (`0x08`) = `c`, bit 7 (`0x80`) = `d`. Bits `0x100`
   and `0x200` are never set by the native (internal callers only).
4. The commit refuses (leaves the directive untouched) when the driver is a player (`0x00853b30` type
   `0x21`) or the driver's `+0x16c0` bit `0x4000000` is set. Otherwise the new sub-state is `0x0e`, or
   `0x0f` when flag `0x100` is set and the driver's crew count (`0x0097e260`) is at least 2.
5. It copies the target to directive `+0x58` (vehicle `+0xcc0`), and sets the directive bits:
   - directive `+0x1a0` bit 0 ("occupants may attack from the car") = `a`, but forced to 0 for a
     tank-type vehicle (`0x00ad3140`, class value 14) unless flag `0x200`, forced to 0 when vehicle
     `+0xc6c` bit `0x20` is set, and forced to 0 when the target is a player-class human whose own
     vehicle is a tank-type (`0x00ad3290`/`0x009b5690` tests). The only readers found are the
     passenger-shooting tick near `0x00b6d265`/`0x00b6de9d`, which reset their timers while it is 0.
     Name it "drive-by allowed". HIGH.
   - `+0x1a0` bit 5 (`0x20`) = flag bit 3 (`c`), `+0x1a1` bit 6 = flag `0x200`, `+0x1a8` = block `+8`.
   - `+0x1a0` bit 1 = the driver-object test `0x009431c0` equals 1, or the target's vehicle is a
     tank-type; its readers were not located.
6. **Siren/horn effect (arg `d`).** When flag bit 7 is clear and the driver's team (`+0x1ca8`) is above 4
   (police 5/6, civilians 7), the commit calls `0x00ad57a0(vehicle, 1)`, which turns on the vehicle siren
   (`0x00559b40`: sets vehicle `+0x163d` bit 0 and starts audio event `0x762b3179`; `+0x163c` bit `0x10`
   disables it). Leaving chase to mode 3 calls `0x00559b40(vehicle, 0)`. HIGH (the effect is a looped
   audio event; its asset name was not looked up).
7. Two timers: flag bit 1 is set in the initial flags `0x12`, so `+0x4e8` is armed from the random range
   returned by `0x00b47600` (range not decoded); flag `0x40` is clear, so `+0x4ec` is armed to 0 (the
   10000-12000 ms and 2000 ms alternatives apply to internal callers with flags `0x40`/`0x04`).
   **[2026-10-15 closed: FINAL. Directive `+0x4e8`/`+0x4ec` are vehicle `+0x1150`/`+0x1154`, the two
   event-check timers below. `0x00b47600(vehicle; out lo, out hi)` picks a class c and writes lo/hi in ms;
   the timer is then set to now + uniform integer in [lo, hi] inclusive (`0x00d9e180`). c = 5 when vehicle
   `+0xc72` bit `0x20` is set; else c = 1 when vehicle `+0x16e0` = 9; else c = the chase class of
   `0x00b47460`: `+0xe0c` = 1 -> 0; `+0xe0c` = 0 -> byte `+0xe10`; otherwise 0 for a tank-type target test
   (`0x00ad3290`), else by the driver's team `+0x1ca8` (`0x00b473a0`): teams 1-3 -> low byte of the dword at
   `0x014a4f70 + (team-1) * 0x4c` (`0x00605220`), teams 5/6 -> 0, team 4, 0 or above 6 -> 3. Ranges:
   c = 3 -> 9000-10000; c = 4 -> 6000-7000; c = 5 -> 3000-4000; any other c -> 1000 (fixed). The range is
   only meaningful for c >= 3, because the ram check it delays (`0x00b4a7e0`) refuses c < 3. The same call
   re-arms the timer in the mode-4 net-read path (`0x00b4b30c`), there with `+0x4ec` = 2000 ms. Port rule:
   `first_ram_check_ms = c==3 ? rand(9000,10000) : c==4 ? rand(6000,7000) : c==5 ? rand(3000,4000) : 1000`,
   inclusive bounds.]**

**Per tick `0x00b4d8f0`** (vehicle as `this`; target = handle at `+0xcc0`, resolved to a live object):
1. No driver (`0x00ab5100`) -> nothing.
2. Target missing, dead (`0x0096f4f0`) or the target's controller gone -> `0x00b63ac0(.., 0, 0, 0)`
   (rejoin ambient). The chase therefore **ends when the target dies or despawns**; there is no timer.
3. `0x00b4aa10` (event checks, below). If it returns false the tick stops.
4. **Hand-off to the occupants.** If the target is the local player (`0x0091f5e0`) and the driver's
   current enemy is that player (`0x0097e940`), then: when the session test `0x005f5f80` is true, or the
   squared distance between the two vehicles (`0x00da1330`, 3-D) is **< 100.0** (10 m; `0x012a2dd8`), the
   tick calls `0x00a91070(vehicle, 0)` (passenger/turret attack hand-off) and returns without steering.
   Otherwise it runs the pursuit tick `0x00b4d070(vehicle, target)`. HIGH: what `0x005f5f80` tests (it
   gates a distance-free hand-off).
   **[2026-10-15 closed: FINAL. `0x005f5f80` reads the garage/store interaction state `0x014a1ce4` (the
   store state of `spec-lua-api-behaviour.md`, "store state `0x014a1ce4`") and returns true when it is
   neither 0 (idle) nor 3 (interaction left to finish). Writers: 4 when the store UI is entered
   (`0x005fa7bc`, `0x005fa8f6`, `0x005fb40e`), 1 and 2 by the store-location state machine (`0x005fbc32`,
   `0x005fb702`, `0x005fb8b9`, `0x005fa9ee`), 0 on reset (`0x005faa2c`, `0x005fb8cc`). So while the local
   player is in a garage/store interaction (states 1, 2, 4) a chaser whose driver is hostile to that player
   never steers: it hands off to its occupants at any distance. Port rule: `handoff = target_is_local_player
   && driver_enemy == player && (store_state in {1,2,4} || dist2(vehicles) < 100.0)`.]**
5. `0x00b4d070`: target model refresh (`0x00b4ceb0`: plan, flank selection, `0x00b4ad30` sub-state step),
   then by sub-state: `0x0d` = direct steer (`0x00b47490`, no route), `0x0e`/`0x10` = route follow
   `0x00b2bbc0` when the route object (`+0x11a4`) exists; `0x0f` does not steer here. A blocked route
   (`0x00b49820`) switches to mode 3 with the lead and block flags (§18.7.2).
   **[2026-10-15 closed: FINAL for the sub-state step `0x00b4ad30` (jump table `0x00b4af0c`, sub-states
   0x0d-0x10).
   - **0x0f "wait for the crew"** (`0x00b4ad9d`; entered at commit when flag `0x100` is set and the crew count
     is at least 2). No steering. Each tick: if the driver (`0x00ab5070(vehicle, 0)`) is not a human, has no
     group (`0x005083f0`) or the group is empty, go to 0x0e. Otherwise walk the group members (`0x00508290`
     first, `0x00508330` next), skipping the driver. For each one, first ask `0x00520950(vehicle)` for a free
     seat (it returns the first seat index that is usable and empty, else -1). If there is none, stop the walk.
     Otherwise the member counts as missing unless its vehicle handle (`+0x16c0/+0x16c4`) is this vehicle and
     it is seated (`0x009b9210`) or entering (`0x009b9160`). Go to 0x0e when no member was missing (the
     vehicle filling up also ends the wait). Port rule: hold, without steering, until every other group member
     is in or boarding the car or no seat is left.
     **[2026-10-15 CORRECTED (verifier): the free-seat polarity above is inverted. At `0x00b4ae1d` the code is
     `cmp eax,-1; jne 0x00b4ae6f`: when `0x00520950(vehicle)` finds a free seat (result != -1) the walk STOPS
     and, if no earlier member was marked missing (flag still 1), the sub-state goes to 0x0e (`0x00b4ae76`).
     Only when the car has NO free seat (-1) is the member checked (vehicle handle `+0x16c0/+0x16c4` = this
     vehicle and `0x009b9210` or `0x009b9160`, else missing). `0x00520950` returns the first usable empty seat
     index, else -1 (`0x005209bf`). So 0x0f ends at once while any seat is free; it only holds while the car is
     full and some other group member is not in/entering it. Port rule: `if (group empty || driver not human)
     -> 0x0e; for m in group (skip driver): if free_seat(car) != -1 break; if !(m.vehicle == car && (seated(m)
     || entering(m))) missing = true; if (!missing) -> 0x0e` (the break keeps any earlier `missing`).]**
   - **0x10 "recovery / re-acquire"** (entered only by the no-route recovery of 22.6: 150 ms with less than
     5 m of progress; route objects cleared, anchor reset). It steers like 0x0e (route follow once the planner
     has rebuilt a route). Exit test `0x00b4ae85` each tick: go to 0x0e when there is no driver or the driver
     is a player (`0x00853b30` type `0x21`). Stay when the driver's leader (`0x0097e280`) is the target. Stay
     when the driver is not locally simulated (`0x008addb0`) or has no AI object (`0x004d7e00`). Go to 0x0e
     when the target is not in the driver's knowledge/visibility record (`0x004fc6d0` on AI `+0x510` with the
     target handle). Otherwise call `0x004e68e0(ai, target, 0, 0)`: go to 0x0e when it returns 1 (the "no
     engagement" value: invalid or dead human, or no known target position), else stay. Port rule: after a
     recovery, stay in 0x10 while the driver can engage a known target, otherwise go back to 0x0e. The
     recovery itself is suppressed while `0x00b4a7e0` holds (a ram-capable chaser alongside the target).
   - For completeness: 0x0e -> `0x00b47c80` when `0x00b47f10` holds; 0x0d -> 0x0e when `0x00b47730` is false
     and `+0xe08` bit 0 is clear (with bit 0 set it sets `+0xe09` bit `0x20` instead). Those two predicates were
     not decoded here.]**

**Route planning to a moving target (`0x00b4c710`) - FINAL.**
- Planner cadence: re-run when the planner timer (directive `+0x450`) expires; after each run it is
  re-armed to a random **300-450 ms** (`0x00dab660(0x12c, 0x1c2)`) when the target is moving (any velocity
  component >= 0.25), or **2000-2500 ms** (`0x7d0`-`0x9c4`) when the target is stationary.
- The goal is a position on the road graph: it asks the route builder (`0x00b213b0`) for a path to the
  target (tolerances 2.0), and replans immediately when the target has moved more than **15.0**
  (`0x1117a60`) from the position the current route was planned for, and the route is longer than the
  straight distance plus **20.0** (`0x1116998`).
- Fallback when the target is off the graph: nearest lane candidates within **200.0** (`0x12a30d4`)
  (up to 20 segments, search length 1500.0), then a lane walk of at most **5000.0** (`0x111d490`) with
  flags `0x100`, committed with `0x00b516b0`. If no lane is found the vehicle keeps its last route and
  retries on the next planner pass; sub-state `0x0d` (direct steer) is the no-route mode.
- Return values: 0 planned/ok, 1 invalid target, 2 the vehicle is not ready (`0x00b2f160`).

**Speed law (`0x00b48e50`, called from the mode-4 speed hook `0x00b4b340`) - FINAL.** The mode's desired
speed (`0x00b1e840`, case 4 -> `0x00b4b340`) is 1000.0 when the target is invalid or not a physical object
(`+0x33` bit 3 clear). Otherwise `0x00b48e50` computes a **target-relative** speed with these inputs:
- v_own = own speed, v_t = |target velocity|, a_b = vehicle `+0xc58`.
- Stand-off s = **4.0 m** (`0x12a2f6c`); **30.0** when either vehicle is a tank-type (`0x00ad3140`); when the
  own vehicle is airborne class (`0x00ad4810`) s = 2.0 (40.0 if tank-type). If the target is not in a
  vehicle at all the stand-off is the target's body radius (`0x00942370`) and the ambient 10.0 scale
  (`0x1118d20`) applies.
- Gap g = distance from the own look-ahead point (own position + own velocity * own half-length,
  `0x00a78d30`) to the target point, less lookahead bias `0x00b47400`. Flank manoeuvre (below) shifts the
  target point sideways first; with `+0xe08` bit 3 the dot product with the lateral offset is added.
- **Far (g >= s):** v_cmd = v_t + sqrt(m * 1.2 * a_b * (g - s)), with m = 1 normally and **m = 2 while a
  flank manoeuvre is active** (`+0xe20` in {1,2}); constants 0.6 (`0x12a2e90`) times 2.0.
  If v_own > v_cmd: decel = a_b when the excess (g - s) < 0.1, else min(a_b, (v_own^2 - v_t^2) / (2 * (g - s))).
- **Near (g < s):** v_cmd = v_t - 5 * (1 - g / s) (constants 5.0 at `0x12a2f20`, -5.0 at `0x1187ac8`).
  If `+0xe08` bit 6 and g < 0 (overlap): v_cmd = 0, and decel = a_b when v_cmd was above 5. Otherwise, if
  v_own > v_cmd: decel = (v_own / g) * (v_own - v_cmd).
- Brake dead band: the decel is zeroed when it is below 0.5 * a_b (`0x00b4b4d1`).
- The mode-4 speed hook outputs decel and v_cmd to the common controller: brake = min(1, decel / a_b),
  throttle by the law in §20.6.3(b) with the cap **1.0** (modes 2/4/7), vt clamped to [1, 100] so
  k = (vt/5)^2 at vt >= 10.
- Drive-by speed match: when the 1000 ms timer `+0x1150` has expired and `0x00b4a7e0` holds (sub-state
  0x0e, the chaser's occupants may fire, the target is not already hit, own and target velocity directions
  within cos 45 deg = 0.7071), v_cmd is replaced by the target-velocity-based value from `0x00b4b340`
  (|vector from the target's own `+0xcc0` target| + `0x00b476b0`). PARTIAL: the vector's source.
- Tank-type chaser with a target not in a vehicle: v_cmd = max(v_cmd, 10.0).

**Steering.** Route follow (`0x00b2bbc0`) uses the §18.1 lane walker for the look-ahead point. Within
**15.0** (225.0 squared, `0x00b2bbc0`) of the lead handle `+0xcc0` and with vehicle `+0xc72` bit `0x20`
set, it steers straight at the lead (sign from the heading dot product; the near/aligned case uses cos 30
deg and 5.0 m with vt = 3.5 m/s). Otherwise it applies the throttle/brake pair computed above. The
obstacle probe and following law of §20.6.7 belong to the ambient combiner only; a chaser has no
car-following gap other than the 4.0 m stand-off law above, so it closes to about 4 m and matches speed.

**Flank manoeuvre (`0x00b4a180`) - PARTIAL.** Directive field `+0xe20` takes 0 (trail), 1 or 2 (offset to
one side of the target), 3 or 4 (planner failed; become 1 or 2 when the planner next succeeds). The target
point is shifted to the side given by the sign of the cross product of the relative position and the
target's heading; the chooser draws 0 or 1 at random (`0x00dab660`) every **2000-3000 ms**, or every
**15000-20000 ms** when it drew a non-zero value. Disabled (set 0) when the target's vehicle is in the
refused class (`0x00ad5020`). The drive-by "shoot from alongside" behaviour of the occupants is the reason;
no PIT or deliberate ram logic was found anywhere in mode 4: collisions are the consequence of the 4 m
tailgate law, and the ambient collision reactions of §18.7.3 still apply to the chaser's victims.
**[2026-10-15 closed: FINAL for the chooser `0x00b4a180(vehicle; target human)`. Two corrections above: the
random draw is `0x00dab660(0, 2)`, inclusive, so 0, 1 or 2 each with probability 1/3. And 3/4 mean "switch
to side 1/2 pending", not "planner failed". The deliberate ram does exist, but in mode 9 (see Event checks),
not in mode 4.
1. `+0xe20` = 0 when the target's vehicle is in the refused class (`0x00ad5020`). With `+0xe08` bit `0x40`
   clear, it is also 0 when `+0xe0c` = 2 and the team class (`0x00b473a0`) is < 2, or when `+0xe0c` = 1 and
   `+0xe08` bit `0x20` is clear. When the target is on foot (`0x004dcf00` null): 0, and timer `+0xe24` = 2000 ms.
2. Geometric side (`+0xe08` bit `0x40` set): every 300 ms (`+0xe24`), `0x00b49f10` returns two points p, q
   of the target vehicle. The side is 2 when dot(cross(q - p, up = (0,1,0)), (p + q) / 2 - own position) > 0,
   else 1, so the chaser keeps the side it is already on. No change when `0x00b49f10` fails.
3. Random side (bit `0x40` clear): when `+0xe24` expires, draw r in {0,1,2} and re-arm `+0xe24` to 2000-3000 ms
   (r = 0) or 15000-20000 ms (r != 0). If r equals the current value nothing changes. r = 0 sets 0 (trail)
   at once. From 0, 3 or 4 the new value is r. From side 1 or 2, a draw of the other side writes the pending
   value 3 (to side 1) or 4 (to side 2) and arms `+0xe28` = 2000 ms.
4. Pending commit: when `+0xe28` expires and `0x00b49be0(target)` holds, 3 -> 1 or 4 -> 2 and `+0xe28` is
   cleared. If it does not hold, `+0xe28` is re-armed to 1000 ms. `0x00b49be0` requires `+0xe08` bit `0x20`,
   the class gates of step 1, the target in a vehicle, dot(own velocity, target velocity) >= 0.5 (raw,
   m^2/s^2) and squared distance <= 20.0^2 (30.0^2 with bit `0x40`). With bit `0x40` and a pending value it
   also checks a look-ahead point against the target vehicle (dot >= 1.0).
Port rule: implement the four steps exactly. m = 2 in the speed law applies only to sides 1/2, not to the
pending values 3/4.]**

**Event checks (`0x00b4aa10`).** Runs the generic stuck give-up `0x00b20e20(vehicle, 1)` first (13 s with
no progress, or two samples under 4.0 m apart, re-issues mode 3; §18.7.2), then every 1000 ms
(`+0x1150`): if the target-side conditions of `0x00b4a7e0` hold, tries `0x00b60cf0` and, on success,
re-issues the directive through `0x00b5fe80` (block byte 3) - the chaser leaves mode 4. A second timer
`+0x1154` and a 10000-12000 ms re-arm gate the same action when a target is far (squared distance > 100.0)
and ahead (velocity dot product >= 0). PARTIAL: `0x00b5fe80` / mode 5 role (mode 5's speed is 15.0).
**[2026-10-15 closed: FINAL. Correction: `0x00b5fe80` commits mode **9**, not mode 5. The chase hand-off is a
deliberate **ram**.
- **Timer `+0x1150` (directive `+0x4e8`).** First armed from `0x00b47600` (see Entry step 7). When it
  expires and (`+0xc72` bit `0x20` is set or the target is in a vehicle) and `0x00b4a7e0(target)` holds, the
  chaser calls `0x00b60cf0`. On failure the timer is re-armed to 1000 ms. `0x00b4a7e0` = sub-state 0x0e,
  chase class (`0x00b47460`) >= 3, target vehicle not in the refused class, and then: a tank-type chaser
  refuses a player target; a target on foot needs `+0xc72` bit `0x20` and at most 2 live entries among the
  chaser's `+0xd61` handle slots (`+0xd68`, 8 bytes each); a target in a tank-type refuses; class 5 passes;
  otherwise the own and target velocity directions must be within cos 45 deg (0.7071).
- **`0x00b60cf0(vehicle, target handle)` = "strike opportunity".** The target must be a live human (class
  bit `+0xa` 1, not `0x00853b10`).
  - Target in a vehicle (`0x009b9160`): `0x00b5f940` gives four corner points of the target vehicle. Count
    the corners with dot(corner - own position, own velocity) >= 0, and require at least 2 (the target is
    ahead). Then the box-to-box squared gap of `0x00b5ef00` must be <= r^2, with r from `0x00b5f170`.
  - Target on foot (allowed only with `+0xc72` bit `0x20`): the target must be ahead, and the squared gap
    of `0x00b5ee50` must be <= 10.0^2 (30.0^2 when `0x014f71e8` bit `0x80` is set).
- **`0x00b5fe80(directive, block)` = ram commit.** A remote vehicle forwards network opcode `0x42`. Locally
  it calls `0x00b26250(directive, 9, 0x0b)` and stores block `+8` -> `+0x3b0`, block `+0xc` -> `+0x3ac`,
  block `+9` bits 0/1 -> `+0x3a8`, and the target -> `+0x58`. The block built at `0x00b4ac88` carries
  byte `+8` = directive `+0x1a8` when `+0x1a4` = 0, else max(3, chase class), plus `+0xc` = `+0x1a4` and a
  byte `+9` holding the drive-by bit with bit 1 set. A player target
  (`0x00853b30` type `0x21`) also arms global timer `0x0130edd0` from the activity record `+0x24`
  (`0x005f4a50`). The chase tick then stops (returns false).
- **Mode 9 (slots `0x01186da0` + 9*20: tick `0x00b61750`, enter `0x00b5f300` clears `+0x3a8..+0x3b8`).**
  - Sub 0x0b, the strike (`0x00b61160`): it aims at the target. A target in a vehicle is aimed at its
    position plus its velocity times a lead time; the lead uses the constants 0.75 and 0.5x + 1.0 (the exact
    clamp was not transcribed). The aim point goes through `0x00a91fa0`. The tick writes steering
    (`0x00a929e0`) and `0x00a92940(vehicle, 0)`, plus input -1.0 to `0x00a92640` (reverse) when the target is
    behind (dot < 0); otherwise it takes the speed path `0x00b207d0`. `0x00b5f370` is a contact test (HIGH);
    when it holds the tick calls `0x00ab6c90`/`0x00ab7830`.
  - Sub 0x0c, back-off (`0x00b5fc60`): it reverses away (throttle +-1 by side) until directive `+0x4e8`
    expires or the squared distance exceeds 625.0 (25 m), then goes back to 0x0b. The writer of sub 0x0c was
    not located.
  - Exit to chase is `0x00b5f090(directive, rearm)`: the chase block is rebuilt with `0x00b47110` and
    committed with `0x00b4d260`, flag `0x20` set, and flag bit 1 = rearm. The strike calls it with rearm 0
    when `0x00b60cf0` fails or the stuck test `0x00b20e20` fires. It uses rearm 1 when the target is gone or
    dead, or when timer `+0x101c` has run over 2000 ms. A failed sub-0x0c target check uses rearm 1.
- **Timer `+0x1154`** does not call `0x00b60cf0`. When it expires: if the target is in a vehicle, ahead
  (dot >= 0), more than 10 m away (squared > 100.0) and `0x00b64340` holds, then with 15% probability
  (`0x00dab5e0` < 0.15) it calls `0x00b65e80(directive, target)`, a road-graph look-ahead manoeuvre
  (`0x00b1b090`/`0x00b1b420`). On success the tick stops; otherwise `+0x1154` is re-armed to 10000-12000 ms.
  The internals of `0x00b65e80` stay OPEN.
- **Mode 5** is not reached from chase. It is set only by `0x00b5d730` (mode 5, sub 0). The callers are the
  mode-8 blocked-wait escalation `0x00b55330` (from the lane-follow tick at `0x00b5861a`, after a random
  750-1250 ms or 4750-5750 ms blocked window, or at once when directive `+8` bit 0 and `+6` bit 0 are set)
  and the debug command string "rail_obs" (`0x00b2c5fb`). `0x00b5d730` collects the blocking objects
  (`0x00b5d410`, `0x00b5c7c0`) and builds a detour (`0x00b5c600`). Mode 5 = obstacle bypass ("rail
  obstacle") at speed 15.0. HIGH: the name is inferred from the debug string and the callers.
Port rule: chase class >= 3 chasers re-test every 1000 ms (first test after the Entry step 7 delay). When
`0x00b60cf0` passes, switch to mode 9 sub 0x0b, steer and throttle straight at the (led) target, and return
to chase on a miss, a timeout or a lost target. Mode 5 belongs to the lane follower, not to chase.]**

**Give-up.** There is no distance give-up. The chase ends when: the target dies or is gone (mode 6/3
re-entry), the stuck rule fires (13 s, or the 150 ms/5 m rule in 22.6), the player target is within 10 m and
the hand-off fires (the car stops steering; occupants fight), a later script directive replaces it, or
`vehicle_disable_chase` / `vehicle_set_no_chase` is called.

### 22.3 Drive-to

**`vehicle_pathfind_to_do(vehicle, destName, stopAtGoal, flagC)` (road-graph route) - FINAL.**
- Needs a driver; `destName` resolves to a path object (up to 25 points, taken after the vehicle's own
  position) or a point (2 points). Fewer than 2 points -> false.
- `0x00b2b140(vehicle, points, count, stopAtGoal, 0, flagC, speed = -1.0, requestId)`; count must be
  2..25. The local branch builds a lane route from the point nearest the car to the last point
  (`0x00b20920`, mode flags `0x800100`, speed cap 3.0 unless a positive speed is given) and installs it as
  the ambient lane follower's route (`0x00b57680`). A road-graph search of 200.0 around the car and
  around the destination (candidates up to 15.0 away) is used (`0x00b1b040`, `0x00b1b320`, `0x00b1b730`).
- The completion request is a move-to record (`0x00b2e510`, kind 1): block from constructor `0x004f1370`
  has **arrival radius 3.0 m** (`0x1116444`) at block `+0x10`, maximum distance FLT_MAX at `+0x18`;
  flag byte: bit 0 = `stopAtGoal`, bit 2 = path variant (always set), bit 3/bit 4 are set by the checker.
- **Completion (`0x00b2e720`, per frame, X/Z distance d to the goal, a_b = vehicle `+0xc58`, radius r):**
  - `stopAtGoal` false: complete when d < r.
  - `stopAtGoal` true: braking phase starts when closing speed > 0 and d < v^2 / a_b + r, then the request
    completes when speed < **1.0 m/s**. Stored brake demand v^2 / (2 d) and speed cap sqrt(d * a_b) (min 1.0).
  - path variant (flag 4): completes on the last path node when the remaining distance is below the
    sub-path's `+0x34` and `0x00ab8fd0` speed < 1.0.
  The request id is released through `0x006f66d0`, so `vehicle_pathfind_check_done` returns done on any
  completion path (§9.10a). The car travels on lanes at lane speed (§20.6.3), the script only chooses the
  endpoints.

**`vehicle_pathfind_navmesh_do` (waypoint directive, mode 7) - FINAL.** Despite the name no navmesh is used.
- Setup `0x00b4f150(directive, points, count <= 25, block)`: copies the points into the node ring at
  `+0x25c`; block `+0` = mode (`+0x254`; 1 = loop/ring, otherwise one pass; wrap is `index mod count`,
  `0x00b4db50`), `+4` = speed (> 0 sets bit 2 of `+0x258` and `+0x268`), `+8` = nodes to pre-skip, `+9`
  bit 0 = stop-at-end (`+0x258` bit 0), bit 1 -> `+0x258` bit 3. The helper `0x00b24fa0` passes mode =
  (its loop parameter != 0), stop-at-end = its last parameter, speed 0, `+9` bit 1 always set. Which Lua
  argument position feeds each parameter was not separately pinned. The first node is skipped if the car is
  within 2.0 m (squared 4.0) of node 0.
- Tick `0x00b4f900`: advance rule and last-node arrival from §9.10a (squared distance 16.0 = 4 m,
  4.0 = 2 m when stop-at-end, mode 2 = 2500.0); each tick the route to the next node is built on the lane
  graph (`0x00b4ec00`: when the planner timer `+0x450` has expired and the car is more than 2.0 m
  (squared 4.0) from the previous plan point it plans; otherwise the timer is re-armed 500-1000 ms),
  and the car follows it with `0x00b2bbc0` plus the generic stuck give-up. If the car is not on the graph
  it still plans from the nearest lane.
- Desired speed (`0x00b4dcc0`): `+0x9c` when the script speed override (`+4` bit 0x10) is set, else `+0x268`
  when `+0x258` bit 2, else **1000.0** (no cap; the planner's curve and braking limits apply). Throttle cap
  1.0. Lights are obeyed in mode 7.

### 22.4 Follow

No follow native exists (checked against the vehicle, helicopter and character enter/exit/pathfind native lists). The engine behaviours that are
"follow" are:
1. Chase with a vehicle's driver or the vehicle as target: the stand-off law of 22.2 (gap 4.0 m, speed
   matched, +5 m/s per stand-off of excess gap).
2. Lane car-following of ambient traffic (§20.6.7), for any car in mode 8.
3. Helicopter follow (22.8).
4. On-foot followers (`#FOLLOWER#`) are a human AI matter (§21).
No formation logic (offsets, wing positions) exists for vehicles; the flank manoeuvre is the only lateral
offset, and the group enter native only assigns seats.

### 22.5 Parameters per directive

| Mode | Desired speed | Throttle cap | Steering/route | Lights |
|---|---|---|---|---|
| 4 chase | 22.2 law, 1000.0 with no valid target | 1.0 | route follow / direct steer | off |
| 7 waypoint | `+0x9c`, `+0x268`, else 1000.0 | 1.0 | route per node | on |
| 8 lane follow | route/lane value (§20.6.3) | 0.7-1.0 by speed | lane follower | on |
| 2 flee | route value | 1.0 | lane walk away | off |
| 3 ambient | 0 (re-plan) | | | off |
| 5 | 15.0 | | | off |
| 13 | 40.0 | | | |

Personality and aggression: the choice between chase and flee is the §18.7.3 weighted draw
(`VehicleCollisionResponse`) or the threat processing; `vehicle_chase` bypasses it. There is no per-chase
aggression number; the effect of a driver's personality is the pre-chase draw only. Vehicle braking and
radial capacity (`+0xc58`, `+0xc5c`) are scaled by condition (§20.6.3); directive scale fields
`+0x528`/`+0x52c`/`+0x530` are network-serialized state only (read at `0x00b22200`, `0x00b26680`,
`0x00b270a0`) and their defaults were not decoded (OPEN).

### 22.6 Off-road and stuck

- **Off-road.** No mode drives off the lane graph except mode 4 sub-state `0x0d` (direct steering toward
  the target, `0x00b47490`) and the 15 m close-in steering of 22.2. All route-based modes snap to the
  nearest lane (200.0 search) and follow lanes. A destination far from the graph ends the route at the
  nearest lane point; pathfind arrival is then judged against the real goal position (radius 3.0 or
  4 m), so the car may stop at the nearest lane end without completing until the stuck rule fires.
- **Stuck give-up (generic, `0x00b20e20`, all modes).** 13000 ms without progress, or two samples under
  4.0 m apart (reverse-out window 1000 ms, 2000 ms in modes 4 and 8): re-issue mode 3 and, in the hard-stop
  window, brake 0, throttle 0.1 or 1.0, steering 0 (§18.7.2).
- **Chase no-route recovery (`0x00b4d070` tail).** Only when the planner flag `+0xe08` bit 0 is clear
  and the sub-state is not `0x10`: the vehicle is compared with the anchor `+0xe44`; if it is within 5.0 m
  (`0x130edd4`, squared 25.0) a 150 ms timer (`+0xe34`) is armed and, when it expires, the route objects
  (`+0x11a0`, `+0x11a4`, `+0x11e4`) are cleared, the car is stopped (`0x00a90c70`, `0x00a91f30`, zero
  steering `0x00a929e0`), the anchor is reset to the current position and sub-state becomes `0x10`
  (recovery). Moving 5 m or more clears the timer. PARTIAL: the exit from `0x10` (back to `0x0e` when
  `0x00b4ae85` finds no armed enemy). **[2026-10-15 closed: FINAL. The exit test is decoded in 22.2 under
  "Per tick" step 5 (sub-state 0x10). Port rule: leave 0x10 for 0x0e when the driver cannot engage a target
  it knows about.]**
- Scripted waypoint and pathfind requests never time out (§9.10a): the stuck rule changes the mode to 3,
  which drops the waypoint directive; the pending request is completed by the teardown
  (`0x00b46ca0`), so Lua sees "done" without arriving.

### 22.7 NPC enter and exit

**Orders.** `vehicle_enter_do(char, vehicle, instant, seat, ...)` (`0x00a67000`) has three paths:
1. instant: a request block with the instant bit set goes to the submitter `0x00af22a0`;
2. named seat: seat index resolved through `0x004bf810` and the submitter;
3. otherwise an **AI order** `0x00521200(char, vehicle handle, &template, 1)`: refused when the character is
   already seated (`+0x16d4` = 3, `0x009b9160`) or entering/exiting (`0x009b91e0`); refuses when `0x00ad5180(vehicle)` is true, validates the seat
   (`0x00b0fdf0`), copies the 22-dword action template at `0x012e14e0`
   into the character's action block (`+0x4a8`) and starts action `0x31`. The walk to the door is the seat
   state machine (states 2, 3, 5; `spec-player-runtime.md` §7.5); the state-1 hub requests the AI mode
   `Veh enter` (38) (§6). On success a player-class character gets its
   script-mode field set to 2 (`0x009e3200(2, 0)`).
`vehicle_enter_group_do` repeats this per table entry; instant mode gives seats by table position (first
entry = seat 0 = driver).

**Seat choice** (state-0 upkeep `0x00af5cb0`, every 250 ms per vehicle record): request search mode 0 =
the requested seat, 1 = any seat from 0 (driver first), 2 = any seat from request `+0x04`; the submitter
turns 1 into 2 for a remote-controlled vehicle or a player-class character. Seats are scored by
`0x00b0eb90` (animation availability for the door side, bearing thresholds 45 and 105 degrees to pick the
entry point; `spec-player-runtime.md` §9). No seat -> cancel (state 8).

**Seat record sequence** (vehicle `+0x1340` + `0x30` * seat; FINAL). The three handles that
`character_is_in_a_driver_seat` reads are written by distinct steps:

| Step | Writer | `+0x00` claim | `+0x08` pending | `+0x18` seated | human fields |
|---|---|---|---|---|---|
| submit (state 0 on-entry `0x00af9a90`) | slot alloc | unchanged | unchanged | unchanged | `+0x16d4` = 1, `+0x16c0/c4` = vehicle |
| seat resolved, state 1 hub `0x00afd4c0` -> `0x00b0cb20` | claim | empty -> character; else character -> `+0x10` (next claim) | - | - | `+0x16c8` = seat, `+0x16cc` = entry point; queued in the entry-point queue |
| (switch/exit side) `0x00b0b540` from the exit states | pending | - | exiting character (the previous holder) | cleared | `+0x16d0` = seat |
| seating commit, state 9 `0x00afbd90` -> `0x00b0b930` | commit | `+0x10` -> `+0x00` (or empty) | - | character | `+0x16d4` = 3 (ride state `0x22`) |
| exit release `0x00b086d0` | release | - | - | cleared (if still this occupant) | `+0x16d4` = 0, `+0x16c0/c4` null |

Timing: submit and state 0 happen immediately; the state-1 claim happens at the first seat-resolution
upkeep, i.e. on the same frame for requests with the skip bit (request `+0x21` bit 0), else within 250 ms.
The animated entry (walk to the door, state 2/3, door animation state 4 `Enter_Animations`) takes the
clip length in `vi_enter.xtbl`; the commit fires when the seat-occupancy entry names the character
(`0x00b0b3f0`), and instant/teleport routes (states `0x0b`, `0x0f`, `0x07`, `0x11`, `0x13`) commit at
once. At the commit `on_vehicle_enter` is posted (vehicle slot 7, then player slot `0x16` or NPC slot
`0x17`) and **in the same engine call** `+0x16d4` becomes 3. So by the first script resume after the
commit the thread exists and every poll answers "seated".

**What each query reads at each stage (FINAL):**

| Stage | `character_is_in_vehicle` (`+0x16d4`=3) | `character_is_in_a_driver_seat` (seat 0) |
|---|---|---|
| ordered, not yet submitted | false | false |
| state 0 (submitted) | false (`+0x16d4` = 1) | false (vehicle handle matches, no seat field yet) |
| state 1 and after, seat 0, before commit | false | **true** (claim = character) |
| committed | true | true (seated at `+0x18`) |
| passenger seat | true after commit | false always |

`vehicle_enter_check_done` reports done once `+0x16d4` = 3 for the target vehicle (its numeric return is
in `spec-lua-api-behaviour.md` §22.3); `vehicle_exit_check_done` is true for `+0x16d4` in {0, 1}.

`character_is_in_a_driver_seat` = `inVehicle(c) && (seat0.claim == c || seat0.pending == c ||
seat0.seated == c)`; "inVehicle" here means the character's `+0x16c0/c4` equals this vehicle's handle,
which is set at state 0, so the driver check can already be true while `+0x16d4` is 1 or 2. A character
leaving seat 0 stays true while it is the pending handle (exit states) until release.

**Exit.** `vehicle_exit_do` requires `+0x16d4` = 3 at the call and builds an exit request; exit states
`0x14`..`0x21` run (`+0x16d4` = 2 from `0x14`'s entry). `on_vehicle_exit` fires at the finaliser `0x00b03d80`
before `+0x16d4` is cleared; `vehicle_exit_check_done` is true for `+0x16d4` in {0, 1}.

**`vehicle_enter_group_check_done` stragglers (FINAL, `spec-lua-api-behaviour.md` §38.1).** Done only when
every member is: absent/dead, or in state `+0x501` = `0x26`, or seated in this vehicle (`+0x16d4` = 3 and the
vehicle handle matches). Polling with do-not-reorder false re-issues the AI order (`0x00521700`) to every
member that has no pending order. With do-not-reorder true the group never completes if a member cannot
seat.

**Driver reaction.** Entering the driver seat does not by itself give the car a directive. The first
directive comes from the next AI native or from the default ambient mode when the vehicle is spawned as
traffic. For a mission vehicle without a directive the car sits (engine on) until a native runs.

### 22.8 Helicopter fly-to and follow (PARTIAL)

Heli AI record (found through `0x00a79470`): `+0x298` flags (bit 0 hold/hover, bit 1 stop request, bit 2
script speed in `+0x43c`, bit 3 follow object, bit 4 hover at last point, bit 5 pending request), `+0x299`
state bits (bit 0 ring, bit 1 done, bit 2 brake), `+0x29a` count, `+0x29b` index, `+0x29c` points
(12 bytes each, up to 25), `+0x4b4` phase (1 = fly, 2 = hover/landing-style). Tick `0x00b405b0`.
- Phase 1: speed = `+0x43c` if bit 2, else **45.0** (`0x11169a8`). Waypoint steering is the shared steer
  helper `0x00b38f10(vehicle, 15.0 look distance, speed, height request, ...)`; a node is passed when the
  helper's distance-like result is below **0.2** (`0x12a2e30`).
- Follow (`+0x298` bit 3): the follow target `+0x440/+0x444`, trail distance `+0x448`. If the target is within
  **20.0** (`0x1116990`), speed = clamp of the closing-rate term (negative closing: closing * -0.25;
  positive: closing * 2.0; minimum 1.0); beyond it speed is limited to the target's speed (minimum 1.0).
- Arrival: `index >= count` (or `count - 1` with hover-at-last-point) sets `+0x299` bit 1 and completes the
  request (`0x006f66d0`, `+0x458`). With hover, the helicopter holds with the stop helper (`0x00b35130`,
  `0x00b35aa0`) or a drift target 45.0 along the heading scaled by `0x00b35a80`.
No ramming or chase of vehicles by helicopters was found in this unit; helicopter combat is a separate
module (`0x00b3xxxx`) not opened here.

### 22.9 Test vectors

Notation: a_b = vehicle `+0xc58` (AI_Max_Braking_Decel scaled by condition; use 8.0 m/s^2 in the
examples), speeds in m/s, gaps in metres from the own look-ahead point to the target point.

**TV1: chase, target 25 m/s straight, gap 30 m, own speed 25.**
- g = 30, s = 4.0 (neither a tank): far case.
- v_cmd = 25 + sqrt(1.2 * 8 * (30 - 4)) = 25 + sqrt(249.6) = 40.80 m/s (m = 1; no flank).
- v_own (25) < v_cmd: decel = 0, brake = 0. Throttle: vt = 40.80, k = (40.8/5)^2 = 66.6; throttle =
  66.6 * (1 - 625 / 1664.6) = 41.6, clamped to 1.0. Steering: route follow to the target's lane point.
- Steady state: the gap closes until g = 4 where v_cmd = 25.0 (matched). At g = 2: v_cmd = 25 - 5 * (1 - 0.5)
  = 22.5. At g = 0: 20.0. At g < 0 with `+0xe08` bit 6: v_cmd = 0 and brake = min(1, a_b / a_b) = 1.
- With a flank manoeuvre active (`+0xe20` = 1 or 2): v_cmd = 25 + sqrt(2 * 1.2 * 8 * 26) = 25 + 22.34 = 47.34.
**TV2: chase braking.** v_own = 40, v_t = 25, g = 10: excess 6; v_cmd = 25 + sqrt(57.6) = 32.59 < 40, so
decel = min(8, (1600 - 625) / 12 = 81.25) = 8.0, brake = min(1, 8/8) = 1.0, throttle = 0.001.
**TV3: chase end.** Player target within 10 m (squared distance 99.9) with the driver's enemy = the player and
not co-op: hand-off, no steering this tick. At 100.0 exactly the chase continues (comparison is `<`).
**TV4: drive-to arrival (`vehicle_pathfind_to_do`, stopAtGoal false).** Goal 3.0 m radius: completes when the
X/Z distance is below 3.0 (3.0 does not complete). With stopAtGoal true, v = 10 m/s, a_b = 8: braking starts
when d < 100 / 8 + 3 = 15.5 m; completion when speed < 1.0.
**TV5: waypoint directive.** Nodes A, B (last), mode 0, stop-at-end false: A passes when
`(A - p) . (B - A) <= 0`; the last node B completes when the squared X/Z distance is <= 16.0 (4 m); with
stop-at-end true the radius is 2 m (4.0 squared).
**TV6: seat record over a scripted driver entry (seat 0, instant false).**
t=0 submit: `+0x16d4`=1, claim empty; polls: `character_is_in_vehicle` false, enter check not done,
`is_in_a_driver_seat` false. t <= 250 ms (first upkeep): hub: claim = driver, `+0x16c8` = 0; polls: driver
seat TRUE, in-vehicle false. During the animation (clip length of `Enter Left Driver`): unchanged. At the
commit frame: `+0x18` = driver, claim = next claim or empty, `on_vehicle_enter(name, vehicle, 0)` posted,
`+0x16d4` = 3: all three polls true. Passenger entering seat 1 meanwhile: seat 1 record only; seat 0 queries
stay as they were.
**TV7: instant group.** `vehicle_enter_group_do(v, true, {A, B, C})`: A -> seat 0, B -> seat 1, C -> seat 2
(search mode by table position); all three commit on the next upkeep/engine pass; `group_check_done` true as
soon as all three have `+0x16d4` = 3 for this vehicle.
**TV8: helicopter fly-to.** Three waypoints, no script speed: speed 45.0; node passes at helper result < 0.2;
after the third node `done` bit set and request completed; with hover-at-last-point it completes at
index >= 2.

### 22.10 Port recipe and OPEN

1. Keep the 17-mode directive table; implement modes 4 and 7 beside mode 8. Mode 4: planner every 300-450
   ms (2000-2500 ms when the target stands), speed law of 22.2, stand-off 4.0, throttle cap 1.0, route
   follow with the 15 m direct-steer, hand-off at 10 m for player targets, end on target death.
2. Drive-to: route the lane graph from the nearest lane to the nearest lane of the last point, complete
   with the move-to radii of 22.3; never fail, only finish.
3. Seat records: three handles per seat, written by the claim, exit-pending and commit steps of 22.7.
4. OPEN: sub-states `0x0f`/`0x10` of chase beyond the entry conditions; the role of `0x00b60cf0`,
   `0x00b5fe80` and mode 5; the source vector in the drive-by speed match; the default values of directive
   `+0x528`/`+0x52c`/`+0x530`; the helicopter phase-2 branch; heli combat; the exact lookahead bias
   `0x00b47400`; `0x00a91070` internals (passenger/turret attack hand-off).
   **[2026-10-15 closed: PARTIAL for this list; details at the matching notes in 22.2.
   (a) `0x005f5f80` = store/garage state `0x014a1ce4` is in {1,2,4}: FINAL.
   (b) The flank chooser `0x00b4a180` (draw 0..2; 3/4 = pending side switch) and sub-states 0x0f (wait for
   the crew) and 0x10 (post-recovery hold) are FINAL. The 0x0d/0x0e predicates `0x00b47f10`/`0x00b47730` are
   still OPEN.
   (c) `0x00b60cf0` = strike-opportunity test, `0x00b5fe80` = commit to mode 9 sub 0x0b (ram), FINAL. Mode 5
   = lane-follower obstacle bypass, not chase, HIGH.
   (d) `0x00b47600` = class-based first-ram delay, 9000-10000 / 6000-7000 / 3000-4000 / 1000 ms, FINAL.
   Still OPEN: `0x00b65e80` (the 15% far-target manoeuvre), the writer of mode-9 sub 0x0c, the lead-time
   clamp in `0x00b61160`, and the meaning of the per-team class byte at `0x014a4f70 + (team-1) * 0x4c`
   (table data; its loader was not traced).]**

### 22.11 Scripted NPC move completion and follower rules (stall clusters K2 and K4)

Status: FINAL for the order/ticket/finish contract (restated from 21.5 and the Lua spec), PARTIAL for the
arrival test, because the mode behind action 24 `move scripted` has a null goal routine (21.1 table) and its
tick was not decoded here.

1. **`ai_do_scripted_move(char, nav, run, fire)`** (Lua spec 3.11) resolves the destination as a handle pair
   (class literal `0x031d152c`, registry `0x024433a8`), gates it (`0x00853b10` liveness and three `0x00853b30`
   queries), and stages action 24 through `0x004f4d00` with the handle pair copied to `+0x4b0/+0x4b4`. It
   returns true whenever the character resolved, even when nothing was issued; scripts must not read the
   return as success.
2. **Completion** is the ticket rule of 21.5: the mode calls the finish routine `0x004f4f50`, which (bit 3 set,
   staged id equal, category mode equal) completes the ticket and returns control to the goal layer.
   `ai_scripted_action_is_complete` reads the ticket: pending only while status is 0.
3. **Scripts that do not use the ticket** poll geometry: sh03 waits for `get_dist(KINZIE, tower) < 2.0`, and
   re-issues the move when the distance exceeds 2.0. So the port must bring the NPC to within **2.0 m** of the
   nav point, not merely finish the order.
4. **[2026-10-15 closed: the radius is the engine's 1.414 m box-distance test, not the CHOSEN 2.0 m; see the
   closing note at the end of this section.]** ~~**Port recipe (CHOSEN for the arrival radius):**~~ old text: move along the navmesh toward the nav point at walk or run
   speed per the `run` flag; complete the ticket when within 2.0 m (the tightest radius any script tests); if
   the mode is replaced by a reactive mode (Flee), keep the ticket open as in the 21.5 failure modes.
5. **Followers and homies.** A follower copies its player's target every 2000 ms (21.2). It follows the player on
   foot through the group-leader goal (`group`, goal 5 in 9.6). When the player's vehicle seat state reaches
   seated, followers enter through `vehicle_enter_do` / `vehicle_enter_group_do` (22.7) or the `Veh passenger`
   mode; seat order is by table position, driver first. Test vector: dlc3_m02 truck: after
   `vehicle_enter_group_do` with N entries and instant 0, `character_is_in_vehicle(player, truck)` becomes true
   only when the player's own seat state is 3; NPC entries do not satisfy the player test.
6. **Scripted death scene (m24)**: `m24_homie_death_thread` ends when the staged action and the animation finish;
   treat as an action with its own ticket and the animation length from the data.

OPEN: ~~the arrival radius and tick of mode 24~~ (closed below); the animation length of scripted action
scenes (data driven, `spec-anim-runtime.md`).

**[2026-10-15 closed: action 24 `move scripted` tick, arrival, completion, failure. FINAL for the test and
the branches; HIGH for the steering call. Static, instruction stream only. This replaces the CHOSEN 2.0 m
of items 3 and 4.]**
- **Owner.** The null goal routine is irrelevant: action 24's category mode is 5 `Combat Movement`. Its tick
  `0x0053fe30` skips when `+0x44c` bit 0 (finished) is set, otherwise indexes a byte table
  (`0x0053ff68`, index = action - 14, 54 entries) into 18 targets (`0x0053ff20`); action 24 maps to entry
  10, which calls **`0x0053e600(human)`** every update. (Entries 8..9 and 11..17 are the other actions of
  the mode.)
- **Tick `0x0053e600`, in order (per update):**
  1. Resolve the destination handle pair at `+0x458/+0x45c` (committed copy of staged `+0x4b0/+0x4b4`)
     with `0x00458230` (registry `0x024433a8`, class literal `0x031d152c`). Valid when found, `+0x33`
     bit 4 clear, class descriptor (`0x02cc9900[+0x34]`) byte `+6` bit 3 (value 8) set, and not dead
     (`0x00853b10`). **Arrival test A (any pose):** take the human position, subtract the target's position
     and rotate it into the target's local frame (target position `+0x40`, orientation `+0x4c`), fetch the
     target's local box through its vtable slot `+0x60` (min, max), clamp the local point into that box,
     and compare the **squared** 3D distance from the point to its clamp with the constant at
     `0x01118ce0` = **2.0**. Below 2.0 (all four lanes) = arrived: call `0x004f4f50` and return.
     For a point-like nav object (zero-size box) that is a sphere of radius sqrt(2) = **1.414 m**; for a
     sized box it is the distance to the box surface.
  2. Otherwise, if the human's pose byte `+0xccd` is 2 or 3 (`0x0098da50`): keep moving. Call
     `0x00539170(human, flagA, flagB)` with flagA = `+0x4a5` bit 2 (Lua arg 3) and flagB = bit 3 (Lua arg
     4), then, if either is set, `0x00943960(human, 1, 500)` (raises a 500 ms-based upkeep timer at
     `+0x1390` if it is lower). `0x00539170` sets the locomotion request byte `+0xb35` (1, or 2 plus
     `+0x2bc` bit 7 for the fire variant) and, unless the human is in the `+0x2b8` bit 4 state or `+0xccc`
     is above 1, calls `0x004ea460`, which hands the point stored at `+0x1500` to the locomotion layer
     (`0x004e86e0`). The tick itself does no path planning and no end test in this branch. HIGH: the
     producer of `+0x1500` was not traced.
  3. Otherwise (pose not 2 or 3, i.e. standing still): **test B**: the stored point at `+0x468/+0x46c/+0x470`
     (staged `+0x4c0..+0x4c8`); horizontal (x, z) squared distance (`0x00d9fc80`, no root) below 4.0
     (2.0 m) **and** |stored y - human y| below 2.0 (a double compare against `0x01187ebc` = 2.0) = arrived:
     finish as in step 1. The stored point is filled by `ai_do_scripted_move` from the three floats at
     `0x029cdb98/9c/a0`, which are the shared zero vector (referenced by dozens of sites), so test B
     compares against the world origin and is **effectively never true**; treat it as dead.
  4. Otherwise: call `0x004f50b0(human, 0)` (the give-up end, see below).
- **Completion** (arrived): `0x004f4f50` as in 4.1 and 21.5: bit 3 set, staged id 24 equal, mode equal
  gives bit 2 set, bits 3 and 4 clear, ticket `+0x4aa` completed through `0x006f66d0`, staged block reset,
  current action = none (0x47), and the keyed `0x14` wake record signalled (wakes a thread waiting on the
  order). `ai_scripted_action_is_complete` then reads done.
- **Failure and timeout.** There is **no timer in this tick**: a human that keeps pose 2 or 3 moves for
  ever. The give-up case is "standing still, not within A, B false": `0x004f50b0(human, 0)` raises the
  action's repeat timer to at least 0, sets `+0x44c` bit 0 (finished), and does **not** complete the ticket
  (the ordered bit 3 and staged block remain, ticket status stays 0 = pending). The tick is then
  suppressed and the decision step runs (4.3); `ai_scripted_action_is_complete` stays false until another
  order replaces the ticket (a new kind-3 request frees the old one). A port must therefore not invent a
  timeout; scripts that need one (sh03 re-issue when farther than 2.0 m) poll geometry themselves.
- **Port rule (replaces item 4's CHOSEN radius):** while the destination object resolves and is alive,
  complete the ticket on the first update where the squared distance from the human to the object's local
  box (point objects: its position) is below 2.0 (1.414 m), regardless of pose. If it is not reached and
  the human has stopped (not walking or running), end without completing. A destination that fails the
  gate or is dead skips test A and ends the same way. This also satisfies sh03's `get_dist < 2.0`.
  Note the first check happens on the first update after staging, so a human already inside 1.414 m
  completes in one update.
- **Test vectors.** (a) point nav object at (10, 0, 0), human at (8.7, 0, 0): squared 1.69 < 2.0 completes;
  human at (8.5, 0, 0): squared 2.25 does not. (b) box target, local box x 0..4, y 0..2, z 0..4, local
  human point (6, 1, 2): clamp (4, 1, 2), squared 4.0, not arrived; point (5, 1, 2): squared 1.0, arrived.
  (c) human stopped 5 m away, pose not 2 or 3: finished bit set, `ai_scripted_action_is_complete` false.

---

## Changelog

- **2026-10-15 - 21.8-21.10, 22.11: mission stall map** (SPEC TEAM). 32 of 49 missions stall at a Lua wait;
  clustered by behaviour, with the spec section per cluster; engagement/kill-all recipe and scripted-move
  completion added.

- **2026-10-15 - §22: mission-driving AI** (SPEC TEAM). Chase (mode 4) planner cadence, target-relative speed law (stand-off 4.0 m, +5 m/s per stand-off), 10 m player hand-off; drive-to arrival radii (3.0 m move-to, 4 m / 2 m waypoint); seat-record claim/pending/seated sequence and per-stage predicate reads; heli fly-to basics. Static analysis only.

- **2026-10-15 - §21: combat and NPC behaviour end to end** (SPEC TEAM). Decision loop, target selection
  (no scoring chooser; event 77/78 handlers), cover search and timers, flee/cower chooser (0.8/0.5 flee
  probability, 30 m stop radius), human shield grab gates, native-order override and completion, five
  test vectors. Notoriety shown not to be an AI input.

- **2026-10-14 — §20.6.7–20.6.12: population tails** (SPEC TEAM). Car-following law (`0x00b51950`,
  5 m stop gap, ≈ 11 m steady gap, 0.75/0.5 hysteresis) and obstacle probe; `0x00b5e6c0` re-identified
  as the junction yield; lane changes as 6 m route-search edges; hood → `sp_cat_*` from the `Hood`
  objects of `sr3_city~fawld.czn_pc` with their BSP volumes (tile 1215 → `sp_cat_HenryMills`, 1517 →
  `sp_cat_Ashwood`); ped speeds shown to be animation-driven; sidewalk crossing gate; notoriety
  roadblocks (135 m sites, 270 m despawn, level-1 never, shipped composition); obstruction points;
  stop-line geometry. Corrects §20.6.1(b) (`0x00b2db20` is a stopped-traffic count) and §20.6.3(c)
  (waiting bit). Team B wiring rows updated in §20.6.6.

- **2026-10-14 — new §20.6: free-roam population constants** (SPEC TEAM). Read from the executable:
  - the near-field vehicle and ped target formulas (TOD segment index, notoriety and weather scales);
  - spawn rings, cones and rejects, and the despawn radii;
  - the vehicle and ped model and group rolls;
  - the lane follower's cruise source (every shipped road is 10 m/s), throttle law and signal stop
    rule.

  Corrects §20.1 (90 m is the far, not the near, reject) and §17.4c (3 attempts, not a 3-slot buffer;
  `0x00910940` is the target count). Test vectors and Team B wiring are in §20.6.4/§20.6.6.

- **2026-10-11 — §19.1: "what the save stores vs. what's recomputed" for notoriety, orchestrator-
  routed, synthesis only (no new RE)** (SPEC TEAM, self). Connected this section's own live-record
  field map directly to `spec-save-format.md` §10.6's already-published weapon/notoriety scalar
  rows: only the 4 per-track decay-rate multipliers are saved; the actual level/fraction, decay
  timer, bound stacks, suppressed-flags, and alert-registry all reset every session, confirmed by
  their absence from the save format's own near-complete field map. Also cross-referenced two
  related-but-separate saved items (the 5 time-at-max-notoriety stats, the "STAG notoriety" script
  toggle) to avoid future confusion with the 4-track system. Cleanroom self-check: strict regex
  0 hits, C-cast-dereference check 0 hits.

- **2026-10-09 — §19.7's spawn-production gap CLOSED: who calls the roadblock allocator, and whether it's
  notoriety-gated** (SPEC TEAM, agent `a4fa47508ae3a24bd`, re-run after infrastructure loss — report
  found complete in scratch, spliced from the finished file). Answer: both — of 6 named call paths
  reaching the allocator (`0x00930950`), exactly one (`0x0060a8d0`, a dedicated per-frame scheduler,
  sibling to the already-documented roadblock-consumption tick under the same `0x0090faf0` dispatcher)
  reads the live per-track notoriety level (`0x00605200`, requiring it `≥1`) and cross-references
  `roadblock_notoriety.xtbl`'s own grid, byte-exact against this project's published schema; the other 5
  are scripted/placement-triggered (two STAG-lockdown drivers, a named spawn-point trigger, a trigger-
  volume think-method, and a second, previously-undocumented "roadblock"-named subsystem whose own real
  trigger stays OPEN) and bypass notoriety entirely. Exhaustive literal-reference search proves the
  3-external-reader set at the class-id table slot is complete, not a sample. Full detail:
  `scratchpad/notoriety_roadblock_spawn_caller_2026-10-09.md`. Cleanroom self-check: strict regex 0 hits,
  C-cast-dereference check 0 hits.

- **2026-10-09 — new §20: on-foot ambient pedestrian runtime — spawn, path-following, idle behaviour,
  and the gunfire-to-flee trigger chain, all traced end to end** (SPEC TEAM, agents `a8d92819c0b386b77`
  + nested `a9647422c65e6f757`, wave 6 item 2). **Spawn (§20.1):** a genuinely separate near-field
  pedestrian spawn subsystem found and traced in full, from the per-frame dispatcher `0x0090faf0` to the
  literal human-creation call `0x00bb9070` — closes §17.3's open promotion question for pedestrians the
  same way §17.4c closed it for vehicles. Surprising, directly-checked negative: the creation routine
  never calls any of the 4 sidewalk/wander force-flag setters. **Walking/pathing (§20.2):** confirmed
  sidewalk and wander are `Idle`-mode sub-behaviours, not separate modes; opened a previously entirely
  unread ~28-function Sidewalks query/steering cluster; closes the "human navigator chain" cross-
  reference (§14.2) and event kind 81's producer (§8 item 2). **Idle behaviour (§20.3):** confirmed
  mostly data-driven via `action_node_npcs.xtbl`, runtime reader honestly left open. **Gunfire-to-flee
  (§20.4):** confirmed and precisely mechanized — the real discriminator is team byte `+0x1ca8`==7
  (civilian), `Panic` guarded against but never requested. Corrections applied in place: §17.6 (`"os_ped"`
  is polled directly, not inert despite no callback); §9.9 (real consumer census for all 4 sidewalk/
  wander force flags); §2.5 (7 new sub-state entries); §8 item 2 (kind 81 closed, kinds 14/38/48/67
  opened). Full detail: `scratchpad/ambient_pedestrian_onfoot_behavior_2026-10-09.md` (parts b/c/d; part
  a delivered directly via hand-back, fully absorbed into §20.1). Cleanroom self-check: strict regex 0
  hits, C-cast-dereference check 0 hits.

- **2026-10-08 — new §19.7: the literal police/roadblock spawn instruction, found** (SPEC TEAM, agent
  `ad2c811107eef4dac`). A third, previously-undocumented per-frame population tick
  (`0x00930e20`→`0x00930db0`→`0x00930a40`→`0x0092ff20`), sibling to (not downstream of) `0x00606b80`'s
  own notoriety dispatcher, with a literal position formula (anchor near a tracked reference position
  plus a configured road-direction offset) and 3 type-specific creation dispatches (character/vehicle/
  third-category), including a literal `"Spike_strip"` name check. Closes §19.6's "literal spawn
  instruction" and "roadblock toggle connects to `0x00606b80`" items; corrects §19.4's `0x008addb0`
  misattribution (the real `+0x455` setter is `0x008ecda0`) and its Handler-3/`0x0092ed10` framing;
  narrows §19.5's own OPEN item. Cross-references added to §15.7 (second `0x008cd1d0` consumer) and
  `spec-tables-traffic-ai.md` §7 (second `0x02442750` consumer). Full detail:
  `scratchpad/notoriety_police_spawn_instruction_2026-10-08.md`. Cleanroom self-check: strict regex 0
  hits, C-cast-dereference check 0 hits.

- **2026-10-09 — §19.4 corrected: `0x00606b80` branches on the sign of the new level, and a
  previously-undocumented 4th writer of the team-relationship matrix found; §19.7's own "who creates a
  roadblock-manager instance" item narrowed to the exact allocation primitive** (SPEC TEAM, agent
  `a961a3cb913d385d8`, wave 6 item 4, mostly a synthesis pass over already-solid existing material —
  §6.1/§6.2's crimes/points and decay-timing tables both re-confirmed unchanged). §19.4's own
  description of the roster/player/timer-rearm sequence corrected from "runs every call" to "runs only
  on the `newLevel<=0` clear branch"; the heat-code table corrected from level-indexed to track-indexed;
  `0x004f6540` corrected from "heat/difficulty setter" to a generic AI known-hostiles state-rewrite pass.
  **New finding: `0x00606b80` is a 4th writer of the already-documented team-relationship matrix
  (§12.7, `0x0262d940`, via `0x009b7130`)** — writes HOSTILE on `newLevel>0`, UNFRIENDLY on
  `newLevel<=0`, a binary (not graduated) trigger that is the real, confirmed mechanism for "which
  levels make existing nearby police/gang NPCs actually hostile." §19.7's own spawn-production gap
  narrowed further: the `"roadblock"` class descriptor's global address (`0x026171d0`), its full 9-slot
  vtable, and its embedded 32×1008-byte free-list pool are now named — vtable slot 0 (`0x00930950`) is
  the real creation primitive; only its own caller (gated on live notoriety level or not) remains
  unfound. Full detail: `scratchpad/notoriety_rules_synthesis_2026-10-09.md`. Cleanroom self-check:
  strict regex 0 hits, C-cast-dereference check 0 hits.

- **2026-10-08 — new §17.4c: the near-field (fully-simulated) ambient-vehicle population, resolving a
  long-standing OPEN item; correction to §17.5's "pool reserve/cap" framing** (SPEC TEAM, agent
  `a65b1c1a745e444d3`). Found the per-frame tick (`0x0090f2c0`→`0x0090eaf0`/`0x00906b00`) that applies
  the `vehicle_despawn.xtbl` schedule and confirmed the runtime consumer of
  `player_vehicle_despawn_distance`; concrete 48-vehicle/150-human caps. **Correction**: the three
  "pool reserve/cap" bootstrap calls in §17.5 (`0x009069e0`/`0x009142a0`/`0x00917d10`) are timer-arm
  calls, not pool-capacity setters — their shared callee is this project's generic countdown-timer
  family, not a pool allocator; `0x009069e0` re-attributed from "pedestrian pool" to near-field
  *vehicle* pool (HIGH CONFIDENCE). Also corrects §19.4's `0x008addb0` human-exclusivity claim (confirmed
  it also gates vehicles). Promotion/demotion boundary between distant and near-field tiers: honest
  negative result, no hand-off found. Full detail:
  `scratchpad/ped_traffic_density_radius_2026-10-08.md`. Cleanroom self-check: strict regex 0 hits,
  C-cast-dereference check 0 hits.

- **2026-10-08 — §14.7b un-parked: a direct live hook on `0x00447160` fired 16 times in a fresh capture,
  and a third independent static re-check (one level up from the known caller) still found zero second
  path.** The navmesh-registry presence-bit contradiction is now live-confirmed at the most direct
  possible level (the writer function itself, not just its downstream reader) and remains statically
  unexplained after three full passes. Flagged the most plausible untested candidate (post-load in-memory
  gate mutation) without guessing it as fact. Frida script updated (backtrace + arg dump) and ready, not
  run. Full detail: `scratchpad/navmesh_registry_caller_recheck_2026-10-08.md`. Cleanroom self-check: 0
  new hits. Synced to Team B.

- **2026-10-08 — new §19: notoriety tracking and police-response dispatch; clean-room check renumbered
  to §20.** Opens the live per-track notoriety record (`0x014A4F70`, 4 slots) and its add-points/decay-
  tick/level-change-dispatch chain at the runtime level for the first time — previously only the XTBL
  loader side (`spec-tables-progression.md` §6, `spec-tables-traffic-ai.md` §6) was documented. Confirms
  `Check_Detection` is real live-detection gating, not just a field-name gloss; identifies a new "gain
  suppressed" flag sibling to the already-documented "decay suppressed" one; resolves
  `spec-tables-progression.md` §6.2's own flagged OPEN item (which `NotorietyLevels` set is police vs
  gang). Police-response spawn is confirmed structurally separate from the ambient-population distant-
  tick system (§17) but shares the Traffic zone-data singleton and the `0x00d9e140`/`0x00d9e1e0` timer-
  primitive family with it and with §18's traffic-AI road-network module — a genuinely new cross-system
  link. Honest gaps left open: the literal police-vehicle/roadblock spawn instruction, and
  `roadblock_notoriety.xtbl`/`notoriety_spawn.xtbl`'s runtime consumers (exhaustive scans found neither).
  Full detail: `scratchpad/notoriety_police_response_2026-10-08.md`. Cleanroom self-check: 0 hits. Synced
  to Team B.

- **2026-10-08 — new §18: Traffic AI lane-following consumer module, pool mechanics, Havok integration;
  clean-room check renumbered to §19.** Companion to the same-day `spec-zone-data-format.md` §13.2 data-
  format splice (the `0x2236` Traffic record layout). Traces the consumer module as far as this pass's
  budget allowed: lane-following is structurally closer to graph/segment-hopping over the record's own
  segment→point graph than to a continuous analytic spline-with-cross-track-error formula, but the actual
  steering-decision function was not reached (honest OPEN, not guessed); a 2-bit segment-"kind" field
  exists and at least one consumer branches on it (consistent with junction/intersection handling), but
  the branch-choice algorithm itself was not found; and the Havok integration argument is HIGH confidence
  structural (there is exactly ONE RTTI class, `hk_custom_vehicle_driver_input`/`0x00ae30c0`, that can
  feed Havok's vehicle step, re-disassembled in full to pin all 6 fields it reads) but the literal
  write-site inside the traffic module for those fields was not located (a disp32 byte-scan for `0x128`
  found only false-positive stack-frame-local matches, reported as an honest negative). Full detail:
  `scratchpad/traffic_ai_lane_following_2026-10-08.md`. Cleanroom self-check: 0 hits. Synced to Team B.

- **2026-10-08 — new §17: ambient population runtime (distant pedestrian/vehicle spawn-despawn);
  clean-room check renumbered to §18.** Full trace of the distant-LOD ped/vehicle population: a flat
  hardcoded 75-entity target per kind (not per-zone/per-district); spawn candidates drawn dynamically
  from the already-documented Traffic/Sidewalks zone-data graphs (`spec-zone-data-format.md` §13.2, now
  confirmed to have a real runtime consumer — previously "load-time only"), filtered by camera-
  lookahead-predicted position and (vehicles only) a `Spline_Type` one-hot bitmask eligibility test
  against `distant_vehicle_traffic_types.xtbl` (`spec-tables-traffic-ai.md` §14.4); despawn via a
  distance/angle cull against the same predicted position, plus a whole-category mass-despawn path;
  driven by a single per-frame dispatcher (`0x0090faf0`, called from the same per-frame world update
  §1.1 already documents) that unconditionally ticks both populations every frame, each internally
  throttled to its own heavy pass roughly once per second. **Corrects `spec-zone-data-format.md` §13.4**:
  `0x02609394` and its 5 sibling `"os_*"` toggle-category arrays are 8 slots each (confirmed directly
  from the query/set accessor functions), not "capped 0–18" as previously claimed. Full detail:
  `scratchpad/ambient_population_spawn_despawn_2026-10-08.md`. Cleanroom self-check: 0 new strict-regex
  or C-cast-dereference hits from this section. Synced to Team B.

- **2026-10-07 — A4 appended as §12 (perception's sensing primitives); the clean-room check renumbered
  to §13.**
  - Corrected the scoping note's framing: the `0x00507520` subtree is not a further ~10k-instruction
    body A2 left unopened. A2 already read the tick and most of its producers; A4's real scope was the
    specific primitives A2's own text flagged as not traced.
  - The perception tunables block identified as ordinary `tweak_table.xtbl` entries, with real shipped
    names and values for all 14 `Ai_fov_*` tunables plus `Pain_sound_radius`/`Grenade_shout_radius`.
  - The view-cone/FOV test `0x004fef80` read in full: five context-dependent tunable sets (normal,
    spotted, zombie, cover, carried/witnessed), validated against the real base `tweak_table.xtbl`.
  - Two sound-radius producers A2 had found but not decoded (lists 10 and 12) read in full, including
    two previously uncatalogued event kinds (56, 300) and the voice line "call a doctor".
  - The line-of-sight service's answer-decode and request-queue halves read in full, with the
    underlying raycast execution left as an explicit physics-domain boundary.
  - The target-refresh pass's "choose the target" step corrected to a target-*validity* re-check.
  - The slow pass, the team-relationship read accessor (citing the already-specified Lua write side),
    and the ally-link cylinder-distance test all read in full, closing the remaining "not read"/"not
    decoded" asides of §10.2/§10.3/§10.6 and most of §8 item 12's "still open inside A2" clause.

- **2026-10-07 — A5 appended as §13 (begin-function gating and thrown-weapon trajectory solvers); the
  clean-room check renumbered to §14.** OPEN items 24-31 appended.
  - §3.2's "Begin function" row closed for 7 of its 13 functions: `Cower`, both bodies of `Flee`,
    `Gunfire wary`'s two-call entry, `Passenger`, `RollerBlader`, `Veh evade`, and `Veh threat`'s own
    `0x00523db0`. A shared two-test gate (null-check, class-descriptor bit) opens all 7; a shared
    network-forward convention on not-owned (building and sending message type `0x1b`) is confirmed as
    general across begin functions, not specific to the one case §3.2 had already shown it from —
    `RollerBlader` is the one exception, declining outright with no forward.
  - `Flee`'s real structure resolved as a multi-candidate escape-point search (`0x004da0b0`) that can
    itself call its own "already have a point" sub-routine (`0x004d91a0`, separately callable by apply)
    or redirect into `Cower` entirely before ever reaching its own transition call — a concrete,
    disassembly-confirmed instance of a begin function entering a different mode than the one it was
    asked to enter.
  - `Gunfire wary`'s entry hook corrected: `0x004e8070` never itself transitions into mode 17 — for a
    player human it instead stages a request for `Player` and never reaches `Gunfire wary` at all; the
    real mode-17 entry is entirely inside the second call, `0x004dbd90`.
  - §11.3 part H's two undecoded trajectory solvers (`throw weapon`/`throw grenade`) resolved as two
    thin callers of one shared ballistic-angle core, `0x0054c630` — confirmed both statically (full
    listing) and empirically, via an 11-vector Unicorn-emulation oracle run directly against the real
    function bytes (zero stubs needed for the core itself; one disclosed stub only for the outer
    `0x0054c770` shim's dependence on a runtime-populated class table). Grenade's solver additionally
    confirmed to perform its own SIMD per-candidate ally/obstacle proximity reject before ever reaching
    the shared core — the concrete mechanism behind §11.3 H's prose "it collects allies to avoid."
  - No other spec file edited by this unit; no `.czn_pc` data touched; no leaked Volition/Havok/
    Audiokinetic/RAD material read. Ghidra-free (capstone over `exe_va.py`, plus `unicorn_call.py` for
    the oracle runs). Cleanroom self-check (word-bounded regex + plain-substring backstop) on the entire
    finished file: **0/0**, unchanged from the A1-A4 baseline.
- **2026-10-08 — §15 added: the real Navpoint/`cover_node` movement-target mechanism, direct follow-up
  to §14's dead-navmesh finding.** Traces one concrete, fully-read consumer end to end
  (`0x00b428d0`→`0x00b3e180`): neither class carries on-disk adjacency data; Navpoints self-register
  into up to four flat global arrays by `navpoint_flags` bits; the real destination-selection algorithm
  is a flat linear scan with a two-radius annulus filter and a uniform-random pick, not a graph search
  or nearest-neighbor — confirming the standing "spatial proximity at runtime" hypothesis directly, not
  merely leaving it standing. `cover_node`'s real property set is 8 properties (5 more than previously
  named), two of which (`cover_type`/`cover_index`) are confirmed present in zero of 12,956 real shipped
  objects — a population-confirmed negative. Confirms action 16 `move retreat`'s dead-navmesh failure
  (§14.4) is unrelated to and disjoint from this mechanism. Corrects a prior pass's working label (the
  per-class descriptor vtable's slot `+0x04` is Destroy/Release, not a tail-reader — propagated to
  `spec-zone-data-format.md` §11 separately). Clean-room check renumbered §15→§16. Ghidra-free
  (capstone over `exe_va.py`, plus the project's existing `czn_u2_replay.py` extraction primitives for
  the population census); no leaked material read. Cleanroom self-check: 0/0, unchanged from the
  A1-A5 baseline. Synced to Team B.

- **2026-10-08 — new §15.7: the real `cover_node` selection algorithm, direct follow-up closing §15.6's
  own standing OPEN item** (SPEC TEAM, agent `ab8974ffda1933fc1`, requested so Team B could implement
  cover-selection, previously left unimplemented for lack of a documented mechanism). The Navpoint-
  selector cluster (`0x00b428d0`/`0x00b3e180`) is confirmed a dead end for this question — a fresh
  thread through action 8 `cover take def` finds the real chain (`0x00543480`→`0x00542bc0`→
  `0x005429b0`→`0x0050a690`). `0x0050a690` (1,282 instructions, read in full): brute-force linear scan
  of the live `cover_node` object pool inside an AABB (14.0 base radius, widened for brutes/post-area/
  group-leader), filtered by failure-memory/open-flag/the arc evaluator, SCORED (arc/exposure + path-
  reachability + group-cohesion, clamped [0,100]), and resolved via the same multi-destination navmesh
  path query action 45 already uses — picking the cheapest path-reachable candidate, not nearest or
  random. **Genuinely different selection philosophy from Navpoint's own annulus-filtered uniform-
  random pick (§15.3)** — not two variations on one theme. The arc test (`0x008cd1d0`, read in full)
  confirms `cover_angle_left`/`_right`/`cover_def_angle_left`/`_right` (§15.1) ARE respected exactly as
  their names suggest, against the cover's own generic world-placement yaw — NOT `cover_orient`, which
  narrows but doesn't resolve that property's own role. Resolves this document's own previously-
  unmatched 4th quantize-offset (`cover_def_angle_right`@`+0x83`). A hard player-shield gate
  disqualifies a candidate independent of score. `cover_orient`'s 6-way jump table, the enum tables,
  `navpoint_sync`, and the `navpoint_flags` bit `0x10` remain OPEN (out of scope by the task's own
  framing, not chased). Full detail: `scratchpad/covernode_selection_2026-10-08.md`. Cleanroom
  self-check: 0 strict hits, unchanged.

- **2026-10-08 — CORRECTION to §15.7/§11.3 F, new §14.7** (SPEC TEAM, agent `a1e1827309ece527b`,
  dispatched to settle a real tension `purrsian-62` flagged before Team B implemented against §15.7).
  §15.7's own framing of `0x00a12a80` ("picks the cheapest path-reachable candidate") does not survive
  direct disassembly: `0x00a12a80` is a third direct caller of §14.3's dead registry (`0x00447770`)
  and returns `false` unconditionally in every shipped file, before it ever inspects a candidate.
  Generalizes §14.4's `move retreat` dead end to two more actions, by the identical root cause: action 8
  `cover take def` (§15.7) and action 45 `pickup weapon` (§11.3 F, doubly dead — its own per-candidate
  `0x00a12180` snap already empties the candidate array upstream). New §14.7 records the expanded
  blast radius and a naming trap (`0x3171a58`/`0x3171a5c` dead vs. the adjacent, unrelated, live
  `0x3171a64`). **Per `purrsian-62`'s explicit follow-up, all three dependent claims are labeled
  STATICALLY DEAD, RUNTIME CONFIRMATION PENDING, not hard-confirmed-dead** — the static trace is clean
  and exhaustive, but a live Frida hook on `0x00447770` during real combat (capture sheet:
  `scratchpad/live-capture-navmesh-registry_2026-10-08.md`) has not yet ruled out a runtime-only
  registry-population path the static census could have missed; Team B should implement behind a
  switch until that lands. Full trace: `scratchpad/action45_covernode_pathquery_reconcile_2026-10-08.md`.
  Cleanroom self-check: 0 strict hits, unchanged. Synced to Team B.

- **2026-10-08 — LIVE-REFUTED: §14/§14.3/§14.4/§14.6/§14.7's own "statically dead" conclusion, new
  §14.7a** (relayed from `purrsian-62`, who requested the live capture §14.7 itself called for). A
  Frida capture against the real running game — not a static trace — found the "file-backed navmesh
  registry is always empty" conclusion wrong or badly incomplete: of 87,918 real calls to the registry
  reader `0x00447770` during one captured street gunfight, 87,262 (99.25%) returned a real,
  non-sentinel value, not the `-1` failure sentinel §14.3's entire argument rested on; the dependent
  path query `0x00a12a80` returned success 10 of 13 times, closely time-correlated with real
  cover-selection attempts — `cover take def` (action 8) DOES resolve successfully in shipped retail.
  §14.3's own "exhaustive whole-binary call-site census, no second writer anywhere in the binary"
  claim is therefore wrong or incomplete; most likely blind spot (not yet confirmed): it counted
  callers of the two named writer FUNCTIONS rather than doing an independent raw data-xref search for
  the two registry ADDRESSES themselves, which would also catch a bulk-copy-shaped writer. Struck
  through in place, not deleted, at §14 (new banner), §14.3, §14.4 (`move retreat`'s "always declines"
  claim), §14.6 (net conclusion), §14.7 (the "STATICALLY DEAD, RUNTIME CONFIRMATION PENDING" label —
  superseded by its own requested check coming back negative on "dead"), §15.7 point 5, and §11.3 F's
  own correction block. Cross-reference: `spec-zone-data-format.md` §12.7 item 2 similarly re-opened
  (the file-side gate-dword-always-0 DATA finding stands unchanged; only the inference stacked on it
  is refuted). **A dedicated search for the real writer is in progress** (agent `afac5d6c4a3774487`,
  tasked with a fresh raw data-xref scan for the real blind spot before considering any further live
  capture); this section will be updated again once it reports. Full capture data:
  `scratchpad/live-capture-navmesh-registry_2026-10-08/navmesh_registry_live_log.csv` (88,037 rows).
  Cleanroom self-check: 0 strict hits, unchanged.

- **2026-10-08 — new §14.7b: the real-writer search concludes two static passes with the count
  mystery SOLVED and the presence-bit mystery a confirmed, exhausted, still-OPEN contradiction**
  (SPEC TEAM, agent `afac5d6c4a3774487`, two passes + an orchestrator cross-check). Pass 1 found the
  registry's real, previously-uncatalogued count writer: `0x004470a0(count=8)`, called once from the
  engine-bootstrap dispatcher at process start — `0x03171a58` is unconditionally 8 since shortly
  after boot, not file-driven at all, falsifying §14.3's "always zero" claim (the original census's
  blind spot was exactly as predicted: named-function caller-counting, not raw data-xref). Version
  mismatch ruled out by independent MD5 cross-check (exact match). Pass 2: two more static checks
  (literal-xref + whole-PE-section byte scan) both came back clean negatives — `0x00447050` really
  has only one caller, `0x00447160` really has no indirect/vtable reachability — so the ONE confirmed
  presence-bit writer still traces only to the already-dead `.czn_pc` gate, contradicting the live
  capture's own retvals (which decode into exactly the "slot present" success bit-shape). A third
  hypothesis (`purrsian-62`: maybe the file-side gate is set specifically in the `~s` tile-family,
  buried in §12.6's all-files aggregate) was tested directly and refuted — a fresh per-family re-scan
  of both city archives found `~f`/`~s`/plain all at gate=0, zero exceptions. **Net: the contradiction
  is real and unresolved by any static method tried; the only remaining path is a live write-watch**
  (script already updated and ready, `scratchpad/live-capture-navmesh-registry_2026-10-08/`, not yet
  run — needs the user to play again). Full detail:
  `scratchpad/navmesh_registry_real_writer_2026-10-08.md`. Cleanroom self-check: 0 strict hits,
  unchanged. Synced to Team B.

- **2026-10-08 — §14.7b: two final cheap checks, PARKED, no third live capture requested** (direct
  orchestrator follow-up, no new agent dispatch). CSV-correlation check (do calls near each real
  path-query success show `-1` or "present"?) came back inconclusive/uninformative — a real
  methodological limit (no caller-stack discrimination in the log, and a 99.25% background success
  rate makes "nearby calls look present" statistically meaningless either way). Fresh instruction-by-
  instruction re-disassembly of `0x00a12a80` found no alternate `true`-return path (exactly one `true`
  exit, exactly one `false` exit reached two ways) — confirms, does not refute, the function's own
  already-documented logic. Parked as "live-confirmed working, mechanism unresolved" per the
  orchestrator's own call. Cleanroom self-check: 0 strict hits, unchanged.

- **2026-10-08 — §12.4 "scope boundary" updated, OPEN item 19 CLOSED** (cross-file follow-up from
  `spec-physics-runtime.md`'s new §8.8-§8.11, unit Q-U1). The three addresses previously described as
  "not traced" (`0x004ecfc0`/`0x004ecf40`/`0x004ed320`) are confirmed pure cache/queue code with zero
  geometry/Havok calls; the real raycast resolver is a previously-unnamed sibling, `0x004eda70`,
  reached from scheduler `0x004edc80`, which calls the shared Havok ray wrapper directly — the exact
  same chain already confirmed for AI's line-of-fire probe. Vision/line-of-sight and line-of-fire are
  two throttled/cached consumers of one identical low-level path, not two separate mechanisms. Cleanroom
  self-check: 0 strict hits, unchanged. Synced to Team B.

- **2026-10-08 — §17.5 gains a cross-reference** to new `spec-render-pipeline.md` §6.6 (outer OS-thread
  topology and boot sequence) and to `spec-player-runtime.md` §5.1 (the full cross-subsystem per-frame
  order this dispatcher's own call from `0x00702a50` sits inside). No new disassembly in this file.
  Cleanroom self-check: 0 new hits. Synced to Team B.
- **2026-10-10 — §18.1/§18.2 RESOLVED/REFINED: traffic AI's real steering/target-point function and
  junction-exit handling found** (SPEC TEAM, agent `ad96ea71d4c43e15d`). Corrected a function-boundary
  error from the original 2026-10-08 pass (its Ghidra-free capstone method had mis-split one function
  at an internal label): `0x00b1c5a0` is not a function entry, it's inside `0x00b1be40` — independently
  re-verified against the live Ghidra server before splicing. `0x00b1be40` is CONFIRMED as the
  steering/target-point search the prior pass couldn't find: a masked, lookahead-bounded forward walk
  of the point graph, filtered by a lane-class/vehicle-class permission mask (not a cost function).
  Its immediate caller `0x00b1cc60` resolves either a single junction handle or an explicit
  up-to-16-candidate list into the walk's inputs — meaning "which branches are eligible" is decided one
  level further up, not inside the walk itself. Junction kind-flag bits are REFINED to per-exit
  cache/already-resolved bookkeeping, not a discrete RNG/cost choice — confirms, rather than merely
  re-flags, the prior pass's own cache/arbitration hypothesis. Narrowed, not fully closed: whether a
  live vehicle's own position/heading ever feeds this query was traced to `0x00b1cc60`'s own 8 callers,
  none individually opened — the Havok driver-input write-site trace (§18.3) was not attempted this
  pass either. Cleanroom self-check: strict regex 0 hits, C-cast-dereference check 0 hits (caught and
  fixed several decompiler-auto-name leaks — `FUN_`/`param_N` — in the agent's own draft before
  splicing).
- **2026-10-10 — new §18.5/§18.6: traffic AI's live-position feed and junction cost-comparison
  found; the Havok write-site still open** (SPEC TEAM, agent `a470f1a9d75ce284e`). Opened 6 of
  `0x00b1cc60`'s 8 callers. CONFIRMED: `0x00b4c710` reads a live, moving entity's own position
  (`+0x40`/`+0x44`/`+0x48`), gating the path re-query behind a per-entity cached-target/distance
  throttle rather than re-querying every frame — independently re-verified against the live Ghidra
  server before splicing. CONFIRMED: `0x00b1ce80` and `0x00b1e5e0` each run a genuine goal/nearest-
  distance cost comparison across candidate branches, answering §18.2's own "does a cost-based
  choice exist" question — yes, one layer above `0x00b1be40`/`0x00b1cc60`, not inside either.
  `0x00b20920` identified as static junction-graph construction, not a per-frame steering consumer.
  Still OPEN: whether `0x00b1ce80`/`0x00b1e5e0`'s own position parameters are themselves
  live-vehicle-sourced (their own callers not opened); 2 of the 8 original callers never opened;
  **the Havok driver-input write site (§18.3) was not attempted this pass** — a concrete forward
  lead was surfaced instead (`0x00b4d070`→`0x00b4ceb0`→`0x00b4c710`) rather than the originally-
  suggested backward trace. Cleanroom self-check: strict regex 0 hits, C-cast-dereference check 0
  hits.
- **2026-10-10 — §18.6 RESOLVED, closing the full §18 traffic-AI driving arc end-to-end** (SPEC
  TEAM, agent `a047382407ef16c3f`). Traced the Havok driver-input write site the orchestrator
  explicitly asked to finish: `0x00b4ceb0` is confirmed to be pure state-gate bookkeeping around
  `0x00b4c710`'s target-point resolution, never itself writing a steering/throttle field; the real
  write happens one level up, in `0x00b4d070`'s state-`0xd` sibling call `0x00b47490` (a
  heading/target dot-product that picks a steering sign), through the shared, vehicle-agnostic
  command dispatcher `0x00a929e0`/`0x00a92640`→`0x00a90b50`, landing in `0x00ae5290`/`0x00ae8fd0` —
  writing `driverInput+0x130`/`+0x128`/`+0x12c`/`+0xb0` on the exact same instance layout
  `0x00ae30c0` (§18.3) already documented (same `+0x164` flag-byte semantics, same `+0x20`
  curve-parameter sub-pointer), reached via an ordinary `vehicleEntity+0x808` component slot, not a
  special-cased traffic-only object. Independently re-verified every link in the chain against the
  live Ghidra server before splicing (the state-byte switch, both dispatcher calls, and both write
  instructions all matched exactly). Updated §18.3's own title/body (write-site CLOSED) and §18.4's
  item (c) to match. Remaining narrow opens: whether `0x00b4d070`'s `0xe`/`0x10`-state branch is a
  second, parallel write path; the vehicle-type/variant selector's exact semantics; the precise
  vector roles inside the heading dot-product. Cleanroom self-check: strict regex 0 hits,
  C-cast-dereference check 0 hits.

- **2026-10-10 — new §18.6a: the `+0xe09` bit-2 lane-side status bit's setter found (Team-B-routed
  request — they were using a labelled cross-product stand-in)** (SPEC TEAM, agent
  `a8be8f8b3a0e658c6`). An exhaustive binary-wide scan for the `+0xe09` struct-offset literal (18
  genuine code references, all individually opened and classified by bit) found exactly one bit-2
  writer in the whole image, `0x00b47c80`, reached from the known per-vehicle drive tick
  `0x00b4d070` via a third, previously-undocumented sibling call `0x00b4ad30` — unconditional every
  tick, but only reaching the write when the entity's `+0xc69` drive-state is `0xe` AND a geometric
  overtake/pass-side gate (`0x00b47f10`, closing-speed/alignment/distance checks against a second
  vehicle-shaped record) returns true. Answer to Team B's static-vs-dynamic question: dynamic, not a
  spawn-time constant, but throttled behind that gate rather than recomputed unconditionally.
  Independently re-verified `0x00b47c80`'s and `0x00b4ad30`'s disassembly against the live Ghidra
  server before splicing (bit-2 mask instructions, the `0x00b47f10`→`0x00b47c80` conditional call, and
  the `0x00b4d070`→`0x00b4ad30` unconditional call all matched exactly). Left OPEN: the identity of
  the second vehicle-shaped record both `0x00b47c80` and `0x00b47f10` compare against, and the role of
  the entity's own `+0x4c/0x50/0x54` vector. Cleanroom self-check: strict regex 0 hits, C-cast-
  dereference check 0 hits.

- **2026-10-10 — §18.6's `0x00b2bbc0` OPEN item CLOSED: the `0xe`/`0x10`-state sibling branch is
  NOT a second subsystem, it reaches the SAME write site one hop shorter** (SPEC TEAM, agent
  `affd9e1a22bea80aa`). `0x00b2bbc0` early-outs on a null sub-record, the same `0x00a1ef50`
  readiness gate, or a braking test (`0x00ad3820`, not opened); otherwise it resolves a second
  handle pair (`entity+0xcc0`/`+0xcc4`, HIGH CONFIDENCE a lead/target vehicle) and, depending on a
  distance/flag gate, calls `0x00a929e0`/`0x00a92640` DIRECTLY (no `0x00b47490` intermediary) — the
  same generic vehicle-command dispatchers §18.6 already confirmed reach `0x00ae5290`
  (`driverInput+0x130`) and `0x00ae8fd0` (`driverInput+0x128`/`+0x12c`/`+0xb0`). Independently
  re-verified `0x00b2bbc0`'s disassembly against the live Ghidra server before splicing — all 4
  dispatcher call sites and the entity-handle-pair argument pattern matched exactly. `0x00b2bbc0`
  itself has 9 call sites across unrelated functions, confirming it's a generic shared helper, not
  traffic-exclusive. Left OPEN: `0x00ad3820`'s brake semantics, the second handle pair's exact
  role, and the vehicle-type/variant selector. Cleanroom self-check: strict regex 0 hits, C-cast-
  dereference check 0 hits.

- **2026-10-10 — §18.6's `0x00ad3820` OPEN item CLOSED: not a literal brake test, a generic
  entity-status gate; its bailout is a position snap-correction, not a steering no-op** (SPEC
  TEAM, agent `a7d8c5366122f0495`). `0x00ad3820` returns "continue" only when the entity is
  non-null, two one-instruction bit-extractors (`0x008cc4d0`/`0x0091f600`, reading `+0x3b`/`+0x3a`
  bit 0) are both false, and `+0x33` bit `0x8` is set — otherwise it bails. With 30 call sites
  across dialogue/script triggers, physics, and unrelated AI functions, this reads as a generic
  "entity disabled/suspended" gate, not a brake-pedal check specifically. The bailout
  `0x00b2ed40` resolves a second handle-pair object and snaps its position/orientation via a
  direct vtable call (plus zeroing a physics sub-object if present) — no vehicle-command
  dispatcher call anywhere in its body. Independently re-verified `0x00ad3820`'s disassembly
  against the live Ghidra server before splicing (matched exactly). Left OPEN: the individual
  bit semantics (`+0x3a`/`+0x3b`/`+0x33`) and `0x00b2ed40`'s own unopened callees. Cleanroom
  self-check: strict regex 0 hits, C-cast-dereference check 0 hits.

- **2026-10-11 — new §18.6b: `0x00b47f10`'s overtake gate fully reconstructed, with the real
  numeric thresholds — Team-B-routed priority item (their implementation never fired once in a
  900-frame test)** (SPEC TEAM, agent `a091636ed7446e5b2`). The three previously-cited tuning
  constants are NOT three independent thresholds: two of them (1.0, 0.5) are reused for a
  completely different purpose earlier in the function, and the real pass condition is a
  dimensionless RATIO test (`0.5 <= closingProjection/margin <= 1.0`), not absolute
  distance/speed/alignment comparisons. The two "magnitude/cross-product" helpers are confirmed
  plain squared-magnitude and dot-product idioms (no sqrt, no cross product). Most actionably: the
  gate's step 3 requires the two vehicles' forward axes to be ≥90° apart (an oncoming/crossing-
  traffic test) — if Team B's 900-frame test only observed same-direction traffic, the gate was
  structurally guaranteed never to fire regardless of tuning, independent of any threshold values.
  Independently re-verified both helper functions' full bodies, all six cited constant values, and
  the step-3 forward-dot-product read sites against the live Ghidra server before splicing — all
  matched exactly. Cleanroom self-check: strict regex 0 hits, C-cast-dereference check 0 hits.

- 2026-10-11 (self, direct query, no agent — orchestrator-routed): §18.6's steering-magnitude
  OPEN item RESOLVED. `0x00ae5290`'s curve is fully inline, no separate evaluator: a 2-field
  `{threshold at +0x08, slope at +0x0c}` piecewise-linear response shaper read off the struct at
  `this+0x20`, plus a previously-undocumented airborne-gated early-out and an internal sign
  negation. Cleanroom self-check: strict regex 0 hits, C-cast-dereference check 0 hits.

- 2026-10-11 — §18.4(a) FULLY RESOLVED: the junction cost-comparison functions'
  `originPos`/`destPos` inputs are now traced to their source for every remaining caller (SPEC
  TEAM, agent `ad580496a8ccbfc32`, independently re-verified by direct query before splicing —
  `0x00b1ce80`'s call site inside `0x00b25620` and `0x007d39b0`'s global-singleton selection both
  matched exactly). `originPos` confirmed live-vehicle-sourced everywhere in the traffic-decision
  family (`0x00b25620`, `0x007e3380`, and `0x00b1e5e0`'s caller chain via `0x00b2b630`); `destPos`
  confirmed NOT uniform — either another live object's position (via an unresolved singleton
  accessor `0x009da4e0`) or an externally-supplied scripted/network destination (via a byte-stream
  deserializer one hop further up `0x00b25620`'s own call chain), a genuine, confirmed finding
  rather than an unresolved parameter. Of the 2 remaining unopened `0x00b1cc60` callers,
  `0x00b45aa2` joins the live-entity family; `0x007d41a1` is a DIFFERENT consumer entirely — a
  corner-curve point-pool system keyed on one of two global singletons
  (`0x0012fdff0`/`0x0012feb20`), sharpening item (b)'s existing open note rather than resolving it.
  Also includes 2 small self-corrections to this same section made before dispatching this agent:
  fixed two stale `0x00ad2f80` "exact semantics OPEN" cross-references (resolved earlier this
  session by `get_char_vehicle_type`, `spec-lua-api-behaviour.md` §21.24/§22.19) and struck a
  momentarily-wrong note about `0x00b2bbc0`'s `0xe`/`0x10`-state branch (already fully resolved in
  §18.6, caught by the orchestrator before any agent time was wasted on it). Cleanroom self-check:
  strict regex 0 hits, C-cast-dereference check 0 hits.

- 2026-10-11 — §18.4(b) RESOLVED: the "corner-curve point pool" was a misreading; closes the
  full §18 open-items list (SPEC TEAM, agent `a0bad2c44b64da2b0`, independently re-verified by
  direct query before splicing — `0x02705fe0`/BSP-pool addresses/GPS-module xref ranges all
  matched exactly). `0x007d1040`/`0x007d3830` are plain accessors on the already-documented
  Traffic zone-data singleton `0x02705fe0`, resolving the long-standing `0x0115a220`
  quantization-constant and "whose point pool is this" open notes. `0x0084b170`'s "curve
  progress test" hypothesis is REFUTED — it's a generic 2-point segment query against a global
  BSP-tree pool (`0x0242dd90`/`0x0242ddcc`), also used by the ordinary lane-follower
  `0x00b1be40`. The two singletons `0x007d41a1` reads (`0x0012fdff0`/`0x0012feb20`) are
  per-player-slot records of the already-documented GPS waypoint-navigation module
  (`spec-hud-runtime.md`'s "Waypoint/GPS" section) — `0x007d41a1` is the GPS route pathfinder
  reusing ordinary traffic lookahead machinery, not a traffic/camera/cutscene consumer. Genuinely
  still open: the BSP pool's own per-entry real-world meaning (registration functions not
  opened); whether the GPS player-slot selector is strictly local-co-op-only. Cleanroom
  self-check: strict regex 0 hits, C-cast-dereference check 0 hits.

- 2026-10-11 — BSP pool identity CONFIRMED: city district/"hood" gang-territory boundary
  volumes, not water/occlusion/exclusion geometry (SPEC TEAM, agent `ae950690639a4fa3f`,
  independently re-verified by direct raw-memory read — the 9 `hood_*` field-name strings and
  the `.?AVdistrict@@`/`.?AVhood@@` RTTI tags at `0x01301adc`/`0x01301b1c` both matched exactly).
  Only `0x0084d120` is a real constructor (via dispatch slot `0x01164304`), reading 9 named
  fields from a separate "hood definition" table (not any `.czn_pc` content-kind id) and
  inserting into the pool unless `hood_island_type!=0` or the pool (cap 15) is full. `0x0084af30`
  is the pool reset, not a registrar; `0x0084b0c0`/`0x0084b110` are the point-test/segment-test
  siblings of the already-documented `0x0084b170`. Matches `spec-cellphone-menu-runtime.md`'s
  already-documented pause-map district-control/STAG-takeover system by field names
  (`hood_owner`/`hood_income`/`hood_takeover_areas`) — closes the open item for good. Cleanroom
  self-check: strict regex 0 hits, C-cast-dereference check 0 hits.

- 2026-10-11 — self-correction, no agent: §17.7's confidence-summary OPEN line still listed
  "the near-field vehicle population's own spawn-candidate algorithm and despawn-schedule tick"
  and "the distant-to-near promotion/demotion boundary" as open, but §17.4c (landed 2026-10-08)
  already resolves both — same stale-marker pattern this session's cleanup pass just flagged
  elsewhere. Struck in place, pointed at §17.4c; the 3 genuinely-still-open sub-items left
  untouched. Cleanroom self-check: strict regex 0 hits, C-cast-dereference check 0 hits.

- 2026-10-11 — §18.6b's steering-curve threshold/slope OPEN item RESOLVED, picked directly from
  Team B's own CHOSEN-stand-in list (SPEC TEAM, agent `ab93c2359d407d0c6`, independently
  re-verified by direct query before splicing — the ctor's vtable write and both float reads
  from the shared constant matched exactly). The curve struct's real values are
  threshold=1.0/slope=1.0 (hardcoded, universal, no `.xtbl` read, one construction site
  project-wide) — NOT Team B's placeholder 0.2/0.3. Since the consumer always clamps input to
  `[0,1]`, these real values mean the curve is a structural no-op: steering input passes through
  unshaped, no deadzone or ramp. Cleanroom self-check: strict regex 0 hits, C-cast-dereference
  check 0 hits.

### 22.12 NPC weapon accuracy data (PARTIAL, 2026-10-15)

This section answers the gap Team B's combat audit raised: there were no accuracy numbers. Only the data layout is FINAL so far. The per-shot miss model is still OPEN.

- **Accuracy table.** Each weapon names an accuracy record. The weapon's info block holds a pointer to that record; when the weapon names none, a default record is used. Records are loaded from a weapon-tweak container and found by name (string compare).
- **Record fields** (float unless noted):
  - turn_speed
  - Close_range, Close_accuracy, Far_range, Far_accuracy
  - Bounces_per_sec, Speed_multipler (the misspelling is the game's own field name)
  - recover_penalty, recover_time, bullets_to_unsteady (all in the Recovery group)
  - start_burst_box_pct (Firing group)
  - explosive_z_offset, explosive_y_offset, lead_pct, explosive_error_radius_min, explosive_error_radius_max (Explosive_Miss group)
  - explosive_error_flat (a sub-record)
- **Burst firing.** The weapon field Burst_Delay_ms holds the delay between bursts. NPC_Desired_Burst_Size defaults to 3 for one weapon class, and to 1 for every other class, before the table value is read.
- **CHOSEN until the reader is found:**
  - Interpolate accuracy linearly from Close_accuracy at Close_range to Far_accuracy at Far_range, clamping outside that span.
  - Read accuracy as the probability that a shot hits.
  - Spread each miss randomly around the target.
  - Give explosive weapons a miss radius chosen uniformly between explosive_error_radius_min and explosive_error_radius_max.
  - Fire bursts of NPC_Desired_Burst_Size shots, with Burst_Delay_ms between bursts.
- **OPEN:** the in-game shot code that reads Close_accuracy and Far_accuracy (one candidate site reads the explosive radius max but is not confirmed); grenade throw decisions; the Firearm_Accuracy and accuracy_multiplier upgrade stats, which scale accuracy for the player.

#### 22.12.1 Explosive aim and burst timing (2026-10-16, static)

- **Explosive aim** (grenades and rockets, 0x0054dce0; returns success or failure). It uses the accuracy record of the shooter's current weapon.
  - **Lead:** the aim point is the target position plus a vector built from the target's velocity, scaled by lead_pct and by a random factor drawn uniformly from 0.75 to 1.2. CONFIRMED.
  - **Error radius:** t = clamp(distance(shooter, target) / 15.0 m, 0, 1). The low end is min(explosive_error_radius_min, 0.25 x a distance-scaled value). The radius is low + t x (explosive_error_radius_max - low), and it multiplies a random offset vector. The explosive_error_flat flag picks between two random-offset generators; the flat variant is guessed to keep the offset horizontal. CONFIRMED, except the meaning of the flat variant, which is a GUESS.
  - **Final adjustments:** explosive_z_offset scales the normalised aim direction, and explosive_y_offset adds a vertical offset. The result must then pass a line-of-sight and validity check; if it fails, the throw is not taken. CONFIRMED.
  - **Early gates:** a timing value above 5000 ms, or above 2000 ms together with a target flag, sends the code down an alternate path. Those gates are not decoded.
- **Burst timing** (around 0x00556a00). Weapon info is reached as the character's current weapon info.
  - Burst_Delay_ms and NPC_Desired_Burst_Size are both multiplied by 1/30, which turns them into frame-time units.
  - The next-fire timer becomes max(candidate, old - Burst_Delay x 1/30).
  - When the weapon is idle, the burst window becomes max(old, Burst_Size x 1/30 + x).
  - A piecewise-linear table in weapon info appears to feed this timer. GUESS.
- start_burst_box_pct is a player reticle value only (HUD), not NPC accuracy.
- **Still OPEN:** no reader of Close_accuracy, Far_accuracy, recover_* or bullets_to_unsteady was found by whole-text scanning. Keep the CHOSEN bullet model in §22.12. A reference search outward from the accuracy array in Ghidra is the next step.
