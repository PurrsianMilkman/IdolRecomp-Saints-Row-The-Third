# Interpretation of bridge dumps `20261001T122853-team-a-nnlt` (zscene load step, cutscene.xtbl parse, cutscene jump table, fade callers, UI resolution writers, safe-frame constants)

Team A, 2026-10-01. Source: CrreishDump output for `team-a/ghidra/jobs/mnao-followup.json`
(steps `zscene`, `zxref`, `jt`, `fade`, `ui`, `consts`). Follows `interp_mnao.md`; its §7 next-dump
list is worked through here. Every function named in the job was present as a defined function.
One listing from the previous job is re-read here because the jump table points into it:
`20261001T114101-team-a-mnao/zscene/func_0x0072d660.txt` (cited below as "mnao listing").

Labels: **CONFIRMED — disassembly** = read in these dumps' listings (or in the mnao listing where
said so); **HIGH CONFIDENCE** = follows from a dumped instruction, reference list or string but the
deciding body was not dumped; **HYPOTHESIS** = plausible, not settled; **OPEN** = not settled, see §8.

Shared primitives, as in the earlier interpretations: UI Lua state getter 0x00e1a1b0 (returns
0x02a45450), "is this global a function" check 0x00e0cef0, call builder 0x00e0ca80, call dispatch
0x00e0cd00; cutscene state dword 0x0153b520, cutscene manager pointer 0x0153b528 (its first dword
points at the manager's scene-table entry, whose +0x8 is the entry kind), cutscene-side object
0x0153b53c with the state setter 0x008788e0; zscene state 0x0153b51c, current entry 0x0153b530,
pending entry 0x0153b538; XML table helpers 0x00dac9a0 (open a table file, returns its "Table"
element), 0x00dc4ff0 / thunk 0x00dab9e0 (first child element by name), 0x00dc5030 / thunk
0x00dab9f0 (next sibling by name), 0x00daba10 (text of a named child), 0x00daba40 (text of a named
child, variant), 0x00dab960 / thunk 0x00dab9d0 (close), 0x00dc5150 (count children by name).

---

## 1. 0x007258a0 — the cutscene machine's second per-frame step (answers Q1)

### 1.1 Callers and place in the frame — **CONFIRMED — disassembly** for the references

`xref 0x007258a0` lists exactly two call sites: 0x00703121 (code Ghidra has not put in a function)
and 0x00bdc0ef inside 0x00bdbf30. The mnao job listed the per-frame stepper 0x0072d660's two call
sites as 0x00702a7c (inside 0x00702a50) and 0x00bdbc54 (no function). So both routines are called
from the same two code regions, and in each region 0x0072d660's call precedes 0x007258a0's
(0x00702a7c < 0x00703121; 0x00bdbc54 < 0x00bdc0ef). **HIGH CONFIDENCE:** 0x007258a0 runs once per
frame from the same driver as 0x0072d660, after it. **OPEN:** the two driver bodies (0x00702a50 and
0x00bdbf30), and whether anything between the two calls matters.

### 1.2 Body — **CONFIRMED — disassembly**, full listing and decompilation

The routine takes no arguments. It first writes two status bytes every call:

- 0x0153b525 := 1 when the cutscene state is 10, 11, 12 or 13, else 0. Its only other reference is
  the one-line getter 0x00720780. **HIGH CONFIDENCE** label: "a cutscene is playing".
- 0x0153b526 := 1 when the state is ≥ 2 **and** the manager 0x0153b528 exists **and** (the manager
  has no table entry, or that entry's kind +0x8 is non-zero); else 0. Getter 0x00720790.
  **HIGH CONFIDENCE** label: "a cutscene is in progress (requested or later)".

Then it switches on the cutscene state (states above 0x10 fall through to the end). The switch uses
its own tables at 0x00725dbc (targets) and 0x00725dd4 (byte index); the index table was not dumped as
data, so the case numbers below are the decompiler's reading of it, which the listing's compare
(`state > 0x10 → return`) and the per-case stores are consistent with.

| State | What 0x007258a0 does |
|---|---|
| 0, 2 | Calls the zscene reset-check 0x00720320; if it reports true, calls the promotion 0x00720410 (pending → current, zscene state := 1). Nothing else. |
| 4 | Only when 0x0153b543 is clear, the manager's entry has kind **2** (0x00721ba0) and 0x009f9cc0 is false: runs 0x00b903f0, calls slot +0x40(0) of the service objects 0x16 and 7 (0x00dd6450 indexes the service table at 0x02a3c170), runs the one-shot 0x00a74760 (it moves the stage dword 0x0271a250 from 0 to 1 after four calls on the object 0x02442750), then inside the bracket 0x005d1590 / 0x005d15d0 mounts the packfile named "preload_items.vpp" when 0x0149365c or 0x012ebc04 is set and "preload_effects.vpp" when 0x0149365c or 0x01493660 is set (0x00709280 builds the path from "packfiles\pc\cache\" or, when 0x0149365e is set, "cache\"), and finally sets **0x0153b543 := 1**. |
| 5 | If the entry has kind **1** (0x00721b80) and there is a current zscene entry whose selected handle is the loaded scene (0x0071fee0 then 0x007203e0): stop the transition stream with 0x00731600(1) (0x004638d0 on the pool 0x0153b730 with the handle 0x0153b71c) and write **state := 7**. Otherwise, if 0x00720320 reports true: when the kind is 2, call 0x00723120 (walks two index lists of the world-object set 0x03171a64 — count +0x204 / indices +0x1fc and count +0xc0 / indices +0xb8 — and returns the first object whose +0x7c handle is live per 0x00dafbb0); then always call **0x00722f10** (§1.3), which ends in state := 6. |
| 0xf | Requires kind 2, else it goes straight to the "stop" tail (0x007218b0, **state := 0x10** through 0x008788e0). With kind 2: if the manager's +0x3590 is set, 0x00731e00 releases the object 0x0153b8e0 (virtual +0x8c(0)). Then slot +0x40(0, 0) of the service-9 object's +0x48 (0x00720550) and, if its byte +4 is clear, of the service-8 object's +0x48. Then two paths on **0x0153b52c**: (a) non-null — a chained cutscene (§1.4): copy a block of manager fields (+0x4454, the float +0x21c0, bits 6 and 1 of +0x35da, the string at +0x4550, 16 bytes at +0x35f0, 48 bytes at +0x3600, 16 dwords at +0x36a8) to the stack, free the heap block named "cutscene_virtual_pool" (0x00db53e0 on the heap object 0x01493a78 with 0x0153b518), clear 0x0153b518 and 0x0153b528, mount the two preload packfiles again inside the 0x005d1590/0x005d15d0 bracket, and call 0x00725670 with the saved block (§1.4), which ends in **state := 5**. (b) null: 0x00dd6300(0x01496108), five calls 0x00db1e10(7,3), (1,1), (5,7), (0x17,0x16), (8,4) (these undo the (x,8) settings made in §1.3; **HYPOTHESIS:** per-service streaming/memory mode switches), 0x009df3d0 then 0x00941ee0(no-player flag twice); if 0x0153b543 is set: when 0x0153b54e is clear, release the mounted preload packfiles (0x00ddb530 once per mounted file, same conditions as the mounts) and set 0x0153b54e := 1; then 0x00a74000 (stage 3 → 4). Finally the "stop" tail: 0x007218b0 and **state := 0x10**. |
| 0x10 | With kind 2 only: 0x00a747a0, the stage machine's tail (0x0271a250: 4 → 5 waits for 0x005ce4f0 while pumping 0x00daf9a0(1,0), releases "preload_items.vpp" through 0x00709530 and "effect_slurp"; 5 → 0 waits for 0x008fb9f0, releases "effects_slurp", and posts the marker name "salloc_ingame_cutscene_unlock_async_poll" to 0x00707410(0, name)). |
| 1, 3, 6..0xe | Nothing. |

**0x0271a250** is therefore a staged lock dword walked 0 → 1 (state 4) → 2/3 (0x00a73c20, reached
from §1.3) → 4 (state 0xf) → 5 → 0 (state 0x10). **HIGH CONFIDENCE** (from the marker string and the
packfile names): it sequences the in-game-cutscene memory lock: items/effects pools are released,
preload packfiles mounted, and the reverse at the end.

### 1.3 0x00722f10 — the "start loading the cutscene's resources" step — **CONFIRMED — disassembly**

Reached from state 5 above (and from 0x00725670 for a chained cutscene). Two bodies:

- **Entry kind other than 2** (a zscene): select the manager entry's live handle (+0xc, else +0x10
  by the variant rule of §3.3) and hand it to the handle manager's start routine 0x00dafea0; then the
  common tail.
- **Kind 2** (story cutscene): return early when 0x00dd6260(0x01496104) is false — in that case it
  tail-calls 0x00a73c20 (stage 0x0271a250 := 2 or 3) — or when 0x00941f30 is false. If the zscene
  state is ≥ 1: there must be a current zscene entry whose selected handle has class 1 (0x00dafb60;
  "no handle"), otherwise return; then 0x0153b541 := 1, zscene state := 0, current := 0 (the same
  reset 0x00720320 performs). Then, on the first pass (manager byte +0x35da bit 3 clear):
  0x00dd6300(0x01496104), the service-9 +0x48 object, the five calls 0x00db1e10(7,8), (1,8), (5,8),
  (8,5), (0x17,8), then 0x00721990 with the name "npc_basehead", set bit 3 and return (the next
  frame continues). On the second pass: 0x007202e0("npc_basehead") must be true; then 0x0071ff20
  (manager) gives a handle that is started with 0x00dafea0; every handle in the entry's list at
  +0x20 (count +0x28, §3.4) is started with 0x00dafea0; then 0x007315a0(entry +0xf4, kind == 1)
  starts the entry's soundtrack stream (§3.4); then the common tail.
- **Common tail:** 0x00d9e140(0x012f5d30, 120000) — a 120 s stamp (**HIGH CONFIDENCE:** a load
  timeout), 0x0153b54f := 1, **state := 6**.

The string "npc_basehead" is a functional identifier of a resource the cutscene loader always
loads first (**HIGH CONFIDENCE:** the shared head mesh for NPCs).

### 1.4 0x00725670 — (re)create the manager for a chained cutscene — **CONFIRMED — disassembly**

Called only from state 0xf path (a). It allocates "cutscene_virtual_pool" (0x7800 bytes, 0x00db52d0
on 0x01493a78) into 0x0153b518, allocates the manager (0x45a0 bytes from it through 0x00dad360) and
constructs it with 0x00724be0(0x0153b52c) into 0x0153b528, runs 0x007211d0, copies the saved
16-dword block back to +0x36a8, calls 0x0101b460 and 0x0073c730(0x0153ba48), clears all but bit 7
of 0x0153b524, writes 0x012f5d30 := -1, copies the float to +0x21c0 and bit 1 into +0x35da, stores
the saved +0x4454 only when the session object (0x0087ba20) exists with +0x5c == +0x58 (the same
co-op host test as the fade broadcast helpers), and if the new entry has kind 2 calls
0x00941ee0(0, 1) and 0x00941eb0. It copies the saved string into +0x4550, bit 6 into +0x35da, the
16 bytes into +0x35f0 and the 48 bytes into +0x3600, takes a 64-bit token from 0x0084a780 into
+0x4454 and sets bit 5 of +0x35db, then calls 0x00722f10 (§1.3) and writes **state := 5**; when
0x0153b557 is set it also re-arms the two 1000-unit timers 0x0153b55c / 0x0153b560 (0x00dad810,
0x00dad740). So **0x0153b52c is the "next cutscene to chain into"** (**CONFIRMED** for the data
path; it is copied from the manager's +0x35cc by the state-13 case of 0x0072d660, mnao listing).

### 1.5 Consequence for a bare Lua `zscene_prep`

**CONFIRMED — disassembly:** with the cutscene state at 0 (no cutscene), 0x007258a0 calls
0x00720320 every frame and, when it returns true, 0x00720410 promotes the pending entry. 0x00720320
(re-read here) returns true in three cases: no current entry (it also sets the auto-select byte
0x0153b541 := 1); a current entry whose selected handle has class 1 (then it resets: 0x0153b541 :=
0x0153b542, zscene state := 0, current moved to pending when 0x0153b542 is set, current := 0); a
current entry with a live handle while 0x0153b541 is set. Otherwise false.

After `zscene_prep` the teardown 0x00721c20(1, 0, 0) has released the current entry's handle, set
0x0153b541 := 0 and left the current pointer in place. Promotion on the next frame therefore depends
on the released handle classifying as class 1. **HIGH CONFIDENCE** that it does (the teardown's own
"handle not live" branch treats class 1 as the released condition, and the completion routine
0x007285c0 would otherwise never see a fresh load); **OPEN:** the effect of 0x00dafad0 on the
handle's flag word. For the first prep of a session (no current entry) the promotion is
**CONFIRMED** to follow on the next 0x007258a0 call. So a Lua-only zscene **is** promoted by the
engine, in cutscene state 0, by 0x007258a0; the completion then comes from 0x0072d660's state-0
case (tail-jump to 0x007285c0). Both run every frame.

---

## 2. The cutscene state machine's states, from the jump table (answers Q3)

### 2.1 The table — **CONFIRMED — disassembly**

`ptrs 0x0072defc` printed 16 dwords (the job asked for `count:16`). The switch in 0x0072d660 covers
states 0..0x13 (20 values: `state > 0x13 → return`, then an indexed jump through 0x0072defc), so the
table has 20 entries and **the last four (states 0x10..0x13, at 0x0072df3c..0x0072df48) were not
dumped**. The 16 printed targets are:

| State | Target | Case body in the mnao listing |
|---|---|---|
| 0 | 0x0072d711 | tail-jump to the zscene completion 0x007285c0 (idle; runs the zscene idle driver / completion) |
| 1 | 0x0072d71a | needs 0x0059fa10 true and 0x007b06c0 false; kind 2 with 0x0153b558 clear → 0x0071ff60(1) and the marker "CS_STATE_FADING_OUT" with flag 1 then 0x0071ffd0(1); otherwise 0x00721bc0 / 0x00889cf0, then with no local player → 0x0072bf90(0); with a player → **state := 3** |
| 2 | 0x0072d793 | 0x007285c0; when the zscene state is 2 → cutscene start 0x00725df0(current entry's hash, bytes 0x0153b580/0x0153b581, block 0x0153b588, 1), then the optional loaders 0x00723ea0 / 0x007235c0 |
| 3 | 0x0072d811 | needs a local player, 0x009f26b0(player +0x2088), 0x00867830 / 0x009df3d0 checks and 0x009f9cc0 false → **state := 4** |
| 4 | 0x0072d884 | needs 0x01493a9c ≥ 400, (kind 2 → 0x0153b543 set, i.e. §1.2 state 4 done), 0x0059fa10, 0x00720040, 0x00a90690 → 0x00a936c0(0, player); co-op token into manager +0x4454; 0x007270f0; **state := 5**; then manager byte +0x3630 → 0x007229b0 (bail if the state moved); then for kind 1 with the zscene loaded (0x007203e0): 0x00731600(1), 0x0072bff0, and if the state is 8: 0x00720200, 0x0072c260, and if it moved again, 0x00877ca0(9) → tail 0x00729150 |
| 5 | 0x0072def5 | nothing (0x007258a0 owns state 5, §1.2) |
| 6 | 0x0072d9f4 | 0x0072c790; if still 6 → return; else if 0x00720660 false → fall into the state-7 body |
| 7 | 0x0072da13 | 0x0072bff0; if still 7 → return; else if 0x00720660 false → fall into the state-8 body |
| 8 | 0x0072da32 | 0x00720200, 0x0072c260; if still 8 → return; else if 0x00720660 false → fall into the state-9 body |
| 9 | 0x0072da56 | 0x00720040 → tail-jump 0x00729150 (**HIGH CONFIDENCE:** playback start; it reads 0x0153b543, 0x0153b52c, 0x0153b54e, 0x012ebc04, 0x01493660) |
| 10 | 0x0072daba | tail-jump 0x0072c980 (playing) |
| 11 | 0x0072dac3 | 0x00720040 false and 0x0153b54d clear: when the float 0x0153ba48 exceeds the float 0x0153b290 → **state := 10**, tail 0x0072c980; else 0x0153b54d := 1 and tail-jump 0x0072d030 |
| 12 | 0x0072da71 | 0x00720040 true, or 0x00867830 with 0x00877dc0(10) on 0x0153b53c, or already 10 → **state := 10**; tail 0x0072c980 |
| 13 | 0x0072db0c | 0x0059fa10 false → tail 0x0072c980; else copy manager +0x35cc into **0x0153b52c** (the chain target, with 0x007b4750(0x44) on 0x012fced8) unless bit 7 of +0x35da; 0x0072a160; kind 2 → 0x0071ff60(1), marker "CS_STATE_STOP" (1), 0x0071ffd0(1) |
| 14 | 0x0072db85 | 0x012f5e34 clear and 0x00722e00 true → **state := 0xf**, 0x012f5e34 := 1; else 0x012f5e34 := 0 |
| 15 | 0x0072def5 | nothing (0x007258a0 owns state 0xf, §1.2) |

The four undumped entries are **HIGH CONFIDENCE** by elimination: the mnao listing has four
remaining case bodies, each tagged with the state it serves and none reachable from the 16 printed
targets:

| State | Body | Summary |
|---|---|---|
| 0x10 | 0x0072dbb9 | 0x00720040, (kind 2 → 0x00a73ca0), 0x0085fbf0; 0x0153b543 := 0; 0x0153b6b5 := (0x0153b4d0 and 0x01503eba); 0x0153b6b4 := (kind == 1); 0x007281f0; **state := 0x11** |
| 0x11 | 0x0072dc40 | 0x00720040 or not 0x00877b20; 0x0153b6a8 → 0x00861950 / 0x0085bd00; with bit 7 of 0x0153b524: release "cutscene_pause_spawning" (0x00bbc1a0) when 0x0153b558, kind 2 → 0x00b8fee0 / 0x00b90900, the eight 0x009075e0 / 0x00905e70 / 0x00905fc0 / 0x00905f60 calls with 3 then 1, 0x0090fa60(0,0); kind 2 → marker "CS_STATE_STOPPED" (1); 0x0153b543 := 0; **state := 0x12** |
| 0x12 | 0x0072dd44 | 0x00dafa90 false; marker "CS_STATE_FINAL_STREAMING" (0); without bit 7 of 0x0153b524 the same spawning/audio restore block; then every world object in the list 0x03171a64 (+0x1b0 / +0x1a8) that passes 0x004d47e0 and lacks bit 1 of +0x7a gets 0x008d55b0; **state := 0x13** |
| 0x13 | 0x0072de4f | 0x00720040 or not 0x00877b20; if 0x0153b6b5: **fade-in** 0x0059fc40(0x012f5d28, 0, 1) when 0x0153b6b4 (kind 1) else (500, 0, 0); clear both; 0x007f1070(0); 0x008798e0(0x0153b53c); 0x0153b53c := 0; free "cutscene_virtual_pool"; 0x0153b518 := 0; 0x0153b528 := 0; 0x025f4ba1 := 0; **state := 0** |

Other writers of the state seen in the two jobs: 0x00725df0 writes 2 (scene not yet loaded), 3 and a
register value; 0x0072bfc0 writes 13; 0x00720830 writes 11; 0x00722f10 writes 6; 0x00725670 writes
5; 0x007258a0 writes 7 and 0x10. The reference list for 0x0153b520 is capped (92 uses in 50
functions), so this is not the complete writer set. **OPEN:** who writes 1, 9, 12 and 14, and the
bodies of 0x0072c790 / 0x0072bff0 / 0x0072c260 / 0x0072c980 / 0x0072d030 / 0x00729150.

### 2.2 Reading of the states — **HIGH CONFIDENCE** for the grouping, names where the strings give them

| States | Phase |
|---|---|
| 0 | idle; the zscene machine runs under it (promotion in 0x007258a0, completion in 0x0072d660) |
| 1 | start requested; moves to 3 once the player exists ("CS_STATE_FADING_OUT" marker opened for story cutscenes) |
| 2 | scene load requested; waits for the zscene to reach state 2, then 0x00725df0 continues (it writes 3) |
| 3 | fading out; → 4 when the player-side checks pass |
| 4 | preload: 0x007258a0 mounts the preload packfiles and sets 0x0153b543; 0x0072d660 then moves to 5 |
| 5 | 0x007258a0: start the resource loads (→ 6), or for a loaded zscene → 7 |
| 6, 7, 8 | loading/setup chain (0x0072c790, 0x0072bff0, 0x00720200 + 0x0072c260) |
| 9 | playback start (0x00729150) |
| 10, 11, 12 | playing (0x0072c980 each frame; 11/12 are sub-states that return to 10) |
| 13 | "CS_STATE_STOP": playback ended; the chain target is captured |
| 14 | waits for 0x00722e00 → 15 |
| 15 | 0x007258a0: either chain into the next cutscene (→ 5) or begin teardown (→ 0x10) |
| 0x10 | release/unlock stage (0x00a747a0) → 0x11 |
| 0x11 | "CS_STATE_STOPPED": world/audio restore → 0x12 |
| 0x12 | "CS_STATE_FINAL_STREAMING": world objects re-streamed → 0x13 |
| 0x13 | fade-in request, manager freed → 0 |

The marker calls 0x00707410(flag, name) take the state names "CS_STATE_FADING_OUT", "CS_STATE_STOP",
"CS_STATE_STOPPED", "CS_STATE_FINAL_STREAMING" and, in §1.2, "salloc_ingame_cutscene_unlock_async_poll".
**HYPOTHESIS:** a profiling or memory-tracking scope marker (flag 1 = open, 0 = close). The names are
the developers' state names and are used here only as labels.

The fade machine's "cutscene state not in 10..13" gate (mnao §2.3 block C) and the byte 0x0153b525
of §1.2 therefore both mean "a cutscene is playing".

---

## 3. 0x0073bfb0 — what `cutscene.xtbl` gives, and where the entry fields really come from (answers Q2)

### 3.1 The table file itself — **CONFIRMED — disassembly**, full listing

0x0073bfb0 takes eight arguments: the table file name (the cutscene init passes "cutscene.xtbl"),
a container name, the element-filter name ("main" from the init; the "%s_%s" patch-container name
from the second call), an allocator/context object (virtual slots +0x38 allocate, +0xc / +0x5c /
+0x18 / +0x80 describe it), two further values, a platform index and a byte. Its callers are the two
sites in 0x0072d330 (mnao). It returns at once when **both** option bytes 0x0153ba00 and 0x0153ba01
are set.

1. It allocates two scratch objects from the context (0x4c008 and 0x9478 bytes; 0x0073a590 and
   0x0073a5e0 turn them into free-lists of 0xfff and 0x1f3 16-bit slots) and fills them from two
   other tables: 0x00739a80 reads "effects.xtbl" (every `Effect` element's `visual` and `name`
   texts) and 0x00739b20 reads "items_3d.xtbl" (every `Item`'s `Name`, `character_mesh` or `Mesh`,
   and the `Props`/`Prop` names whose `Flags`/`Flag` text says "Attach by default", with the
   platform suffixes "rig_pc" … / ".ccmesh_pc" / ".csmesh_pc"). These are lookup tables for the
   per-scene parse in §3.4; they are not stored in the scene entries.
2. It opens the table file (0x00dac9a0; the "Table" root). The number of `cutscene` elements
   (0x00dc5150) must be 1..200, else the file is closed and nothing is built.
3. For each `cutscene` element it reads the text of its `name` child and applies the filter:
   - filter null or "main" (case-insensitive): accept the name unless it **starts with "dlc" or
     "patch"** (3- and 5-character case-insensitive prefix tests);
   - any other filter string: accept the name only when it **starts with the filter string**
     (prefix test of the filter's length).
   Accepted names are heap-copied (0x00a74910 on the context) into a local list of at most 200.
4. The file is closed. **Only for the null/"main" filter** the scene table is allocated:
   0x00723d60(accepted count + 12). 0x00723d60 (re-read here) caps the capacity at 200, takes
   4 + capacity × 0xf8 bytes from the heap object 0x01493a28, runs the element constructor
   0x007233c0 on every slot, and sets count 0x0153b29c := 0, capacity 0x0153b298 := capacity, base
   0x0153b294 := block + 4; it does nothing if a table already exists. The patch-container calls
   therefore **append** into the 12 spare slots (**HIGH CONFIDENCE** that the +12 is for them).
5. It opens a streaming request group with 0x006f70f0 (record: the container name, the context, the
   platform index, the byte 0x0149365c, a 16-bit 0x100). Then for every accepted name it builds
   `<name>.cte_xtbl`, opens that file (0x00dac9a0 with the context); when the file is missing the
   name is skipped (no entry is appended). Otherwise:
   - 0x0073a310 reads the file's `Cutscene` element's `CutsceneType` text (§3.2) and yields the
     resource type code 0x12 (for "Story", or for "Zscene" when 0x0153ba02 is set) or 0x13 (all
     other values), and the byte "not a Designer scene".
   - the entry is appended with 0x00723e40(name) (name copy at +0x0, CRC-32 at +0x4; refused when
     the table is full, in which case the file is closed and the name skipped).
   - **primary resource** (skipped when 0x0153ba01 is set and the scene is not of type "Designer"):
     0x006f6c20 opens a request record for `<name>` with the type code, flags 0x800, slot -1 and
     the platform index; 0x006f7330 adds the `.cte_xtbl` path as a type-0x17 resource in the
     category "cutscene"; 0x0073ada0(file, entry, 0, …) adds the scene's own resources (§3.4);
     0x006f6f10 closes the request and its return value is stored at **entry +0xc**.
   - **female variant** (for every non-"Designer" scene): when 0x0153ba00 is set, +0x10 := +0xc.
     Otherwise the same three steps are repeated for the name `<name>_f` (0x006f6c20 / 0x006f7330 /
     0x0073ada0(file, entry, **1**, …) / 0x006f6f10) and the result is stored at **entry +0x10**.
     For a "Designer" scene +0x10 := +0xc with no second request.
   - when 0x0153ba01 is set, +0xc := +0x10 afterwards.
   - the `.cte_xtbl` file is closed (0x00720d60 on the request record).
6. The request group is closed with 0x006f75c0 (record carrying the caller's last byte and 0x1000).

So **`cutscene.xtbl` contributes only the scene names** (element `cutscene`, child `name`), the
dlc/patch split, and the table size. Everything else in the 0xf8-byte entry comes from the scene's
own `<name>.cte_xtbl` file through 0x0073ada0. The mnao statement "parsed from cutscene.xtbl" is
corrected accordingly.

**0x006f6f10** (re-read): the closing step calls 0x00db3fd0(record byte +1, +4, +8) and keeps the
result in 0x014f6b8c; when the record carries a name at +0xc it also runs 0x00c9b840 and
0x00db1690(name, 0x014f6b80, 0); when 0x014f6b84 is set and the record's first byte is clear it runs
0x00daff10 on the handle. The value stored at +0xc / +0x10 is thus a **request-group handle** that
covers every resource added between 0x006f6c20 and 0x006f6f10 — the scene file plus its lightset,
textures, effects, animations and item meshes — which is what the handle manager later loads
(0x00dafea0) and classifies (0x00dafb60). **HIGH CONFIDENCE** for the label "request group";
**CONFIRMED** for the data path.

### 3.2 `CutsceneType` → entry kind +0x8 — **CONFIRMED — disassembly** (0x0073ada0 and 0x0073a310)

| `CutsceneType` text (case-insensitive) | +0x8 | type code from 0x0073a310 |
|---|---|---|
| "Zscene" | 1 (2 when the option byte 0x0153ba02 is set) | 0x13 (0x12 when 0x0153ba02 is set) |
| "Story" | 2 | 0x12 |
| "Designer" | 0 | 0x13 |
| any other text | 2 | 0x13 |
| element absent | unchanged (constructor default; 0x007233c0 not dumped) | 0x13 |

So kind 1 — the only kind `zscene_prep` and the auto-prep accept — is exactly the "Zscene" type,
and kind 2 — what the cutscene machine's 0x00721ba0 tests — is a story cutscene. The byte
0x0153ba02 makes zscenes behave as story cutscenes (**HYPOTHESIS:** a debug option). The three bytes
0x0153ba00 / 0x0153ba01 / 0x0153ba02 are zero-filled and no writer appears in these dumps
(**OPEN**).

### 3.3 The two handles and the variant rule — **HIGH CONFIDENCE** upgrade

+0xc is the request group for `<name>` and +0x10 the one for `<name>_f`, built with the "female"
flag so that animation files are taken as `f_<file>` when such a file exists (0x00da90d0 is the
existence test, **HIGH CONFIDENCE**). The selection rule 0x0071fee0 (re-read) takes +0x10 when +0xc
is not live and either +0x10 is live or the local player's byte +0xa41 equals 1. The mnao
HYPOTHESIS "gender variant" is therefore **HIGH CONFIDENCE**: +0x10 is the female-player variant of
the scene and +0xa41 is the player's "female" flag.

### 3.4 0x0073ada0 — the `.cte_xtbl` parse into the entry — **CONFIRMED — disassembly**, full listing

Arguments: the open file, the entry, the "female" flag, the platform index (0 selects the "_pc"
suffixes: ".csc_pc", ".anim_pc", ".cvbm_pc"), and a value passed through to the item-mesh helper.
It finds the `Cutscene` root element (nothing happens without it) and then:

| Element (child of `Cutscene`) | Entry field | Type | What is read |
|---|---|---|---|
| (always, first) | +0x18, +0x1c | dword, dword | set to the two .rdata dwords 0x01148818 / 0x0114881c (0 in the file); overwritten at promotion by the per-object parameters |
| (always, first) | +0xac | byte | := 0 |
| `CameraScript` | — | — | only when 0x0149365c is **clear**: the text, with its extension replaced by ".csc_pc", becomes a type-0x18 resource request (prefixed "f_" for the female pass when that file exists); nothing stored in the entry |
| `SceneLightset` | +0xac, +0xad | byte, char[64] | +0xac := 1 and the text copied (strncpy, 64) into +0xad; the text is also a type-0x19 request |
| `CutsceneType` | +0x8 | dword | the mapping of §3.2 |
| `Characters` / `Character` (not for "Zscene") | +0x20 (array), +0x24 (capacity), +0x28 (count) | dword[] | for each character whose `Name` is not "#PLAYER#": if its `IsAngel` flag is set (0x00dac510) and the scene is not "Designer", the three fixed npc mesh names are tried; otherwise its `Mesh` text and its `Killbane` child text. Each name is resolved with 0x00be3360 and the object at (result +0x8) +0xd8 is appended to the +0x20 list by 0x00739370 until the list is full |
| `Vehicles` / `Vehicle` (not for "Zscene") | +0x20 / +0x28 | dword[] | each vehicle's `Mesh` text; the two VTOL mesh names trigger five effect preloads once (0x0073a020); the variant is resolved by 0x00acb350(vehicle, "mesh", "Variant") → 0x00ac18f0 and its id appended to the +0x20 list when not already present and the list is not full |
| `Soundtrack` | +0xf0, +0xf4 | dword, dword | the text (≤ 63 chars) is split at the first ':'; the character before the colon is cut and the right part starts two characters after it (so the written form is `left : right`); +0xf0 := 0x00462960(left), +0xf4 := 0x00462960(right). Both 0 when the element is absent or has no colon |
| `Items` / `Item` (not for "Zscene") | — | — | each `Mesh` text → 0x0073aab0 (item mesh request through the items table of §3.1) |
| `Options` / `Option` (not for "Zscene") | — | — | `Name` "Load Texture": its `String` child + ".cvbm_pc" → type-0x10 request (slot 0xff); `Name` "TOD Mission Override": its `String` → 0x0073a210 |
| `Shots` / `Shot` | — | — | per shot: `Options`/`Option` as above ("Override LUT": `String` + the suffix at 0x0132ab18[platform] + ".cvbm_pc" → type-0x10 request); `CSEffects`/`CSEffect` `Name` (not for "Zscene") → effect lookup (0x00dab330 hash, 0x0073a420 in the effects table) → type-0xf request of the effect's resource (0x005c48f0 result +0x41); only when 0x0149365c is **clear**: `Characters`/`Character` `Options` `ItemLeft` / `ItemRight` → 0x0073aab0, and `Characters`/`Character`/`Animations`/`Animation` `Filename` and `Vehicles`/`Vehicle`/`Animations`/`Animation` `Filename` → ".anim_pc" type-0x15 requests ("f_" variant when the female flag is set and the file exists); always: `Items`/`Item`/`Animations`/`Animation` `Filename` → ".anim_pc" type-0x15 request (same "f_" rule) |
| (end, not "Zscene" and not "Designer") | — | — | 0x0073a090(type code) |

Points the listing settles:
- **The +0xf4 handle is the soundtrack.** The mnao HYPOTHESIS "a sound or video played over the
  load" becomes **HIGH CONFIDENCE: the cutscene's soundtrack stream** (0x00462960 resolves a name
  to an audio id — HYPOTHESIS for that helper; the pool 0x0153b730 is where 0x007315a0 starts it).
  +0xf0 is the left half of the same element (not read by anything dumped so far).
- **The +0x20 list** (array pointer, capacity +0x24, count +0x28) holds the character and vehicle
  resources of a story cutscene; 0x00722f10 starts loading every one of them (§1.3). Zscenes have
  none.
- Entry fields not written by the parser (+0x30..+0xab except what the constructor sets,
  +0xb0..+0xef) remain **OPEN** (constructor 0x007233c0 not dumped).
- Since 0x0149365c is 1 in the shipped executable (§4.4), the `CameraScript` request and the
  per-shot character/vehicle animation requests are **never made** by the shipped game; those
  files are expected to be inside the scene's own package.

### 3.5 Request records — **HIGH CONFIDENCE** reading of 0x006f6c20 / 0x006f7330

Both take a stack record whose first dword is a name, a byte at +4 is the resource type (0x12/0x13
scene, 0x17 cte_xtbl, 0x18 camera script, 0x19 lightset, 0x15 animation, 0x10 texture, 0xf effect),
a dword at +8 the category name "cutscene", and further bytes/dwords (a slot number, -1 or 0xff,
a "preload" byte). Their bodies were dumped but describe the streaming layer, not cutscene
behaviour; they are not interpreted further here.

---

## 4. The six fade callers: who drives, who requests (answers Q4)

The short answer: **none of the six requests a fade.** They drive the fade machine (per-frame
update, init) or write values it reads. Fade **requests** in the executable remain the ones already
known: the cutscene start 0x00725df0 (fade-out, 500 ms or 0x012f5d28, mnao §4.3), the cutscene
state-0x13 case (fade-in, §2.1), and the Lua wrappers `fade_out` / `fade_in` (§26.24). Each caller:

### 4.1 0x005d14b0 — the per-frame system update — **CONFIRMED — disassembly**

Caller: 0x005d3ac0 (not dumped; it is also the caller of the startup routine 0x005d1a30 and of
0x00706be0, **HIGH CONFIDENCE: the main loop**). The body is a straight list: 0x00754410 (no-op
stub), 0x00dc4480 → when true and (0x024d4461 clear or 0x00867830 false) 0x00dc4440(500); 0x00754410,
0x00707710, 0x00dc4470 == 1 and 0x00dc4480 false → 0x00db8d40; 0x00845180, 0x00707440, 0x007c5740,
**0x0059fe70 (the fade per-frame routine)**, 0x00707930, 0x0058f2d0, 0x0059f270, 0x00db9970(0, 0,
the float 0x012ebc08 (15.0) when 0x01493664 is set else 0, 0), 0x00754410, 0x00706750,
0x00dc2e80, **0x00706be0(0)** (the mode-stack pump, §4.6) and, when that returns a non-zero current
mode, 0x005d4260; it returns (current mode == 10). So the fade machine is updated once per frame
from here, unconditionally, before the mode stack is pumped.

### 4.2 0x007a82c0 — a mode callback that also runs the fade update — **CONFIRMED — disassembly**

Its only reference is a data reference from 0x007a8530 (a function pointer stored into a mode
record; **HIGH CONFIDENCE:** it is one mode's per-frame callback in the table at 0x01503bc0,
§4.6). Body: 0x014938ac := 1. If mode **5** is on the mode stack (0x00706ad0(5)): run
**0x0059fe70**; then if the fade target is 3 and 0x0059fa10 is false → return. Then it services a
two-slot ring of 0x3c-byte records at 0x0224206c (index 0x0224205c, per-record "pending" byte at
+0x36 = 0x022420a2): a set flag 0x02242065 → 0x0085bce0(record), 0x00da38e0, 0x00564c20 and clear;
if the other slot is pending → switch to it (index (i + 1) mod 2), the same three calls, return;
else if the top mode is **8** and 0x02242058 → 0x007adfb0; then if 0x007a8140 is false: a pending
current slot → 0x009e69d0 with the record's fields (+0 dword, +4 and +0x10 strings, four bytes
+0x30..+0x33) and clear it; otherwise, once (0x02242064), 0x007a7c60 and 0x00706e70 (a mode-stack
request, §4.6). If mode 5 is not on the stack and the top mode is 8 → 0x007a7c60 and 0x00706e70.
**HYPOTHESIS:** this is the loading-screen mode's callback (mode 5 = loading, mode 8 = its parent),
and the ring holds queued load-screen panels; it only matters here because it is the second caller
of the fade update, active while mode 5 is on the stack — so during a loading screen 0x0059fe70 can
run **twice per frame** (from 0x005d14b0 and from here). The fade routine is idempotent within a
frame except for its clock comparisons, so this changes no spec'd behaviour.

0x0059fa10 (16 bytes, between the target getter 0x0059f9f0 and the one-instruction 0x0059fa20) is
tested by this routine and by 0x0072d660 in states 1, 4 and 13. **HYPOTHESIS:** "the fade has
settled" (state == target); next-dump item.

### 4.3 0x005d2400 — the UI/front-end bring-up that runs the fade init — **CONFIRMED — disassembly**

Caller 0x005d390b (no function). Each step is guarded by 0x00707800 ("abort requested", returns
true to stop) and preceded by 0x005d18e0. In order: 0x00e0d0b0 with four callbacks (0x00e0e1f0,
0x00e0e6e0, 0x00e0e500, 0x00e0e6a0; HYPOTHESIS: the UI Lua state's set-up, by their neighbourhood
with the call builder 0x00e0ca80), **0x008489e0** (the UI subsystem init — CONFIRMED by its
reference list to be the caller of the resolution writer 0x00e23910, §5.2); then with 0x0149365c set, mount "interface_startup.vpp" (0x00709530); create the
heap "interface_tmp_pool" (0x00db64a0 on 0x01493a78, 16 bytes); load the interface pack "patch"
(0x007b3820, when 0x0149365d is set) and "main" with the byte 0x0149365c; 0x007096f0 when
0x0149365c; 0x007b4d70 on the set object 0x012fced8; 0x007c4c90; 0x00715f20; **0x0059fa30 (fade
init: loads the "screen_fade" document, state := 2)**; 0x007adce0(1); 0x00843220(1); two peg loads
"interface-backend.peg" and "ui_bms_dlc_al.peg" (0x005d76a0); the document "save_warning"
(0x007b1cb0, mode 1); 0x007b14c0; 0x007b09f0; 0x014ff67e := 1. So the fade init runs once, during
front-end start-up, after the UI Lua state and the UI subsystem exist and the "main" interface pack
is loaded — which is why `screen_fade` can be found by name at that point.

### 4.4 0x005d1a30 — engine start-up; the writer of 0x0149365c — **CONFIRMED — disassembly**

Caller 0x005d3ac0 (the main loop's owner, §4.1). This is the engine's start-up routine (it creates
the graphics device with the window title, the "vlib" allocator, the "=startup" package and so on).
Its first stores are **0x0149365c := 1, 0x0149365d := 1, 0x0149365f := bit 6 of 0x014937fc,
0x0149365e := 0**, unconditionally (the register holding 1 is used for all four). The reference list
of 0x0149365c is capped (96 uses in 47 functions) but shows no other writer in its listed part, and
every dumped reader treats it as a mode byte (packfile mounting, cache paths, the preload packfiles,
0x0073ada0's skipped requests). **HIGH CONFIDENCE:** 0x0149365c is a "load from packfiles" /
shipping-build flag, fixed at 1 in the retail executable; **CONFIRMED** consequence: the Lua query
`sfx_use_load_images` (which returns this byte) is always true, and the fade machine's load-images
step (0x012e6ab4 := now + 6000 ms) always runs after a loading logo. The mnao label "show
load-screen images preference" is replaced by this.

### 4.5 0x0059faf0 — the auto-save counter — **CONFIRMED — disassembly**

One byte argument: non-zero → 0x013effd4 += 1; zero → 0x013effd4 -= 1, clamped at 0. Callers:
0x00705d50 (at 0x00705d9c), 0x00b94510 (at 0x00b945b5), and two sites outside functions
(0x007aea99, 0x00b94d63); none dumped. The reader 0x0059fb20 compares the counter with its argument.
So the auto-save indicator is a reference count raised and lowered by the save system
(**HYPOTHESIS** for the label; the bodies are not read).

### 4.6 0x00706be0 — the mode stack — **CONFIRMED — disassembly**, full listing; "mode 4" stays OPEN

Data: the stack 0x01503b50 (dwords; top index 0x012f4a80, -1 when empty, at most 16 entries), the
request queue at 0x012f4a94 (pairs of dwords: operation, mode; count 0x012f4a90; 0x012f4a88 points
at it), the per-mode table at 0x01503bc0 (9 dwords per mode: +0 "enter" callback taking the
previous mode, +4 "leave" callback taking the mode below, +8 "may leave" predicate taking the mode
below; a byte at +0x20 (0x01503be0) that the pump clears for the bottom mode), and the "changed"
byte 0x012f4a84.

0x00706be0(x): with x == 0 and a non-empty queue it first calls 0x005d7d10(0), then processes the
queue in order: operation 1 (pop) — if the top's "may leave" predicate exists and returns false,
processing stops with the request left queued; else the current top's "leave" callback is called
with the mode below, the top index drops by one, the queue shifts down. Operation 0 (push) — ignored
when the mode is already on the stack or the stack has 15 entries; else the mode is pushed, the
queue shifts, and if the new top differs from the old one and has an "enter" callback it is called
with the old top. Each processed request sets 0x012f4a84. When the queue empties and the stack is
empty, the +0x20 byte of mode 0x01503b50[0] is cleared. With x ≠ 0 (or after the queue) it calls
0x007068d0(x) (not dumped). It returns the top mode (0 when the stack is empty). 0x00706ab0 is the
plain "top mode" getter and 0x00706ad0(m) the "is m on the stack" test; 0x00706e70 (called by
§4.2) is one of the four queue-append helpers 0x00706e00 / 0x00706e70 / 0x00706ec0 / 0x00706f40.

Nothing in the dumps names the modes. Facts available: the main loop treats top mode 10 as
significant (§4.1); the loading callback tests modes 5 and 8 (§4.2); the fade per-frame routine
suspends its logo/images schedule under mode 4 (mnao). **OPEN:** what mode 4 is; a dump of the mode
registrations (0x00706a40 and its callers, which fill 0x01503bc0) would name them by their
callbacks.

---

## 5. UI width and height: where they come from, when they are written (answers Q5)

### 5.1 The storage — **CONFIRMED — disassembly**

Two global dwords **0x02a5a180 (width) and 0x02a5a184 (height)** are the master copy; the
per-thread 12-byte record of 0x00e236f0 (thread context +0x670) holds a copy of them at +4 / +8 —
the two integers `vint_is_std_res` divides. The record's first dword is unrelated to resolution:
0x00e23770(x) stores x there after calling the hook at slot +8 of 0x02a5a008 when the value changes
(0x02a5a004..0x02a5a014 are five callback slots set by 0x00e225e0..0x00e22620; the callers of
0x00e23770 are three sites in 0x00e26a30). 0x00e237f0 copies the two globals into the record;
0x00e237e0 (nzxf) returns the record + 4.

### 5.2 0x00e23910 — the UI subsystem init writes them first — **CONFIRMED — disassembly**

Caller: 0x008489e0 (the UI init that 0x005d2400 runs, §4.3). Arguments: six; it returns 0 unless
the five callback slots 0x02a5a004..0x02a5a014 are all set and the fifth argument is non-null.
Then: 0x02a75578 := fifth argument, 0x02a7557c := sixth argument, **0x02a5a180 / 0x02a5a184 := the
two dwords at the second argument** (a width/height pair), 0x00e237f0 (copy into the calling
thread's record), then twenty-five subsystem initialisers (0x00e2a8b0 … 0x00e33690), 0x00e2b420
(first argument, 0x02a7557c), **0x00e23000(width, height)** (the display-mode ladder), the
standard-resolution test 0x00e2ad30 on the pair, 0x00e2ab10 with its result, 0x00e22680, and
returns 1.

### 5.3 0x00e23a20(width, height) — the resolution-change path — **CONFIRMED — disassembly**

Caller: 0x005df6e0 (not dumped; **HYPOTHESIS:** the renderer's display-mode / window-resize
handler). If the pair equals the stored globals nothing happens. Otherwise it remembers whether
the old aspect (old width / old height, double) was below 1.5 (the double at 0x012a2d30) and
whether the new one is, stores the new **0x02a5a180 / 0x02a5a184**, writes them into the calling
thread's record (+4 / +8), recomputes the mode 0x0132bd80 with **0x00e23000(width, height)**,
evaluates 0x00e2ad30 on the new pair and stores the result through 0x00e2ab10, then — if the UI Lua
state exists and defines `vint_lib_init_constants` as a function — calls **`vint_lib_init_constants()`**
(no arguments, no results, no document context). Finally, when the "below 1.5" class changed, it
runs 0x00e21670(0x02a7557c): for every UI document in the ring 0x02a4d17c it forms the name
`<document name>_reset` (the name at +0x488, 128-byte buffer), and if the UI Lua state defines that
global as a function, calls it with no arguments in that document's context (+0x580 handle in the
builder's +0x14), after 0x00e2ace0 / 0x00e215a0 on the document.

### 5.4 0x00e2ad30 and the layout index — **CONFIRMED — disassembly**, label HIGH CONFIDENCE

0x00e2ad30(&pair) returns true when the mode 0x0132bd80 (read through 0x00e230a0) equals 2, or when
width / height (single precision) is below 1.5 (the single at 0x011743b0); otherwise false. (It also
compares 1.5 with the single at 0x02a755dc, which is zero-filled and has no other reference, so that
compare always takes the same branch.) This is the same decision rule as the Lua `vint_is_std_res`
(§26.26) in C. 0x00e2ab10(v) stores v into **0x0132c0ac** when v is below the count 0x02a7c87c
(written by 0x00e2a980) and returns whether it did. **HIGH CONFIDENCE:** 0x0132c0ac is the current
layout index — 1 for the standard layout, 0 for the wide one — chosen at UI init and on every
resolution change; `vint_is_std_res` recomputes the same rule from the per-thread copy instead of
reading it.

### 5.5 When, in one line

Width and height are written **once at UI init** (0x00e23910, from the pair its caller passes — the
display size at start-up, HIGH CONFIDENCE) and **on every change** (0x00e23a20). Both writers
immediately recompute the mode ladder and the layout index; the change path additionally calls the
Lua `vint_lib_init_constants()` and, when the standard/wide class flips, every document's `_reset`
function. The Lua queries read the per-thread copy, which is only filled on the thread that ran the
writer (**HIGH CONFIDENCE:** the UI Lua state runs on that thread; a reader on another thread would
see 0/0).

---

## 6. The safe-frame constants (answers Q6) — **CONFIRMED — disassembly**

`ptrs` read both constants as two dwords, low then high:

| Address | Low dword | High dword | IEEE double | Exact value |
|---|---|---|---|---|
| 0x0115ba60 | 0x40000000 | 0x3fb33333 | 0x3FB3333340000000 | 0.07500000298023224 = the single 0.075f (0x3D99999A) widened to double |
| 0x0116dfc0 | 0xa0000000 | 0x3fed9999 | 0x3FED9999A0000000 | 0.925000011920929 = the single 0.925f (0x3F6CCCCD) widened to double |

Both are the decimal constants 0.075 and 0.925 as stored by a compiler that wrote them as single
precision and widened them; both are shared literals (other users: 0x007ef720; 0x008f4fd0 and
0x00903140), consistent with generic 7.5 % / 92.5 % fractions.

**What `vint_get_safe_frame` returns.** With a = the object's +0x8 and b = its +0xc (HIGH
CONFIDENCE from nzxf: the render width and height) and the x87 order (c1·a, c1·b, c2·a, c2·b):

1. round(a × 0.07500000298023224) — the left safe edge, 7.5 % of the width;
2. round(b × 0.07500000298023224) — the top edge, 7.5 % of the height;
3. round(a × 0.925000011920929) — the right edge, 92.5 % of the width;
4. round(b × 0.925000011920929) — the bottom edge, 92.5 % of the height.

So the safe frame is the central 85 % of the screen on each axis (the "title safe" margin). The
mnao HYPOTHESIS "left/top and right/bottom edges in pixels" is now **HIGH CONFIDENCE** (only the
width/height labels of +0x8/+0xc remain inherited).

**Rounding, and why the exact bit patterns matter.** For every integer dimension below 2^23 the
products with these constants never land exactly on a half-integer (0.075f = 10066330 / 2^27 and
0.925f = 15519949 / 2^24 have odd numerators after removing powers of two), so the x87 rounding
mode never decides a tie: the result is simply the nearest integer. A host that used the decimal
constants 0.075 and 0.925 instead would hit exact ties at dimensions ≡ 20 (mod 40) and, under
round-half-to-even, differ from the engine: e.g. at 1440 × 900 the engine returns (108, 68, 1332,
833) while 0.925 × 900 = 832.5 would round to 832. Team B must multiply by the widened single
values (or by 0.075f / 0.925f promoted to double) and round to nearest.

Worked values (CONFIRMED arithmetic on the CONFIRMED constants): 1280 × 720 → (96, 54, 1184, 666);
1920 × 1080 → (144, 81, 1776, 999); 1366 × 768 → (102, 58, 1264, 710); 1440 × 900 → (108, 68,
1332, 833).

---

## 7. Comparison with the current spec text (§26.24 – §26.26 after the mnao edits)

| Section | Verdict | Detail |
|---|---|---|
| §26.24 globals row 0x0149365c | corrects | Not a "show load-screen images" preference: 0x005d1a30 (engine start-up) sets it to 1 unconditionally; it is a packfile/shipping-mode byte, so `sfx_use_load_images` is always true in the retail executable. |
| §26.24 per-frame routine callers | answers | 0x005d14b0 is the main loop's per-frame system update (every frame); 0x007a82c0 is a mode callback that runs it again while mode 5 is on the stack. Neither requests a fade. |
| §26.24 init caller | answers | 0x0059fa30 runs from the front-end bring-up 0x005d2400, after the UI Lua state, the UI subsystem init (0x008489e0) and the "main" interface pack. |
| §26.24 mode 4 | still OPEN | 0x00706be0 is the mode-stack pump (mechanics now CONFIRMED); the modes are not named in any dump. |
| §26.24 auto-save counter | confirms | 0x0059faf0(flag) increments / decrements (clamped at 0); callers not dumped. |
| §26.24 fade requesters | unchanged | 0x00725df0 (out) and the cutscene state-0x13 case (in), plus Lua. The state-0x13 case's arguments are confirmed from the jump-table mapping: (0x012f5d28, 0, 1) for a zscene-kind record, (500, 0, 0) otherwise. |
| §26.25 lifecycle step 3 "who promotes" | answers; closes OPEN | 0x007258a0, per frame, in cutscene states 0 and 2, through 0x00720320 → 0x00720410. A bare Lua `zscene_prep` is promoted in state 0. |
| §26.25 step 5 "parsed from cutscene.xtbl" | corrects | `cutscene.xtbl` gives names only (and the dlc/patch split); every entry field comes from `<name>.cte_xtbl` through 0x0073ada0. |
| §26.25 entry offsets | extends | +0x8 from `CutsceneType` (Zscene = 1, Story = 2, Designer = 0, other = 2); +0xc / +0x10 = request-group handles for `<name>` and `<name>_f`; +0xac / +0xad from `SceneLightset`; +0xf0 / +0xf4 from `Soundtrack` (left / right of " : "); +0x20 / +0x24 / +0x28 = character/vehicle resource list (story cutscenes only). |
| §26.25 gender HYPOTHESIS | upgrades | +0x10 is the female variant (`_f` request, `f_` animation files); +0xa41 is the player's female flag. HIGH CONFIDENCE. |
| §26.25 transition stream HYPOTHESIS | upgrades | +0xf4 is the soundtrack's audio id (HIGH CONFIDENCE); the stream started at promotion is the cutscene soundtrack. |
| §26.25 capacity | refines | capacity = accepted "main" names + 12, capped at 200; patch containers append into the spare slots. |
| §26.26 width/height writers | answers; closes OPEN | 0x02a5a180 / 0x02a5a184 are the masters, written by the UI init 0x00e23910 and the change path 0x00e23a20; the per-thread record is a copy. |
| §26.26 change-time Lua calls | extends | `vint_lib_init_constants()` on every change; `<doc>_reset()` for every document when the standard/wide class flips. |
| §26.26 safe-frame constants | answers; closes OPEN | 0.075f and 0.925f widened to double; the four results are the 7.5 % / 92.5 % edges; ties never occur. |
| §26.26 mode ladder | unchanged | Still only 0x00e23000; its two callers are now read. |

---

## 8. Next dump (addresses; nothing above depends on them)

- **Cutscene jump table, last four entries:** `ptrs count:20 0x0072defc` (states 0x10..0x13 are
  HIGH CONFIDENCE by elimination).
- **The two per-frame drivers:** `func 0x00702a50 0x00bdbf30` (what sits between 0x0072d660 and
  0x007258a0, and whether both run every frame).
- **Fade settled predicate:** `func 0x0059fa10` (16 bytes; tested by 0x007a82c0 and 0x0072d660).
- **Mode names:** `func 0x00706a40` and `xref 0x00706a40` (the registrations that fill
  0x01503bc0; naming mode 4, 5, 8 and 10 by their callbacks), plus `func 0x005d7d10 0x007068d0`.
- **Released-handle class** (settles §1.5's HIGH CONFIDENCE): `func 0x00dafad0 0x00dafb60`.
- **Scene-entry constructor and the remaining fields:** `func 0x007233c0`; `xref 0x0153ba00
  0x0153ba01 0x0153ba02` (the three parser option bytes); `func 0x00462960` (the soundtrack name →
  id helper); `func 0x007315a0 0x004639a0` (what the +0xf4 stream is on the pool 0x0153b730).
- **Cutscene machine bodies still unread:** `func 0x0072c790 0x0072bff0 0x0072c260 0x0072c980
  0x0072d030 0x00729150 0x00720040 0x00722e00`; the capped writer list of 0x0153b520
  (`xref xrefs:200 0x0153b520`).
- **UI:** `func 0x005df6e0` (the caller of 0x00e23a20 — what triggers a resolution change) and
  `func 0x00e230a0 0x00e2a980` (mode reader; layout count writer).
- Unchanged from mnao: `func 0x0045d990 0x0045ea70` (audio-id posts), 0x00707170 (caller of the
  table destructor).

---

## 9. Spec changes (exact text for Team A to apply; wording is final)

### 9.1 §26.24 Screen fade state machine

**Source line, replace** "bridge jobs `20261001T020200-team-a-nzxf` and `20261001T114101-team-a-mnao` ("
**with** "bridge jobs `20261001T020200-team-a-nzxf`, `20261001T114101-team-a-mnao` and
`20261001T122853-team-a-nnlt` (".

**Globals table, replace the row** for 0x0149365c **with**:
`| 0x0149365c | 0 (start-up writes 1) | byte: packfile/shipping mode, set to 1 unconditionally by the engine start-up 0x005d1a30 and never cleared; `sfx_use_load_images` returns it, so the load-images step always runs in the retail executable | 0x005d1a30 only |`

**Per-frame routine paragraph, replace** "**Per-frame routine 0x0059fe70** (callers 0x005d14b0 and
0x007a82c0). **CONFIRMED — disassembly, full listing.** Each frame, in this order:" **with**:
"**Per-frame routine 0x0059fe70.** **CONFIRMED — disassembly, full listing.** It is called once per
frame by the main loop's system update 0x005d14b0 (unconditionally, before the mode stack is
pumped) and a second time by the mode callback 0x007a82c0 while mode 5 is on the mode stack
(HYPOTHESIS: the loading screen); the second call changes nothing because the routine only acts on
clock stamps. Each frame, in this order:"

**Mode-gate bullet, replace** "- Mode gate: if the top of the mode stack (0x00706ab0: 0x01503b50
indexed by 0x012f4a80) is 4, reset all four fade stamps and stop (OPEN: what mode 4 is)." **with**:
"- Mode gate: if the top of the mode stack (0x00706ab0: 0x01503b50 indexed by 0x012f4a80) is 4,
reset all four fade stamps and stop. The mode stack is pumped by 0x00706be0 (push/pop requests
queued at 0x012f4a94; per-mode enter/leave/may-leave callbacks in the 9-dword records at
0x01503bc0; a mode already on the stack is not pushed twice; a pop waits while the top mode's
may-leave predicate is false). OPEN: which mode is 4 (no dump names the modes)."

**Init paragraph, replace** "**Init 0x0059fa30 / shutdown 0x0059faa0.** **CONFIRMED — disassembly.**
Init loads" **with**: "**Init 0x0059fa30 / shutdown 0x0059faa0.** **CONFIRMED — disassembly.** Init
runs once from the front-end bring-up 0x005d2400, after the UI Lua state (0x00e0d0b0), the UI
subsystem init 0x008489e0 and the "main" interface pack are up and before the "save_warning"
document is loaded. It loads"

**Host summary, add a bullet** after the `fade_is_fully_faded_out()` bullet:
"- `sfx_use_load_images()` is always true (the byte it returns is fixed at 1 at start-up), so a
  host that models the loading-logo schedule must also model the load-images step."

**Add after the host summary, before HOST-SIDE SUBSTITUTE:**
"**Who requests fades (CONFIRMED — disassembly).** Apart from the Lua wrappers, the executable
requests a fade-out from the cutscene start 0x00725df0 (500 ms or the value at 0x012f5d28) and a
fade-in from the cutscene machine's state-0x13 case (§26.25): 0x0059fc40(0x012f5d28, 0, 1) when
the finished record was a zscene (byte 0x0153b6b4), else 0x0059fc40(500, 0, 0), and only when the
byte 0x0153b6b5 was set at state 0x10 (0x0153b4d0 and 0x01503eba both set). The callers of the
per-frame routine and of init request nothing."

**OPEN — next dump block, replace** its two bullets **with**:
"- `func 0x0059fa10` (the "fade settled" predicate used by the cutscene machine and 0x007a82c0).
- `func 0x00706a40`; `xref 0x00706a40` (mode registrations, to name mode 4); `func 0x005d7d10 0x007068d0`.
- Audio-id posts (HYPOTHESIS check): `func 0x0045d990 0x0045ea70`."

**Review status, replace** the whole paragraph **with**:
"**Review status (2026-10-01): re-derived from the executable (jobs `20261001T020200-team-a-nzxf`,
`20261001T114101-team-a-mnao`, `20261001T122853-team-a-nnlt`): CONFIRMED parts — globals and
initial values, state encoding, both request helpers, both broadcast helpers, the four UI natives
including `Screen_fade_transition_complete`, the completion body, the per-frame routine and its two
callers, init/shutdown and the init's place in start-up, the fixed value of 0x0149365c, the
mode-stack mechanics, the engine-side fade requesters; HIGH CONFIDENCE — the document-id/handle
labels of 0x012e6aa0/0x013effc0, the co-op purpose of 0x53, the script calling the completion
native, 0x0149365c as a shipping-mode byte; HYPOTHESIS — audio-id posts, the auto-save counter's
meaning, 0x007a82c0 as the loading-screen callback; OPEN — which mode is 4, the body of
0x0059fa10.**"

### 9.2 §26.25 zscene lifecycle

**Source line, replace** "bridge jobs `20261001T020200-team-a-nzxf` and `20261001T114101-team-a-mnao` ("
**with** "bridge jobs `20261001T020200-team-a-nzxf`, `20261001T114101-team-a-mnao` and
`20261001T122853-team-a-nnlt` (".

**Globals table, replace the row** for 0x0153b294 / 0x0153b29c / 0x0153b298 **with**:
`| 0x0153b294 / 0x0153b29c / 0x0153b298 | scene table base / count / capacity; entries 0xf8 bytes; capacity = number of accepted "main" names in cutscene.xtbl + 12, capped at 200 (the spare slots take the per-container patch tables) | 0x00723d60 (allocate), 0x00723e40 (append), 0x007231e0 (destroy) |`

**Replace the row** for 0x0153b71c / 0x0153b720 / 0x0153b724 **with**:
`| 0x0153b71c / 0x0153b720 / 0x0153b724 | the scene's soundtrack stream (started at promotion from the entry's +0xf4 audio id on the pool 0x0153b730), its secondary handle, and its 1000-unit timer; completion waits until it ends (status 0x66) or 5 s pass | 0x007315a0, 0x007317a0, 0x007316a0, 0x00731600 (stop) |`

**Add rows** at the end of the table:
`| 0x0153b525 / 0x0153b526 | bytes rewritten every frame by 0x007258a0: "a cutscene is playing" (state 10..13) and "a cutscene is in progress" (state ≥ 2 with a manager); getters 0x00720780 / 0x00720790 | 0x007258a0 |`
`| 0x0153b543 | byte: the preload packfiles are mounted (set in cutscene state 4, cleared at 0x10/0x11) | 0x007258a0, 0x0072d660 |`
`| 0x0153b52c | the next cutscene to chain into (copied from the manager at state 13; consumed at state 15) | 0x0072d660, 0x00729150 (cleared) |`
`| 0x0153ba00 / 0x0153ba01 / 0x0153ba02 | parser option bytes: no female variant (+0x10 := +0xc); female variant only (+0xc := +0x10); treat "Zscene" scenes as story cutscenes (kind 2). All zero; no writer dumped | — |`

**Scene entry list, replace the `+0x8` bullet with**:
"- `+0x8`: kind, from the scene file's `CutsceneType`: "Zscene" → 1 (2 when 0x0153ba02 is set),
  "Story" → 2, "Designer" → 0, any other text → 2. Only kind 1 can be prepared by `zscene_prep` and
  the auto-prep; kind 2 is a story cutscene. **CONFIRMED — disassembly** (0x0073ada0)."

**Replace the `+0xc` and `+0x10` bullets with**:
"- `+0xc`: handle of the streaming request group for `<name>` (the scene resource, type 0x12 for a
  story cutscene / 0x13 otherwise, plus the `.cte_xtbl` file and every lightset, texture, effect,
  animation and item mesh the scene file names). **CONFIRMED** for the data path (0x006f6c20 /
  0x006f7330 / 0x006f6f10 in 0x0073bfb0); HIGH CONFIDENCE for the label "request group".
- `+0x10`: the same for `<name>_f`, built with animation file names prefixed `f_` where such files
  exist — the female-player variant. For a "Designer" scene (or when 0x0153ba00 is set) it is a
  copy of +0xc. It is selected when the primary is not live and either it is live or the local
  player's byte `+0xa41` is 1 (HIGH CONFIDENCE: the player's female flag)."

**Replace the `+0xac` / `+0xad` bullets with**:
"- `+0xac`: "has a lightset" byte, 1 when the scene file has a `SceneLightset` element.
- `+0xad`: the `SceneLightset` text (strncpy, 64 bytes); looked up by CRC in the lightset cache and
  removed from it on teardown."

**Replace the `+0xf4` bullet with**:
"- `+0xf0` / `+0xf4`: the two halves of the scene file's `Soundtrack` text, split at the first
  `:` (one character is dropped on each side of the colon, i.e. the form `left : right`), each
  passed through 0x00462960 (HIGH CONFIDENCE: name → audio id). Both 0 when absent. +0xf4 is the
  stream started at promotion (the soundtrack); +0xf0 is not read by anything dumped."

**Add bullets**:
"- `+0x20` / `+0x24` / `+0x28`: array pointer, capacity and count of the resources of a story
  cutscene's `Characters`/`Character` (its `Mesh` and `Killbane` names, or the three fixed names
  when `IsAngel` is set) and `Vehicles`/`Vehicle` (the `Variant` id); the cutscene loader starts
  all of them (0x00722f10). Empty for a zscene.
- Fields not listed here are written by the entry constructor 0x007233c0 or at run time
  (**OPEN**)."

**Lifecycle, replace step 3's sentence** "Its only caller is 0x007258a0 (not dumped; HIGH
CONFIDENCE: the cutscene machine's load step; OPEN: its body and callers)." **with**:
"Its only caller is **0x007258a0** (**CONFIRMED — disassembly**): the cutscene machine's second
per-frame step, called from the same two drivers as 0x0072d660 (0x00702a50 … 0x00703121 and
0x00bdbc54 … 0x00bdc0ef, HIGH CONFIDENCE once per frame, after 0x0072d660). In cutscene states 0
and 2 it calls the reset-check 0x00720320 and, when that returns true, promotes. 0x00720320 returns
true when there is no current entry (it then sets 0x0153b541 := 1), when the current entry's
selected handle has class 1 (it then resets state := 0, current := 0, re-queues the entry when
0x0153b542 is set), or when the handle is live and 0x0153b541 is set. So a bare Lua `zscene_prep`
(cutscene state 0) **is promoted by the engine on the next frame** — CONFIRMED when no scene was
current before, HIGH CONFIDENCE otherwise (it depends on the released handle classifying as class
1; OPEN: 0x00dafad0's effect on the flag word)."

**Replace step 5** **with**:
"5. The scene table is built by 0x0073bfb0 from **`cutscene.xtbl`** (inside `cutscene_tables.vpp`)
   and one **`<name>.cte_xtbl`** file per scene. **CONFIRMED — disassembly, full listing.**
   `cutscene.xtbl` contributes only the names: each `cutscene` element's `name` child; for the
   "main" container the names starting with "dlc" or "patch" are skipped, for a patch container
   only the names starting with the container's name are taken; at most 200. The table is
   allocated for the "main" call with capacity = accepted names + 12 (capped at 200); patch
   containers append. For each name the scene file `<name>.cte_xtbl` is opened (a missing file
   means no entry); its `Cutscene` element is parsed by 0x0073ada0 into the entry fields listed
   above (`CutsceneType` → +0x8, `SceneLightset` → +0xac/+0xad, `Soundtrack` → +0xf0/+0xf4,
   `Characters`/`Vehicles` → the +0x20 list) and into two streaming request groups whose handles
   become +0xc and +0x10. The `CameraScript` and per-shot character/vehicle animation requests are
   skipped when the shipping-mode byte 0x0149365c is set — always, in the retail executable.
   0x007231e0 is the table's destructor, called from 0x00707170 (not dumped)."

**Add step 6**:
"6. The cutscene machine proper. **CONFIRMED — disassembly** for the state table of 0x0072d660
   (jump table at 0x0072defc, 20 entries; the last four read by elimination, HIGH CONFIDENCE) and
   the five cases of 0x007258a0: 0 idle; 1 → 3 ("CS_STATE_FADING_OUT"); 2 waits for the zscene to
   load, then 0x00725df0 continues; 3 → 4 once the player-side checks pass; 4: 0x007258a0 mounts
   `preload_items.vpp` / `preload_effects.vpp` and sets 0x0153b543, then 0x0072d660 → 5; 5:
   0x007258a0 starts the resource loads (0x00722f10: the shared `npc_basehead` resource, the
   manager's handle, every +0x20 resource, the +0xf4 soundtrack; a 120 s stamp at 0x012f5d30) → 6,
   or for a loaded zscene → 7; 6, 7, 8: 0x0072c790 / 0x0072bff0 / 0x00720200 + 0x0072c260; 9:
   playback start 0x00729150; 10, 11, 12: playing (0x0072c980); 13: "CS_STATE_STOP", the manager's
   +0x35cc becomes the chain target 0x0153b52c; 14 → 15 when 0x00722e00; 15: 0x007258a0 either
   chains into 0x0153b52c (manager rebuilt by 0x00725670 → 5) or begins teardown → 0x10; 0x10:
   memory unlock stage (0x00a747a0) → 0x11; 0x11: "CS_STATE_STOPPED" → 0x12; 0x12:
   "CS_STATE_FINAL_STREAMING", world objects re-streamed → 0x13; 0x13: fade-in request, manager and
   `cutscene_virtual_pool` freed → 0. The state names are the strings the machine passes to the
   marker routine 0x00707410 (HYPOTHESIS: a profiling scope). OPEN: the writers of 1, 9, 12, 14 and
   the bodies of the loading/playback helpers."

**Host summary, replace the last bullet** ("A host that loads scenes itself may promote …") **with**:
"- A host runs, every frame: promotion of a pending entry (the 0x00720320 rule, cutscene state 0 or
  2), then completion (0x007285c0: handle resident and soundtrack ended or 5 s → state 2). The
  engine does exactly this from 0x007258a0 and 0x0072d660; nothing else promotes."

**OPEN — next dump block, replace** its bullet **with**:
"- `ptrs count:20 0x0072defc`; `func 0x00702a50 0x00bdbf30 0x007233c0 0x00dafad0 0x00dafb60
  0x00462960 0x007315a0`; `xref 0x0153ba00 0x0153ba01 0x0153ba02`; `xref xrefs:200 0x0153b520`;
  `func 0x0072c790 0x0072bff0 0x0072c260 0x0072c980 0x0072d030 0x00729150 0x00720040 0x00722e00`;
  `func 0x00707170`."

**Review status, replace** the whole paragraph **with**:
"**Review status (2026-10-01): re-derived from the executable (jobs `20261001T020200-team-a-nzxf`,
`20261001T114101-team-a-mnao`, `20261001T122853-team-a-nnlt`): CONFIRMED parts — prep gate, stub,
teardown, pending slot, entry offsets, `zscene_is_loaded` truth table, all three state codes,
promotion and completion bodies, the promoter 0x007258a0 and its rule, the `skip_all_cutscenes`
byte, the table's allocation/append/destroy, the two-file parse (`cutscene.xtbl` names,
`<name>.cte_xtbl` fields), the `CutsceneType` → kind mapping, the cutscene state table for states
0..0xf; HIGH CONFIDENCE — the labels of 0x0153b541/0x0153b542, the resident/failed handle
classes, the female variant (+0x10, `+0xa41`), +0xf4 as the soundtrack, the request-group label of
+0xc/+0x10, states 0x10..0x13 by elimination, promotion of a Lua-only prep after a previous scene;
HYPOTHESIS — cutscene guard, 0x0153b534's nature, the marker routine, the parser option bytes'
purpose; OPEN — the entry constructor and unlisted fields, the loading/playback helper bodies, the
released handle's class.**"

### 9.3 §26.26 UI resolution queries

**Source sentence, replace** "bridge jobs `20261001T020200-team-a-nzxf` and
`20261001T114101-team-a-mnao`." **with** "bridge jobs `20261001T020200-team-a-nzxf`,
`20261001T114101-team-a-mnao` and `20261001T122853-team-a-nnlt`."

**`vint_is_std_res` bullets, replace** "Its callers 0x00e23a20 and 0x00e23910 were not dumped."
**with**: "Its two callers are the UI subsystem init 0x00e23910 and the resolution-change path
0x00e23a20 (below)."

**Replace** "- 0x00e236f0 returns a per-thread 12-byte record (thread context +0x670, allocated on
first use from the pool at 0x02a5a090 and zeroed); the two integers are the record's second and
third dwords (**CONFIRMED — disassembly**). Their writers were not dumped." **with**:
"- 0x00e236f0 returns a per-thread 12-byte record (thread context +0x670, allocated on first use
  from the pool at 0x02a5a090 and zeroed); the two integers are the record's second and third
  dwords, copies of the global **width 0x02a5a180 and height 0x02a5a184** (**CONFIRMED —
  disassembly**). Writers: the UI subsystem init 0x00e23910 (called by 0x008489e0 during front-end
  bring-up; it takes the pair from its second argument and copies it into the calling thread's
  record with 0x00e237f0) and 0x00e23a20(width, height) (caller 0x005df6e0, not dumped;
  HYPOTHESIS: the display-mode/resize handler), which does nothing when the pair is unchanged and
  otherwise stores both globals and the record copy. Both then recompute the mode through
  0x00e23000 and the C-side standard-resolution test 0x00e2ad30 (the same rule as this function:
  width / height < 1.5 or mode 2), whose result becomes the layout index 0x0132c0ac (1 = standard,
  0 = wide; HIGH CONFIDENCE for the label). On a change 0x00e23a20 also calls the Lua global
  `vint_lib_init_constants()` if it is a function, and, when the standard/wide class flipped, calls
  `<document name>_reset()` for every loaded UI document that defines it (0x00e21670). The record is
  filled only on the thread that ran the writer (HIGH CONFIDENCE: the UI thread)."

**`vint_get_safe_frame` paragraph, replace** "**OPEN**: the two constants; both dumps print only
their low dwords (0x40000000 and 0xa0000000, the patterns of single-precision literals widened to
double — 0xa0000000 fits 0.1f, 0x40000000 fits 0.85f, 0.15f or 0.075f), and both are shared
literals used by unrelated routines, so a raw 8-byte read is needed. **HYPOTHESIS**: the four
values are the left/top and right/bottom safe-frame edges in pixels." **with**:
"The two constants (**CONFIRMED — disassembly**, read as two dwords each): 0x0115ba60 =
0x3FB3333340000000 = 0.07500000298023224 (the single 0.075f widened to double) and 0x0116dfc0 =
0x3FED9999A0000000 = 0.925000011920929 (0.925f widened). The function therefore returns
round(0.075 × a), round(0.075 × b), round(0.925 × a), round(0.925 × b) with the widened single
values — the left, top, right and bottom edges of the central 85 % of the screen (**HIGH
CONFIDENCE** for the edge reading; it rests on a = width, b = height). Host: multiply by 0.075f and
0.925f promoted to double (not by the decimals 0.075 / 0.925) and round to nearest; with the
widened constants no integer dimension below 2^23 produces a tie, whereas the decimals tie at
dimensions ≡ 20 (mod 40) and would differ under round-half-to-even (e.g. 1440 × 900: engine
(108, 68, 1332, 833)). Worked values: 1280 × 720 → (96, 54, 1184, 666); 1920 × 1080 → (144, 81,
1776, 999)."

**OPEN — next dump block, replace** its two bullets **with**:
"- `func 0x005df6e0` (what triggers 0x00e23a20); `func 0x00e230a0 0x00e2a980` (the mode reader and
  the layout-count writer)."

**Review status, replace** the whole paragraph **with**:
"**Review status (2026-10-01): re-derived from the executable (jobs `20261001T020200-team-a-nzxf`,
`20261001T114101-team-a-mnao`, `20261001T122853-team-a-nnlt`): CONFIRMED parts — both
argument/return shapes, the `vint_is_std_res` decision rule, the mode ladder of 0x00e23000 and its
two callers, the per-thread record of 0x00e236f0 and its writers, the global width/height, the
change-time Lua calls, the `vint_get_safe_frame` data path and both constants; HIGH CONFIDENCE —
width/height reading, output order, the four double thresholds, the safe-frame edge reading, the
layout index 0x0132c0ac; HYPOTHESIS — multi-monitor meaning of the mode, 0x005df6e0 as the resize
handler; OPEN — what triggers a resolution change.**"
