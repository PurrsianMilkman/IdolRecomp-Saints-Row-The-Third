# AI unit A0: the per-character AI update and the 44-mode machine protocol

Team A, 2026-10-03. Investigation note only; no spec file was edited. Unit A0 of the order proposed in
`review/scoping-ai.md` §4. That note is the foundation and is cited, not repeated: its §1 call-chain
diagram, its §1.3 category-to-mode table and its §1.5 per-character action-state layout are taken as
given unless this note corrects them, and corrections are flagged **[corrects scoping]**.

**Method.** A private copy of the local Ghidra project (`tools/gp_aiA0`, deleted afterwards),
headless `-readOnly -noanalysis`, `ghidra/CrreishDump.java` in `func`, `range`, `ptrs`, `xref` and
`calls` modes, plus one private read-only push-argument survey script kept in the session scratchpad
(it lists, for every call site of a target, the containing function and the preceding PUSH operands).
Full listings were read for every function named in §1-§5 unless marked otherwise. The dumps stay in
the scratchpad; everything below is prose.

**Labels** (house style): **CONFIRMED** = read in a listed instruction stream or a static table read in
this pass; **HIGH** = follows from a dumped instruction or reference list but the body it points at was
not read in full; **HYPOTHESIS** = plausible, not settled; **OPEN** = collected in §8.

**Naming.** Mode names (`Idle`, `Panic`, ...) and sub-state names (`attack`, `sidewalk`, ...) are the
engine's own strings from static tables, as elsewhere in this project. Function roles are my words.
Character offsets are offsets into the human object (the same object as the scoping note's "character").

**Out of bounds, kept throughout.** No `.czn_pc` interior was opened; no navmesh or path-planning code
was read.

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
    others. This was not checked.
- `0x0099bcb0(human, dt)` returns early only if the object is unusable (`0x00853b10`: null, flag `+0x33`
  bit 2 set, or type byte `+0x34` = 0xff) or if `+0x33` bit 3 is clear. Otherwise it calls the AI
  update `0x004e8d00(human)` **unconditionally**, once, near the end of its body (call at
  `0x0099c79b`). Nothing in between can skip it.
- A third route exists: `0x00952760` → `0x00af4150`. In one branch (its state word `+0x16d4` = 3,
  with a live linked object at handle `+0x16c0`) `0x00af4150` looks up a helper object
  (`0x004d7e00`). On that object it clears byte `+0x1da6` bit 0x10 and sets bit 0x20, then calls the AI
  update directly. It sets those bits so that call is exempt from the throttle (§1.4). Route
  identity: HYPOTHESIS (a carried or attached-character special case).

So the AI update runs **at most once per human per frame from the normal path**. There are two
pre-conditions: the human sits on one of the two manager lists, and its `+0x33` bit 3 is set (HIGH: an
"active/simulated" flag). A human on both lists, or one also reached through the `0x00af4150` path, could
be updated twice in a frame; this was not checked (§8).

### 1.2 Entry checks

The single argument is the human. Order, CONFIRMED:

1. It computes a "human-typed" pointer, used later for the throttle and the tail calls. This is the
   human itself when its type descriptor (the table `0x02cc9900` indexed by type byte `+0x34`) has bit 2
   of byte `+0xa`; otherwise it is null.
2. If `0x00722000()` returns true, it returns at once. That function tests a global state counter
   (`0x0153b520` > 1) and a pointer (`0x0153b528`). HIGH: a global pause or loading condition.
3. If the human is unusable (`0x00853b10`), it returns.
4. **Network authority split:** `0x008addb0(human, 0)`. For a non-null object it calls `0x00884760` on the
   object's network record (`+0x3c`). That compares the record's owner byte with the local peer id, or
   answers true when there is no session. HIGH: "is this object locally owned".

### 1.3 Non-authority (proxy) branch

CONFIRMED by listing. This branch never chooses, never ticks a mode, and never touches the budget:

- It resolves the handle at `+0x1330` (`0x004e58a0`: a 64-bit handle resolved through `0x00458230`; the
  result must be a live object whose type has bit 0 of `+0xa`). HYPOTHESIS: the current combat target.
- If a target exists and the current mode is `Combat Movement` (5) or `Fire spray` (12), it calls
  `0x00963570` on the target's `+0x1330` sub-object. The arguments are a flag computed from the
  human's vehicle (`0x00941f40` → `+0x19c` → `0x00b83a00`) and "mode is Fire spray".
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
     some animation and physics follow-up that was not read in detail. HIGH: a dead or incapacitated
     character.
2. **Target upkeep.** It resolves the `+0x1330` handle and the human's vehicle (`0x00971cf0`). If there
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
     number of live AI humans, maintained as a field of a larger structure.
   - **Who is throttled.** A human is eligible only if all of these hold:
     - it is human-typed;
     - it is not in a vehicle that passes `0x00536300` (vehicle at `0x00941f40`, test `0x00b81790` on
       vehicle `+0x19c`);
     - its mode is neither `Brute Melee` (3) nor `Brute Stomp` (2);
     - byte `+0x1da6` bit 0x20 (exempt) is clear.

     Ineligible humans skip the throttle and always think.
   - **The rule for an eligible human:**
     1. If the latch is already set, set its `+0x1da6` bit 0x10.
     2. Otherwise, if count > budget / 2 (signed halving), set the latch, and do not set the bit for
        this human.
     3. Then, if bit 0x10 is set: clear it and **skip this frame**. If it is clear: set it and
        **think**.

     So bit 0x10 means "thought last time". With the latch clear, an eligible human thinks every other
     frame, alternating. Once more than half the budget has thought in a frame, every later eligible
     human skips that frame and is left with the bit clear, so it is guaranteed to think on its next
     visit.
   - **What a skipped human still does:**
     - the event pump runs in flag-only mode (`0x004f5b90(human, 1)`);
     - if it has a vehicle object (`0x00941f40` non-null; HIGH: the human's vehicle, since its `+0x19c`
       feeds the vehicle tests elsewhere in this note), and not in the case "`+0xac8` non-zero and
       vehicle `+0x1a0` low 2 bits clear and vehicle `+0x1a1` bit 7 clear", it runs `0x004ddb60`
       (calls `0x004f8a50` when the `+0x510` sub-object's `+0x20c` is non-null) and `0x00b6ccb0`;
     - then it returns.

     It gets **no perception tick, no decision, no pending-mode apply and no mode tick** that frame.
     Its mode keeps whatever state the last tick left.
   - The counter is incremented for **every** human that reaches the think path, eligible or not.
5. **The think path** (CONFIRMED), in this exact order:
   1. count `0x013576d0` += 1;
   2. **perception tick:** `0x00507520` with `this` = human `+0x510`;
   3. **event pump:** `0x004f5b90(human, pending)`, where `pending` = `+0x317` bit 3. Events are
      therefore delivered only when no mode switch is pending (§2.3);
   4. **decision step** `0x004f5210(human)`, which may request a mode (A1 material; its handoff is in
      §4.3);
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
      bits of `+0x13c5`, and maintains those bits. HIGH: the driver-side hand-off to the vehicle
      system.

**[corrects scoping]** on `0x004f5b90`: the scoping note's "decision timer / gate" label is wrong. It is
the per-character **event (stimulus) pump** that delivers queued events to the current mode's slot
`+0x10` (§2.3). It is called three times in the update with a "flag only" argument of 1 (skip paths)
or the pending bit (think path).

---

## 2. The 44-entry mode table `0x012e1538`

### 2.1 Layout and the role of each slot

CONFIRMED by static read: 44 rows × 0x24 bytes = `0x012e1538`…`0x012e1b67`, file-backed. Row
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
| `+0x14` | `0x004ea530` (network read) | `init_params(block)` | **Initialise the 0x90-byte parameter block** to the mode's defaults. It is also called directly by begin functions on their local block before entering. Examples: `Idle`'s `0x0050f520` is called on a stack block by the `Idle` begin `0x00512f90` and by `0x00a34c30`; `Panic`'s `0x004ee590` is called by the `Panic` begin `0x004ef450`. The network read calls it on the live block `human+0x8d0`. |
| `+0x18` | `0x004e61b0` (network write of the human's AI state) | `net_write(human, stream)` | **Serialise mode-specific state.** Examples: `Panic` `0x004eecd0` writes fields `+0x8d0…+0x8f4` with the bit-stream writers `0x00881110`/`0x00881040`; the shared `0x00523ca0` writes byte `+0x8d0` and the handle at `+0x8d8`. |
| `+0x1c` | `0x004ea530` (network read, the counterpart of `0x004e61b0`) | `net_read(human, arg, stream)` | **Deserialise mode-specific state** after the generic fields, the mode byte, the sub-state byte and the action block have been read. |
| `+0x20` | `0x004ea9b0` (a network message handler, referenced only from the table at `0x01169604`) | `on_remote_request(stream, arg)` | **Receive a remote "enter this mode" request.** The handler reads a mode byte; if the mode is < `0x2c` and has slot `+0x20`, that slot reads the rest. Otherwise the default reads a 2-byte character id (`0x004db850`), requests the mode through the setter and, if accepted, reads the action block into `+0x450`. Example: `Panic`'s `0x004efc10` reads a character id and calls the `Panic` begin `0x004ef450`. |

**[corrects scoping]:** the scoping note guessed (HYPOTHESIS) that `+0x08`/`+0x10` were further
lifecycle hooks and `+0x18`/`+0x1c` an "action still valid / interrupt" pair. They are: `+0x08` exit,
`+0x10` event handler, `+0x18`/`+0x1c` network write/read, `+0x20` remote mode request. The shared pair
`0x004f3eb0`/`0x004f5440` used in all decision-driven modes is the **network write/read of the
committed action** (CONFIRMED by listing):

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
    argument; a large body, `0x004ebc70`–`0x004ec4c0` region, not read in full). A zero result from the default handler
    becomes 2.
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
  reactive mode.

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

The scoping note described one function, `0x004e4130`, as "the mode setter". It is in fact the first of
**three layers**: a **request** (`0x004e4130`), an **apply** step that runs once per AI update
(`0x004e8310`), and the **transition primitive** that actually swaps modes and runs exit and enter
(`0x004dd380`). Between apply and transition sit per-mode **begin functions** that build a parameter
block.

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
`0x009715d0` (likely "is down") is false.

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
3. If a parameter block was passed, copy 0x90 bytes (0x24 dwords) into `+0x8d0`, the **live mode
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
by replication (the network read of §2.4 writes `+0x501` directly). HYPOTHESIS: leftover modes. OPEN
(§8).

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
  Code in `0x006b4980`–`0x006b81a0` requests `Idle` (3 sites), `Cower` (3) and `Flee` (2). The
  scoping note attributes this range to mission and vehicle routines; not re-checked.
- **Combat- or damage-triggered.** `0x009a48e0` requests `Stunned` at `0x009a4e6e`. The per-human
  update `0x0099bcb0` calls it with ten arguments, and `0x00af4150` does too. HYPOTHESIS: a hit or
  ragdoll reaction. Six more functions in the human-object region (`0x0094fa00`, `0x0095f250`,
  `0x0096b720`, `0x009720f0`, `0x009acb20`, `0x009e52e0`) also request `Stunned`; their identities
  were not read. More such sites exist (for example `0x0099b7e0`, `0x00952760`, `0x00a275e0`).
HYPOTHESIS: knock-down entry is "request Stunned" from many places, which fits the
  setter's rule that a pending Stunned request cannot be overwritten (§3.1).
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
     `+0x24 + 6·a` (`0x00dab660(lo, hi)`; record pointer `+0x31c`);
   - if the per-action repeat timer `+0x320 + 4·a` has less time remaining than the roll, restart it
     with the roll.

   **The cool-down can only be lengthened by finishing, never shortened.** The two words are the
   behaviour record's per-action repeat minimum and maximum; their naming is the existing
   `ai_behavior.xtbl` spec's, not re-checked here.
2. Set `+0x44c` bit 0 (**action finished**). Restart the timer at `+0x43c` with 0.
3. **Ordered-action completion.** This step runs only if all three hold:
   - the ordered bit `+0x44c` bit 3 is set;
   - the staged block's action id (`+0x4a8` first byte) equals `a`;
   - action `a`'s category mode equals the current mode.

   It then does the following:
   - clear bits 3 and 4 and set bit 2 (**ordered action completed**);
   - complete the request id `+0x4aa` through the request pool (`0x006f66d0(id, own handle)`);
   - reset the staged block `+0x4a8` to the 0x58-byte template `0x012e14e0`;
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

CONFIRMED by listing of `0x004e8d00` and `0x004f5210`. This is hand-off depth only; the decision step
itself is A1.

- **The finish suppresses the mode tick.** The AI update skips the tick while `+0x44c` bit 0 is set
  (§5). Between "finished" and the next accepted request, the human is frozen in its old mode and gets
  no ticks.
- **The decision step sees the finish.**
  1. The current action is now `0x47`, so the per-action re-decision wait is bypassed. The `+0x438`
     timer is restarted and the chooser `0x004f3750` runs.
  2. If the chooser returns an action (≠ `0x47`), and it differs from the current one *or* bit 0 is
     set, the step does the following:
     - clears bit 0;
     - clears the previous action (`0x004f4eb0`);
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

### 4.4 The ordered-action seam (kind 3) at the hand-off point

CONFIRMED by listing of `0x004f4d00`, `0x004f5360` and `0x004f4c90`. The request pool itself is
specified elsewhere and was not re-read.

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
  by that mode's later finish. Its id stays open until something cancels it.
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

CONFIRMED (static read of `0x012e1538`, 396 dwords). Slot headings use the roles of §2.1.

- **Stub markers:** `nop` = `0x00754410`; `ret1` / `ret3` / `ret4` = the constant event-handler stubs
  `0x00682c50` / `0x006147f0` / `0x0066ac20`; `—` = null.
- **Act. net** = the shared committed-action network pair `0x004f3eb0` / `0x004f5440` (§2.1).
- **CAE** = reachable from the decision layer via action category bytes (scoping note §1.3).
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

Checked by script: every cell of the table above was compared against the 396-dword dump, with 0
mismatches.

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
The scoping note's 237 probably counted the stubs.

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
   - `+0x8d0`…`+0x95f`: the 0x90-byte live mode parameter block.

   None of `+0x500`…`+0x504` is reset by exit. The just-entered flag `+0x503` was seen only being set
   (to 1) by the transition; who clears it was not traced (§8).
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
   probably the main one for the 24 non-CAE modes. Its producer side (who appends records to
   `+0x1c54`) was not traced; it is the natural interface between perception (A2/A4) and the reactive
   modes (A5).
6. **The Brute modes are throttle-exempt**, as are vehicles that pass `0x00536300`.
7. **The decision gate's forced `Skydiving` goes through the generic apply path** (31 is generic), so
   it behaves like any CAE mode request even though no action carries category 31.

---

## 8. OPEN

1. **Who clears `+0x503` (just-entered)?** HYPOTHESIS: the enter slot or the first tick; a structure-offset
   search was not run.
2. **Who appends event records to `+0x1c54`?** The record layout seen here: link `+0x00`, kind `+0x08`,
   timer `+0x24`, mode and sub-state filters `+0x28`/`+0x29`, flags `+0x2a`; the `Panic` handler also
   reads `+0x10`/`+0x14` (a handle), `+0x30`, `+0x34`. The kind values seen are 12, 46 and 81 (in
   `Panic`'s handler). Their names and the producers are for A2/A5.
3. **The default event handler `0x004ebc70`** (a large body) was dumped but not read beyond its setter
   calls. It is the reactive-mode dispatcher for every mode whose `+0x10` returns 0.
4. **How `Aim`, `Gunfire evade` and `Veh enter` are entered.** None has a direct caller of the transition
   primitive. Candidates: the computed-mode call in `0x00a34c30` (only seen entering `Idle`), the
   remote-request path, or the `Hijack` mode's own handlers. Also: why `0x00afd4c0` requests `Veh enter`
   (38) when apply ignores 38.
5. **The `Veh threat` → `Dbno` fall-through** (§3.2): confirm that `0x009715d0` is false for a
   vehicle-threat human in practice, or whether the binary really double-transitions.
6. **Identity of the budget source `0x02442954`** (HIGH: count of live AI humans), and of the
   eligibility and exemption bits (`+0x1da6` bits 0x10/0x20): only `0x00af4150` was seen setting 0x20.
7. **The second human list** (`+0x1f0`, via `0x009ea630`/`0x009e91c0`) runs **before** the frame's
   budget reset. Confirm whether it holds players only; if it holds any AI human, those humans count
   against the previous frame's budget state.
8. **Double update.** Can a human be on both manager lists, or be reached through `0x00952760` →
   `0x00af4150` in the same frame as its normal update? Not checked.
9. The "may think" gate's individual tests (`0x009a1dc0`, `0x00985d80` states 4/5/6/8, `0x0095d740`,
   object flags `0x2be`/`0x408`) are unidentified.
10. The second word of each behaviour-record per-action triple (`+0x24 + 6·a`) is used here as the
    roll's upper bound. Cross-check against the `ai_behavior.xtbl` spec's field names when folding.

---

## 9. Clean-room check

- Addresses are plain hex throughout. There are no Ghidra auto-names for functions, globals or labels,
  no decompiler variable names, and no pasted pseudocode. Two shared stubs carry library symbol names in
  the database because of identical-code folding; they are cited by address only. Mode and sub-state
  names are the engine's own data strings read from `0x012e1538` and `0x012e1168`; the two network tag
  strings in §3.1 are engine data strings too.
- No spec file was edited. The private Ghidra copy `tools/gp_aiA0` was deleted after the dumps. The
  push-survey script stays in the session scratchpad only.
- **Self-check, run on the finished file as the last step.** The project's standard auto-name regex
  (`iVar…|uVar…|…|FUN_…|DAT_…`, run with `grep -noP`) returns **0 hits**. A plain-substring backstop
  scan for `unaff_`, `extraout_`, `Stack_`, `Var`, `param_`, `local_`, `FUN_`, `DAT_`, `LAB_` and
  `undefined` (plus the two folded library symbol names) returns **0 hits**. One earlier hit, `param_`
  inside my own argument name in the §3.3 heading, was renamed before this final scan.
