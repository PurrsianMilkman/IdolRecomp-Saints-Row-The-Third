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
   kinds 5 and 97 are consumed by `follow avoid LOF` and `follow browse`; producers OPEN.]**
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

    Still open inside A2: who sets list-0 flags `0x2` and `0x4` (no setter in the perception range),
    the tunables block `0x013b7fd8`…`0x013b8450`, the team-relationship table `0x009b7100`, and lists
    12, 17–21 in detail.
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
(`0x013b7fd8`…`0x013b8450`), which is filled at run time from a source not traced here.
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
6. **Slow pass** `0x00502f10` when timer `+0x21c` fires (every 1500–2000 ms; not read).
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

**Score scales.** K = 100 is the common pivot: a distance score is 100 at a reference radius.
- `0x004fa9f0` gives 100·d²/r², with r the tunable `0x013b8010`.
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
  thrown objects. Also fed by `0x00501d80` (from `0x004ec660` and `0x009286a0`).
- **11:** imminent threats, 1 s: `0x004fecf0` (from the list-9/10 passes) and `0x004fee30`, called by
  the sense pass for objects with `+0xe8` bit `0x100` within 20 units. The pass also posts **event 18**,
  and the new-entry hook posts **event 2**.
- **12:** `0x00502130` (from the witness routine and `0x004e7010`); not decoded.
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
      human's line of fire to the target.
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
    end at least 3.5 (`0x01118d48`) farther from the centroid than the human is now.
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
  - if T is in the state `0x009826e0` (`+0xcc8` = 12 with `+0xcce` 20 or 21): `+0x24` = the object
    linked at `+0xda8` (or T's position plus its facing), `+0x30` = T's position;
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
  - up to eight pickups within 225 that pass `0x004f0810` are navmesh-snapped;
  - one multi-target path query (`0x00a12a80`, path limit 22.5) chooses the reachable one → `+0x08`;
  - none: back-off 700 ms.
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
  - a game-mode switch (`0x00614d00`, a byte jump table);
  - the vehicle is not in any of the states `0x00ad31a0`, `0x00ad3270`, `0x00ad3290`, `0x00ad3140`;
  - vehicle `+0x5c` ≥ 0.1;
  - T visible;
  - a clearance probe around the vehicle must be clear;
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
  - a seat from `0x005457f0` → `+0x3c`;
  - the seat's entry point (`0x00b0eac0`), transformed to world space (`0x004e44b0`, `0x004f4770`)
    → `+0x24`;
  - the vehicle → `+0x08`.
- **38 `follow other car`** (`0x00548e80`): the same, for a group member's vehicle that is not the
  leader's. A member in `Hijack` (18) offers its own committed block's handle (`+0x458`); a member in
  `Passenger` (25) or `Veh trans` (41) offers its vehicle. The vehicle must also not be `0x00ad4810`.
- **39 `follow carjack`** (`0x00549080`): `0x00548590`. A candidate vehicle, chosen by vehicle class
  (`0x00ad2f80`, 0…5, through a jump table) or by `0x0051cc80` / `0x0051ce60` / `0x0051cd70` → `+0x08`;
  it also sets `+0x56` bit 5 and clears `+0x3c` and `+0x50`.
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
      must exist.

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
  vehicle's local bounding box (`0x004f2970`, a clamp-and-measure point-to-box distance). It returns
  true only when that exceeds 0.5 (`0x01116a20`) on every lane (`0x004bc680` = "argument < this in all
  lanes"). So for a `0x009422d0`-class human against a T in a vehicle, `advance` holds only while the
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

## ~~9.~~ ~~10.~~ ~~11.~~ 12. Clean-room check

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
