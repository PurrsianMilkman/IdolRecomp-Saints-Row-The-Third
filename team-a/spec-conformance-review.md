# Conformance review: Team B free-roam merge 8bdd080

Reviewed: TEAM B commit `8bdd080` (merge of `6ef519a` radio / `bf1a443`, and `7c03bcd` vehicle physics / `d01e56f`, `c4c2141`), review date 2026-10-14.
Specs: `spec-audio-runtime.md` §14.6, `spec-physics-runtime.md` §10.11. Read-only review; no Team B file touched. Paths are relative to TEAM B.
Severity key: WV = wrong value, MC = missing case, WO = wrong order, TV = test vector disagrees / not covered.
I did not build or run Team B tests. I could not run `radio_sim.py` for a queue comparison: no Python on this machine, and Team B has no queue vector test (see R-9).

## 1. Radio (`include/sr3radio/radio_sim.h`, `tests/synthetic_radio_sim_test.cpp`)

| # | Sev | Spec | Team B file:line | Code does | Spec says |
|---|---|---|---|---|---|
| R-1 | WO/MC | 14.6.2 queue builder, pattern quirk | radio_sim.h:172-175, 204-206 | Pattern restarts at entry 0 each rebuild, 512 steps counted from 0; `patternPos = steps % pc` is recorded but never read back. Flagged CHOSEN in a comment | Wrap/bound counted from saved position `+0x19E`; first pass after the first build is cut short; vector ends at position 2 after 946 draws. Clarification added |
| R-2 | MC | 14.6.2 intro/outro defect | radio_sim.h:157, 149-152 | `i == -2 && count == 3` returns content 0 (correct). Does not write 1 into the high byte of idx[2], so the later "index + 256" wrong-entry effect is missing (declared CHOSEN) | Permanent +256 on idx[2] after a ghost pick; later normal pick of slot 2 returns the wrong entry |
| R-3 | WV (minor) | 14.6.2 intro/outro defect | radio_sim.h:155 | Probe loop uses `i = (i+1) % count` after a negative `i`, correct for truncating remainder. Final fallback `max(0, i)` picks idx[0] when the last probe was negative | "Take the last index probed" (can be a ghost); equivalent except in all-negative-probe cases (not reachable for count <= 3) |
| R-4 | WO | 14.6.2 becoming audible step 5 | radio_sim.h:276-278 | If the entry is not longer than the remaining time, advances and re-arms to the NEW entry's `armDuration` | Re-arm to the old remaining r in both branches. Clarification added |
| R-5 | MC | 14.6.2 advance | radio_sim.h:210-216 | No `+0x68A` bit 4 "rebuild requested" path | Rebuild when the bit is set, even if the queue is not exhausted |
| R-6 | MC | 14.6.1 default station rule 3, 14.6.1 rule 4 | radio_sim.h:318-332 | Rule 3 (Police/FBI by `Special_Vehicle_Type`) omitted (retail gives 0xFF, so harmless). Rule 4 re-rolls when `k == count` | Engine reads one record past the array (can return Off or Mix Tape via the overrun). Re-roll is a deliberate, flagged deviation |
| R-7 | MC | 14.6.3 | radio_sim.h (absent) | No item-to-Wwise table (`Radio_Songs` 0x90D4215D ... `Radio_News_Breaks` 0xA73F0527), no OFF/enter-car bus events 0xC4F14EEA / 0x01DF15F0, no `Radio_Stations` switch 0x02C9B0FE, no news posting | Specified FINAL; audit marks MISSING |
| R-8 | MC | 14.6.1 table | radio_sim.h:353-378 | Roster has ids/flags/HUD images only; patterns, per-station counts and I%/C%/O% are not loaded, so every station's `pattern` is empty (Off semantics for all) | Station table lists patterns and intro/outro percentages; Team B must fill them from `misc_tables.vpp_pc` 636-647 |
| R-9 | TV | 14.6.4 | tests/synthetic_radio_sim_test.cpp:21-27 | Only ring[0..4] checked (matches `D091BB5C 22AE9EF6 E7E1FAEE D5C31F79 2082352C`); no 24-entry GENX queue vector, no 946-draw check | Queue vector and draw count are the acceptance test |
| R-10 | WV (low) | 14.6.2 commercial enabled test | radio_sim.h:85-92 | `initiallyDisabled` is keyed by enable slot and defaults all-false | Retail "initially disabled" source not specified in §14.6.2 (CHOSEN, noted in code) |

Correct per spec: MT19937 constants and tempering, 8192-value ring with cursor wrap (spec says it repeats every 8192) and `rand32` / `randInt` (inclusive, `%`) / `randFloat` (v * 2^-32) shapes; bag pick (no wrap-around, first unused, reset-all and redraw); commercial enabled test with slot > 30 reading 0 and slot 30 admitted; intro roll then outro roll only if no intro, DJ talk gated by queue < 511 and `+0x689` bit 4; draw order song / intro% / intro / outro% / outro; sweeper and commercial single bag draws; start index `(int16)(rand & 0xFFFF) % count` with truncating remainder; ghost entry 0 for i = -2 with count 3; roster numbering, Off as station 2 and selectable, Mix Tape only when non-empty, News and Credits rejected, cycle order Off, GenX ... The Mix then wrap; tune gate (bit 5, hidden, < 8 listeners); inaudible tick skipping outros with raw `Length`; overlap-adjusted arm (intro `Length - Delay`, entry before an outro `Length - Prestart`); join-mid-song seek `Length - r`; timer not reset when the last listener leaves; world start rebuilds all non-Mix queues; Mix Tape in-order loop; NPC acquire `rand_float > 0.25` rejects, blocked / global-block checks; default station rules 1 and 2 (KLASSIC force, `+0x1630`).

## 2. Vehicle physics (`include/sr3physics/vehicle_spec_data.h`, `src/physics_vehicle_spec_data.cpp`, `vehicle_driving_slice.h`, `tools/sr3_viewer.cpp` PlaytestVehicleRig)

| # | Sev | Spec | Team B file:line | Code does | Spec says |
|---|---|---|---|---|---|
| P-1 | MC | 10.11.6 lookup walk at `0x00c63930` | vehicle_driving_slice.h:595-640, src/physics_vehicle_driving_slice.cpp:339-351 | `FrictionSlipTableCurve` is a CHOSEN "boundary early-exit, linear remap of the overshoot fraction, times multiplier". Audit row 9 says the lookup rule is OPEN | Resolved: sequential first-exceed search u_i = A*start*q^i, v_i = B*table[i], S_i = u_i^2 + v_i^2 > 1, secant interpolation t = (1 - pr)/(r - pr) with denominator floor 1.19e-7, clamp u* to +-1.1. Four test vectors given for the genki front table |
| P-2 | WV | 10.11.6 field roles | vehicle_driving_slice.h:620-625 | Struct comments still call `boundary` `+0x48` and `entryMultiplier` `+0x4C`. `BuildFrictionSlipTableFromSpec` (spec_data.cpp:159-160) sets boundary = x/16 and multiplier = q, which is correct; only the labels and the stale curve contradict | `+0x48` = q, `+0x4C` = start (a1-relative); desc-relative `+0x40` = q, `+0x44` = start |
| P-3 | WV | 10.11.10 / 10.2 torque scale | vehicle_driving_slice.h:179, sr3_viewer.cpp (rig comment "torque scale is OPEN") | `torqueScale = 1.0` (CHOSEN) | `Vehicle_engine_torque_modifier` = 1.25 from `tweak_table.xtbl`; stale OPEN in the rig comment and audit row 1 |
| P-4 | MC | 10.11.10 Maximum_Speed clamp | spec_data.cpp:101-109, substeps.cpp (IntegrateChassis hunk) | Clamps planar (forward, right) speed to `Maximum_Speed` only. No `vehicle_max_speed` override (`+0x1d84`), no nitrous blend toward `Maximum_Speed_With_Nitrous` (decay 0.5/30 per call), no class 3/4/5 exemption, no bonus term, no skip for wrecked vehicles | 3-D speed rescale to cap + bonus after the blend. Clarification added |
| P-5 | MC | 10.11.8 extra gravity | audit row 21 (MISS) | Not implemented; audit says predicate OPEN | Predicate resolved: enabled for every class except Helicopter / Vtol, contact mask 0, `Extra_Gravity` != 0. Airborne = (0, -10*Mass, 0); all wheels in contact = Mass * Extra_Gravity * normal (zero if n.y < 0.5) plus the coasting damping term. Genki sag only matches the spec with g_eff = -19.8, which Team B already uses in its test |
| P-6 | MC | 10.11.5 suspension | vehicle_driving_slice.h:296-300 | Spring 30000 / damper 2500, 3500 / rest length 0.3, loadFactor 1.0 stay CHOSEN in the rig. `PerMassToPerWheel` exists and is tested, but nothing feeds the rig | genki spring 60000 F / 45000 R, damper 7500 both, rest length 0.37 / 0.35, `X` = invMass. Sim has no vertical state, so unwired by design |
| P-7 | MC | 10.11.0 slot map | vehicle_substeps.h:92-101 | Comments still carry the old wrong slot map (audit row 22) | Slots +0x20 input, +0x24 steering, +0x28 engine, +0x2c transmission, +0x30 brakes, +0x34 suspension, +0x38 aero, +0x3c wheel collide, +0x44 damper |
| P-8 | MC | 10.11.7 steering profile | audit row 6; spec_data.cpp:111-117 | Max rate set from data; `SteeringLimiterAccel` / `SteeringFullLockTimeSec` tested but not used by the limiter, so no acceleration / damp-angle profile | Min-time profile with A = V^2/(2*damp); genki full lock 77.5 ms |
| P-9 | WV (low) | 10.11.8 damper | spec_data.cpp:97 | Collision damping chosen only if `threshold > 0 && abs(omega) > threshold`; applied to yaw rate only (planar) | `|omega|^2 > threshold^2`; zero threshold would pick collision damping for any nonzero spin. Retail threshold is 4.0143 so no practical effect |
| P-10 | MC | 10.3 reverse rule | vehicle_substeps.h:214 | `reverseGearRatio` parsed, never engaged (CHOSEN 0, "trigger OPEN") | Reverse trigger and ratio source are in the §10.3 Team B note; stale OPEN in the code comment |
| P-11 | MC | 10.11.4 / 10.11.9 | audit row 25 | Rig hardcodes genki values; exotic and pickup vectors unused in sim | Vectors for exoticsports01 and pickup03 exist (pickup sag tested) |
| P-12 | TV | 10.11.9 | tests/synthetic_physics_vehicle_spec_data_test.cpp:20-26, 39-57 | Tests: genki and pickup sag (0.12672 / 0.16103 / 0.11965 / 0.13059), x = 0.056137, table entries 0, 1, 7, 10, 14, 15, q = 1.45767, start = 0.0035086, exotic R x / entries. Not checked: rows for exotic sag, full table arrays, lookup-walk vectors, nitrous blend | Complete rows in §10.11.9 |

Correct per spec: sag formulas and the `zF <= zC` fallback signal (tested to the spec numbers, using g_eff = -19.8); spring and damper per-mass x Mass; wheel radius `0.2048 * Ts[size]` (genki 0.2458); friction ratio x_k formula (invMass + cz^2 Yaw_TF/Yaw_UI/Mass + cy^2 Roll_TF/Roll_UI/Mass) and table `1 - (1-s_i)^(1/x)`, s_0 = x/16, s_{i+1} = s_i*q, q = (16/x)^(1/15), [15] = 1; steering accel and full-lock time formulas; yaw inertia = Mass * Yaw_Unit_Inertia (rig 1500 * 2.18906 = 3283.6); lift = 0.5*rho*Cl*A*v^2; drag from data (rig 0.7 / 1.0 / 1.3 gives the spec's -0.455 coefficient); velocity damper form `omega *= max(0, 1 - D*dt)`; rig genki values (wheelbase 3.3784, mean track, steering 35 deg, V 18.85, threshold 18.999, friction 1.25 / 2.5, braking 4000 / 3500, spin 2 / 6 / 4.0143, Maximum_Speed 37.998, cgHeight derived from COM y -0.3127 and wheel y -0.2185 less radius).

## Spec clarifications added

1. `spec-audio-runtime.md` §14.6.2 queue builder, "Pattern quirk" bullet: step counter and wrap measured from saved position `+0x19E` (explains R-1; Team B's restart-at-0 cannot match the 946-draw vector).
2. `spec-audio-runtime.md` §14.6.2 "Becoming audible" step 5: re-arm to the old remaining r in both branches, and the timer is never unarmed there (explains R-4).
3. `spec-physics-runtime.md` §10.11.10 Maximum_Speed clamp: compared speed is 3-D, all components rescaled; nitrous blend and class exemption apply regardless of the bonus inputs (explains P-4).

Not spec-ambiguous (Team B stale, not spec gaps): P-1, P-3, P-5, P-7, P-10 are all resolved in the spec and their notes are dated 2026-10-14; Team B's audit (`results/freeroam_audit_vehicle_physics.md`) was written before reading them.

# Conformance review: Team B older systems (Bink decoder, Lua host and engine callbacks)

Reviewed: TEAM B working tree as of 2026-10-14 (read-only; no build, no test run). Paths are relative to TEAM B.
Specs: `spec-bink-codec.md` 5.3ai-5.3al; `spec-lua-api-behaviour.md` "Engine callback dispatch" (both 2026-10-14 sections), 7.27, 7.28, plus a sample of other natives.
Severity key as above: WV = wrong value, MC = missing case, WO = wrong order, TV = test disagrees with spec.
I read the code and comments only. I could not run `synthetic_engine_callbacks_test.cpp` or the Bink real-data tests here.

## A. Bink decoder

| # | Sev | Spec | Team B file:line | Code does | Spec says |
|---|---|---|---|---|---|
| B-1 | MC | 5.3aj, 5.3ak, 5.3ai Q2 (colour cursor past-end fix-up) | include/sr3binkcodec/block_dispatch.h:986-989, 1006-1009, 1119-1122, 1163-1166, 1192-1195 | When the colour cursor has too few bytes left, every colour-cursor consumer (primary=8, primary=6, secondary=3 fill, secondary=5, 64-byte raw copy) returns `SourceExhausted` and the frame stops | If cursor >= end, cursor = base + 4 (checked once, before the first byte, for secondary=5 also before the 2nd literal), then read. The pattern cursor (`0x10019d10`, used by primary=8 masks and secondary=5 controls) has the same rule with its own base + 4 (block_dispatch.h:986, flat_fill_prestage_array.h `hasAtLeast`). This is the likely cause of Team B's own "frame 0 stops at Y row 3 col 6, primary=6 colour cursor exhausted" note (frame_decoder.h:363-366) |
| B-2 | MC / test gap | 5.3ai answer 3, 5.3al item 4 (DC cursor per stripe) | include/sr3binkcodec/frame_decoder.h:363-367, 531; block_dispatch.h:895 | Pre-stages (23 table builds, DC array, colour and pattern arrays) are built once per frame; for row > 0 `skipPrestage` keeps one `dcNext` and one colour cursor running across all stripes | The descriptor is refilled once per stripe by the first `0x1001a160` call; the "DC slot restarts at 0 each stripe" (5.3al item 4: Y stripe 1 starts at slot 0, DC 52, 5.3ai: class reset to 0 at each stripe). Team B's measurement (stripe 1 continues at DC word 75) can be the same stream if the per-stripe arrays are laid end to end, but then the arrays run out (B-1) where the DLL refills. The spec never says how large a per-stripe refill is. Clarification added to 5.3ai |
| B-3 | WV (minor, ambiguous) | 5.3ak step 1 | block_dispatch.h:1033-1037 | secondary=5 skip guard is "fewer than 2 block columns remain" | Spec text says "fewer than 16 rows remain in the plane" (same guard as secondary=3). Team B's columns test matches the primary=1 handling in 5.3af Q2, not the 5.3ak wording; one of the two is a typo. Not resolved here |
| B-4 | TV (info) | 5.3al item 3 | include/sr3binkcodec/idct_butterfly.h:117-128 | The DC-only fast path is chosen for the whole block (all of rows 1-7 zero); the generic path otherwise | Spec: per-column fast path, "same result as the general path"; whole-block is equivalent. No mismatch |
| B-5 | Gap | 5.3aj, 5.3ai | block_dispatch.h:1026 | Comment above the primary 2/3/4/7 fall-through still says primary 6 was "never mapped". Dead comment, behaviour is correct (SolidFill6 handled at :1002) | - |

Implemented correctly (checked against the spec text, not run):
- Dequant row pointer `nibble * 0x100` from `intra_quant` (`unpackQuantRow`, block_dispatch.h:597), indexed by natural position k = r*8 + c (idct_butterfly.h:130-132). Quant nibble read after the AC tree, DC from the shared cursor, int16 DC with no bias (block_dispatch.h:964-970).
- `deq = (coef * q) >> 11` in 64-bit arithmetic shift per term; butterfly network matches the 5.3al reference (idct_butterfly.h:72-101) including the 3784, -5352, 2217 and 2896 constants and output order.
- Row pass `(v + 0x7f) >> 8` stored with `static_cast<uint8_t>` (wraps, no clamp): idct_butterfly.h:152-153.
- Scan permutation `kCategoryToRasterK` (ac_tree_decoder.h:188-194) equals the 5.3al table for t = 1..63 and writes `coeffs[SCAN[t]] = L[t]`; DC is not touched by the tree.
- primary=6: one colour byte, 8x8 fill, no main-reader bits (block_dispatch.h:1002-1017). primary=8: c0, c1 from the colour cursor, 8 mask bytes from the pattern cursor, bit x LSB = leftmost picks c1 (981-1000). secondary=5: lit1, lit2 from the colour cursor with no bias, controls from the pattern cursor (1160-1176). primary=5 shares `dcNext` with secondary=2 in raster order (953, 1272).
- secondary=3 style fills on the BIKi grid path use the colour byte verbatim, no +128 (1143-1146).

## B. Lua host and engine callbacks

Files: include/sr3luahost/engine_callbacks.h, src/lua_engine_callbacks.cpp, src/lua_host.cpp:84-106, tests/synthetic_engine_callbacks_test.cpp, tools/lua_host_run.cpp.

| # | Sev | Spec | Team B file:line | Code does | Spec says |
|---|---|---|---|---|---|
| L-1 | WV + TV | Death section B "Firing for players", 2026-10-14 RESOLVED note | src/lua_engine_callbacks.cpp:106-137 (player branch), header engine_state.h:236; tests/synthetic_engine_callbacks_test.cpp:204-208 | Player `on_death` is queued on the first `tickCharacterDeaths` after death ("the frame after death") and the test asserts exactly that | Player `on_death` is queued by the gameplay frame update about 4000 ms (`respawn_delay_ms`) after the finalizer, once the timer armed by `0x00702810` expires; busted path also 4000 ms. Team B followed the older "queuing on the frame after death is a safe stand-in" wiring item 5, which the RESOLVED note supersedes. Clarification added to the wiring list |
| L-2 | MC | Section F `on_take_damage`, `on_damage`, `on_detection` | src/lua_engine_callbacks.cpp (no fire site); lua_engine_state.cpp:264, 326 (damage paths) | No `fireEngineCallback` call exists for `on_take_damage` (6 args, every non-0x200 hit incl. lethal, before the death chain), `on_damage` (no args, threshold crossing) or `on_detection`. The Lua registrations are stored but never fire | FINAL fire rules and argument lists; `character_damage` default `true` sets flag 0x200 and suppresses `on_take_damage`; `character_kill` fires it with attacker nil and hitpoints <= 0. m18 "sonic" depends on this |
| L-3 | MC | Section D sinking path, falling-through-world path (B "Extra path") | lua_engine_callbacks.cpp:15-17 | Not modelled (comment says so) | 10 s no-driver sinking fires `on_vehicle_destroyed`; a scripted NPC falling through the world fires `on_death` directly, ignoring the latch |
| L-4 | MC | 7.27 port rule | src/lua_spec_confirmed_stubs_b06_playerrt_sec6_7.cpp:285-315 | `character_is_ready` false for: unresolved, dead, pose-mode blocked, pending scripted-action ticket | Also false for hidden or unusable (+0x3b bit 0), +0x3a bit 0, not activated in the world (+0x33 bit 0x08 clear), NPC AI mode `Stunned` (0x22), or `Idle` (0) with sub-state `trans to stand` (0x26). Code comment :305-311 says the AI-mode tests "never fire" because the host does not track them; a hidden or never-activated NPC wrongly reads ready |
| L-5 | MC | 7.28 port rule | stubs_b06:990-1003, stubs_b11_batch4_flags_combat.cpp:238; character_state.h:487 | `scriptedActionLookupNonNull` is only read; no source file sets it (grep shows 0 writers). So `ai_scripted_action_is_complete` is always true and `character_is_ready` never blocks on a ticket | `ai_do_scripted_advance/fire/rush/take_cover`, `ai_suggest_action`, `character_take_human_shield_do` create a pending ticket; `ai_clear_scripted_action` and `force_ai_mode_check_done` clear it; a complete ticket is consumed on the first read. Result: is_complete false while pending |
| L-6 | WO (info) | V.3 "On a normal end cb2 can fire twice" | lua_engine_callbacks.cpp:294-300 | `endMissionCellphoneCall` fires cb2 once (`ended` guard) | Engine can fire it twice on a normal end. Harmless for shipped cleanup functions; not a mismatch in observable shipped behaviour |
| L-7 | WV (minor) | V.0 name arguments | lua_engine_callbacks.cpp:231, 257 | Vehicle name arg is always the name the script used; occupant arg is nil for an empty name | Vehicle name comes from the record name, else the first player whose current vehicle is it, else nil; a recordless NPC occupant is `"#FOLLOWER#"`, not nil. Only matters for recordless NPCs; the host has no such NPC path |
| L-8 | MC | V.4 (c) | lua_engine_callbacks.cpp:324-330 | Disable emits `on_trigger_exit` for the local player only | Every tracked human inside; fine for single-player scripts that only track the player |

Implemented correctly:
- Mechanism A/V.0: new script thread per event, never synchronous (lua_host.cpp:84-106 allocates via `BareThreadTable`, no `lua_resume`); missing or non-function global loses the event, full 256-entry table loses the event, args typed string/nil/number/boolean pushed before allocate; registrations persistent and never cleared on fire (header comment and test lines 137-149); "" clears; later registration replaces.
- Order within a death: `on_human_killed(team, killer, victim)` then `on_death(victim, killer)` then latch; NPC only for the global hook; creation (index) order across victims; latch cleared when the victim is no longer dead (lua_engine_callbacks.cpp:106-137, test :144).
- Killer rule: attacker of the latest application, nil with no attacker, frozen once dying (:100-104, :163); `character_kill` records nil (b30:74).
- `on_vehicle_destroyed(vehicle_name, nil)`: vehicle slot else player slot 0x12, thread created before occupants are killed, once per vehicle (:179-215, test :261-282).
- `on_mover_destroyed(mover, attacker|nil)` argument shape (:217-220); no producer is wired, noted in the code.
- `on_vehicle_enter` / `on_vehicle_exit`: three args `(occupant, vehicle, seat)`, vehicle hook first then occupant hook, seat 0 = driver, exit fires before the seat is released, exit player hook is the local player's (:222-277).
- Cellphone: cb1 on answer, cb2 on end with zero args, removal clears slots and never fires cb2 (:287-310).
- Trigger enable/disable transitions: enabling an already-enabled trigger does nothing; disable of a trigger with the player inside fires exit during the call; enabling a disabled one resets the overlap set so "already inside" enters on the next pass (:320-338). The "baselined" rule for an already-enabled trigger consumes the first pass, consistent with rule (a).
- `character_is_ready` early exits (dead, pose-mode) and `character_playing_combat_move` (`+0x1358 != 0xffff`); `ai_scripted_action_is_complete` true on unresolved; `ai_scripted_action_is_processing` gate polarity.
- `character_damage` argument shape: float damage without rounding, default `true` third arg to flag 0x200, flags 0x100 always set (stubs_b15:1657-1664); `character_kill` flag 0x100 from arg 2, arg 4 opt-in finalizer (b30:56-89).
- `mesh_mover_hide/show/reset/set_invulnerable` return values and the +0x82 bit 0x1 / 0x8 writes (stubs_b06:341-417).
Sample size: about 12 natives beyond the callback families. I did not walk the other FINAL-note natives.
## Review of 96f7d81 / 576952f (engine callbacks + 9 OPEN values), 2026-10-14

The engine-callback part (`src/lua_engine_callbacks.cpp`) is covered by the previous section (the older-systems review read it). Its findings still apply: player `on_death` must fire about 4000 ms after death, not on the next tick; `on_take_damage`, `on_damage` and `on_detection` are never fired. New in this merge, `src/lua_engine_open_values.cpp`:

| Item | Spec | Code | Verdict |
|---|---|---|---|
| Human-shield gate | §13.14 note (char1 state non-zero and not 3–6, char1 is the holder, partner alive, link mutual, partner == char2) | `characterHasSpecificHumanShield` | Matches. Minor: `state >= 3` also rejects states = 7, while the spec excludes only 3–6. No shipped path sets = 7 (HIGH), so this is harmless |
| Grab | §13.14 (`0x009acb20`: mutual links, hostage bit, both states = 2) | `beginHumanShield` | Matches |
| Synced action | §18.5 (fraction = 1 when remaining = 0, else elapsed/duration; finished if no instance or fraction = threshold) | `syncedActionFinishedFor` | Matches. The duration source is a port choice, as the spec allows |
| RC driver | §18.10 corrected (controller id pair, set only by RC start / cleared by stop) | `beginRcControl`, `endRcControl`, `playerIsRcDriver` | Matches, including the "already has an active vehicle" bail |
| HUD screen exit | §24.24 (callback args: screen name, player name) | `popHudScreen` | Matches. The firing site of the native is still OPEN in the spec, so a harness must pop the screen itself |

No new spec clarifications were needed for this merge.
## Review of e0195f9 (radio playback) and e58745a / cb6b54f (post-96f7d81 OPEN blockers), 2026-10-15

### A. Radio playback merge (`include/sr3radio/radio_sim.h`, `src/radio_content.cpp`, `radio_media.cpp`, `radio_player.cpp`, `tests/real_radio_content_test.cpp`)

Status of the earlier 8bdd080 radio findings:

| # | Status | Evidence |
|---|---|---|
| R-1 pattern counter | FIXED | radio_sim.h:180-186: step counter seeded from `patternPos`, slot type = pattern[slot mod count] with slot from 0; test real_radio_content_test.cpp:91-92 asserts 946 draws and position 2 |
| R-4 becoming-audible re-arm | FIXED | radio_sim.h:286: `s.timerMs = r` keeps the OLD remaining r in both branches (empty-queue fallback 1000 is a port choice) |
| R-8 patterns / percentages | FIXED | radio_content.cpp:82-142 loads Intro/Outro %, playlist (anything else = Song), lists; test :48-52 checks counts per station |
| R-9 queue vector | FIXED | real_radio_content_test.cpp:77-92 asserts the 24-entry GenX vector, 512 entries, 946 draws, position 2 (spec 14.6.6) |
| R-10 commercial initial state | FIXED/CHOSEN | radio_content.cpp:198 reads `commercials.xtbl` InitialState into `initiallyEnabled` (spec leaves the source open; reasonable) |
| R-2 +256 on idx[2] after ghost pick | STILL OPEN (WO) | radio_sim.h:150-153 comment still says not modelled; the 24-entry vector does not exercise it |
| R-3 | STILL OPEN (minor, unchanged) | probe fallback |
| R-5 `+0x68A` bit 4 rebuild-requested path | STILL OPEN (MC) | no writer or reader in radio_sim.h; only the `noDjTalk` bit at :106 |
| R-6 default station rules 3 and 4 | STILL OPEN (MC, harmless in retail) | not touched by the merge |
| R-7 item-to-Wwise table, bus events, switch, news posting | STILL OPEN, now documented | radio_player.h:8-10 declares flat 2D playback, no bus fades (ids 0xC4F14EEA / 0x01DF15F0 undecoded), no `Radio_Stations` switch. This is an audible-output substitute, not a logic bug; those ids are OPEN in spec 14.6.6 too |

New findings:

| # | Sev | Spec | Team B file:line | Code vs spec |
|---|---|---|---|---|
| R-11 | MC (low) | 14.6.6 file schema, song record | radio_content.cpp:120 | Song Intros/Outros keep only the first 3 names; an unknown name is dropped (warning), spec says an unknown name gives index -1 stored as `0xFFFF` (a real slot that the picker can probe). Retail data resolves every name, so no vector impact |
| R-12 | WV (low) | 14.6.6 / 14.6.2 content registration | radio_content.cpp:41-50 | Duplicate cue hash returns the existing index (matches 14.6.2 `0x0055C950`); a duplicate keeps the FIRST type and length. Matches spec; noted because `commercials.xtbl` Length patch (:197) touches only the first match, also as spec |
| R-13 | WV (low) | 14.6.2 posting | radio_sim.h:271, 288 | `posted` events are emitted for the audible station at expiry and on join with seek `Length - r` (matches 14.6.2 steps 4-5 and the audible-advance rule). Not an error; the empty-queue branch emits nothing |
| R-14 | WV (info) | 14.6.1 mix tape | radio_content.cpp:160 | Mix Tape row built in code (name "My Radio 85.5", image `ui_radio_mixtape`), flagged HIGH not FINAL in the code; spec 14.6.1 lists the same as code-built. Consistent |

Correct per spec (new in this merge): content table capacity 925 and retail total 915 (test :32); registration order sweepers, commercials, intros, call-ins, outros, sing-alongs, songs, giving GENX indices 0-1 / 2-82 / 83-136 / 137-172 / 173-190; Police/FBI song type 6 and News type 7; `Delay` for intros and `Prestart` for outros stored as `+0x0E`; Playlist_entry parsing; FNV-1 of lower-cased name.

### B. e58745a / cb6b54f (flee, seat-0, on_pickup, icon effect index)

| # | Sev | Spec | Team B file:line | Code vs spec |
|---|---|---|---|---|
| B-1 | MC | 45.1 `flee` Team B blocker 2026-10-15 | lua_spec_confirmed_stubs_b02_sec27_28.cpp:~746-759 | Zero return values and no throw: matches. But only the actor is resolved; the target (default `#PLAYER1#`, via the second resolver) is read and discarded. Spec: if EITHER fails, silently no order. A flee from a nonexistent target still bumps the counter. Also the `#PLAYER#` exclusion for the actor and the dead-actor skip are the port's own choices, not stated in the spec note (actor resolves like follower-aware names) |
| B-2 | WO (low) | 45.1 flag dword | same, comment only | Comment documents dword 2 / 3 / +0x10; the values are not stored (counter only). Spec's flag dword = 2, +1 if first flag, +0x10 if second; nothing reads it back in m07/m12, so harmless |
| B-3 | MC | seat-0 record `+0x1340` (claim `+0x00`, pending `+0x08`, seated `+0x18`) in `character_is_in_a_driver_seat` and the m06 note | lua_engine_callbacks.cpp:231-232, 277-278 | One boolean `seat0Occupied` set by `commitVehicleSeat` seat 0 and cleared by `commitVehicleExit`; the three handle fields (claim / pending / seated) are not modelled, so a character that is only entering or reserving seat 0 reads false (spec: true for any of the three). Not wired to `vehicle_evacuate`. |
| B-4 | MC | m06 note: seat-0 occupant at m06 start = false (FINAL) | stubs_b05:328, b14:531, b15:1469 | `seat0Occupied` is still "OPEN until set" and only ever set TRUE; with no seating it stays OPEN and the m06 `vehicle_pathfind_to` path still throws instead of reading false. Spec says default false |
| B-5 | OK | 20.12 on_pickup note | lua_spec_confirmed_stubs_b16_wave2_sec20_21.cpp:~330-335 | Now arg 1 = callback, arg 2 = item: matches the 2026-10-15 note. The comment's "flagged to Team A, spec lists item first" refers to the superseded Arguments line; clarification added at 20.12 |
| B-6 | OK | effect-name table (10.1; 'vfx_pc_icon' ordinal 564) | stubs_b02_sec27_28.cpp:~746-756, engine_state.h:2656-2661, tools/sr3_viewer.cpp:18607-18631 | Lower-folded Name set from effects.xtbl plus `*_effects.xtbl` (skybox / interface excluded), icon valid when present: spec-consistent for the validity test. The numeric slot (564) is not stored (set, not map), so a port that needs the index itself (not just != -1) would not have it; the spec allows port-own slots. Only gap: loaded in the viewer only (host without the installer stays OPEN) |

Spec clarification added: 20.12 Arguments line now notes the role swap is already corrected by the Team B blocker note (callback first, item second).

## Re-review of 84f35fa (fix/review-physics, 05a435e) and ea2136b (fix/review-radio, 63837d2), 2026-10-15

Read-only, code and comments only; no build, no test run. Paths relative to TEAM B. Status of the earlier 8bdd080 findings, then new mismatches.

### A. Physics (spec-physics-runtime.md 10.11 / 10.3)

| # | Status | Evidence |
|---|---|---|
| P-1 lookup walk 0x00c63930 | FIXED (lookup) / see N-1 | driving_slice.cpp `LookupFrictionSlipTable` (:386): circle test, u_i = A*start*q^i, v_i = B*table[i], first S > 1 (or i = 15), secant t with floor 1.19e-7, u* clamp +-1.1. Matches 10.11.6 |
| P-2 field labels | FIXED | vehicle_driving_slice.h:623-630 (q = desc +0x40 / a1+0x48, start = +0x44 / a1+0x4C); spec_data.cpp sets `start = x/16`, `q` |
| P-3 torque 1.25 | FIXED | vehicle_substeps.h:278 `engineTorqueModifier = 1.25`; passed as `finalScale` at substeps.cpp:859, applied last in driving_slice.cpp:~165 (after the 0.85 penalty), as the spec orders it |
| P-4 Maximum_Speed clamp | FIXED | spec_data.cpp:50 `ApplyMaximumSpeedLimit`: skip when wrecked, override cap when >= 0, classes 3/4/5 exempt from the nitrous blend (decay 0.5/30), limit = cap + bonus, 3-D rescale of all components. Called at substeps.cpp:~1227. Bonus input never fed (OPEN in spec too) |
| P-5 extra gravity | PARTIALLY FIXED | spec_data.cpp:69 `ComputeExtraGravityForce`: class 3/4 off, contactMask != 0 or Extra_Gravity 0 off, airborne (0,-10*Mass,0), all-contact Mass*EG*n (n.y >= 0.5), coasting term 0.89408 / 9.8. Substeps.cpp:~1186 wires only the coasting term, with all-wheels-contact hard-wired true and forward = body x; main term and airborne branch are unreachable in the planar sim (by design) |
| P-6 rig suspension | STILL OPEN | vehicle_driving_slice.h spring 30000 / damper 2500 / 3500 / rest 0.3 untouched; sr3_viewer.cpp rig not fed from `PerMassToPerWheel`. Unwired by design (no vertical state) |
| P-7 slot-map comments | FIXED | vehicle_substeps.h:92-98 now carries the FINAL map (+0x20 input ... +0x44 damper) |
| P-8 steering profile | PARTIALLY FIXED | driving_slice.cpp:65 `SteeringProfileLimiter::Update`: min-time profile, A = V^2/(2*damp), turn-in vs return pair, <= 8 segments, 1e-6 tolerance; substeps.cpp:~383 uses it only when both damp angles are data-sourced, else old limiter. Missing: the reversal seed V_ret^2/(2*damp_ret) and the `+0x164` bit 1 bypass (spec itself marks the seed PARTIAL) |
| P-9 damper threshold | FIXED | spec_data.cpp:~97 `omega*omega > threshold*threshold` (still yaw-rate only) |
| P-10 reverse | PARTIALLY FIXED | spec_data.cpp:98 `DecideReverse` matches cases (a)-(d), the class-1 rule and plain mode; `WheelTorqueRatio` (:130) = -(reverse x diff) when flag and gear index <= 0; shift machine suspended (substeps.cpp:~340). But `reverseFullLogic` defaults false (substeps.h:282) and `reverseGearRatio` defaults 0 (:206), so no shipped tuning engages reverse until a caller sets both; the spec's 3.38 is the most common value, xtbl-omitting files untraced |
| P-11 exotic / pickup vectors | STILL OPEN (rig) | viewer rig still genki-only; data path covers them |
| P-12 test gaps | PARTIALLY FIXED | tests/synthetic_physics_review_conformance_test.cpp added (388 lines; header says P-1..P-12). I did not enumerate its vectors; confirm it asserts the four spec lookup vectors, the reverse vectors (s = 0.5/1.2/-1.5, ratio -14.196) and the class 3/4 extra-gravity zero |

New mismatches introduced:
- N-1 (MC, regression): friction slip table is no longer applied. substeps.cpp:~1003 and ~1011 now set `axleCurve = nullptr` ("force application undecoded"), removing the 2026-10-08 wiring of `frictionSlipTable`. Previously a chosen (wrong) curve ran; now the circle-saturation correction does not run at all. The spec (10.11.6 step 6) only says the corrected tangential impulse is written into the force/torque accumulators; if (u*, v*) -> force is genuinely undecoded, this is correctly reported as OPEN, otherwise the lookup result is unused.
- N-2 (WV risk, verify): nitrous blend decays 0.5/30 per `ApplyMaximumSpeedLimit` call (spec: per `0x00a86620` call, once per vehicle per update). The call sits inside the micro-step integrator (substeps.cpp:~1227), so with N micro-steps per frame the blend reaches 0 in 2 s / N. Must be called once per frame or scaled by the micro-step count.
- N-3 (MC, minor): `maxSpeedOverrideMs` and the wrecked flag exist, but nothing in the rig sets `vehicle_max_speed` (+0x1d84); no regression, just untested path.
- N-4 (MC, minor): the reverse test vector `s = 0.5, x = -1 -> flag set, accel 1.0` is only reachable with `reverseFullLogic = true`; the rig comment still calls the trigger "source untraced", which is the byte at +0x14, correct per spec.

### B. Radio (spec-audio-runtime.md 14.6)

| # | Status | Evidence |
|---|---|---|
| R-2 +256 after ghost pick | FIXED | radio_sim.h:205-220: i = -2 with count 3 and high byte 0 sets it to 1 and returns content 0; later pick of slot 2 returns idx[2] + 256; overrun guarded at :281. Intro only (outros get no ghost byte; retail max 2 outros, harmless) |
| R-3 | STILL OPEN (minor, unchanged) | probe fallback unchanged |
| R-5 +0x68A bit 4 | FIXED (mechanism) | radio_sim.h:117, 231, 275; test synthetic_radio_sim_test.cpp:204-206. No producer sets the bit; the spec names no writer either |
| R-6 rule 3 | FIXED | radio_sim.h:~385-395: types 6/11 -> first Police station, 5/7 -> first FBI, else falls through. Rule 4 re-roll still a flagged deviation |
| R-7 Wwise table and events | PARTIALLY FIXED | radio_sim.h:~125-147 item table (ids and no-event flags match 14.6.3 incl. Callin/Police_FBI no event, SingAlong posts nothing), `Radio_Stations` set at each audible transition (:~290, spec says "once when a station becomes audible"), bus events latched in `playerBus` (radio_player.cpp:46, 57, 71), `postNewsBreak` (1000 ms delay). `postNewsBreak` has no caller in src/tools/include, so Lua `radio_newsbreak` never reaches it |
| R-4 proof test | FIXED | radio_sim.h:~286 keeps old r; test added in the same merge |

New mismatches:
- N-5 (WV, low): `stationSwitchSets.push_back` fires on every `becomeAudible`, including re-tunes to an already-audible station; spec says set once when a station becomes audible (the group is HIGH-confidence inert, so no audible effect).
- N-6 (MC, low): the intro ghost byte is a per-station-record field (`r.introGhostByte`), where the engine's high byte lives in the song's own record idx[2]; with several songs sharing a station the port tracks one byte per list (check whether `r` is per song). If per list, a second 3-intro song would never get its own ghost pick. Verify at radio_sim.h:258.

Summary: of 12 physics and 7 radio items, nine are FIXED, six PARTIALLY FIXED or open-by-design, and the remaining unfixed ones are P-6/P-11 (rig) and R-3. N-1 and N-2 are the two that can change driving behaviour.

## Re-review of eefbbd9 (fix/review-callbacks-479fedf, b2fa1c2), 2026-10-15

Read-only, code and comments only; no build, no test run (tests/synthetic_review_479fedf_test.cpp, 300 lines, not read). Paths relative to TEAM B. Status of the earlier L-1..L-5, B-1, B-3, B-4 findings and the 2026-10-15 Team B blocker notes.

| Item | Status | TEAM B evidence |
|---|---|---|
| L-1 player on_death 4000 ms | PARTIALLY FIXED | lua_engine_callbacks.cpp:125-143: `kPlayerRespawnDelaySeconds = 4.0`, timer armed at the first `tickCharacterDeaths` after death (not at the finalizer), queued when 4 s have passed on `syncedActionNowSeconds()`; revive clears it. Error is at most one tick. Busted path (same delay) and the NPC falling-through-world path (L-3) not modelled; the test's 4 s assertion is unread |
| L-2 on_take_damage | FIXED (characters) | `fireCharacterHitCallbacks` lua_engine_callbacks.cpp:162-186: six args (victim, attacker or nil, hp fraction after / max, flags 0x8, 0x1, 0x4000), skipped for flag 0x200, fired before the death chain, player slot vs scripted-NPC slot. Called from lua_engine_state.cpp:~300 (`applyCharacterDamage`), :~337-340 (`applyGuaranteedLethalHit`), lua_engine_callbacks.cpp:~220 (damage model), b30_tranchef_camera.cpp:~80 (`character_kill`: attacker nil, fraction 0, flag 0x100). `character_damage` forwards its flags (b15:1693), so default `true` (0x200) suppresses it. Vehicle-resolver branch not modelled |
| L-2 on_damage | FIXED (mechanism) | threshold stored by `on_damage` (b06:777-779, `onDamageThreshold`); fires with no args on old > T and new <= T in one application (lua_engine_callbacks.cpp:178-185). Flag 0x200 does not suppress it, matching the spec's OPEN note. Threshold default 0.0 when arg 3 omitted |
| L-2 on_detection | STILL OPEN | no fire site anywhere in src (grep for a fire call: 0); registration only |
| L-4 character_is_ready | PARTIALLY FIXED | b06:299-303 adds hidden (+0x3b bit 0) -> false. Still missing: +0x3a bit 0, "not activated in the world" (+0x33 bit 0x08), AI mode Stunned (0x22), Idle (0) with sub-state `trans to stand` (0x26). `character_hide` (b10:418) and `character_show` (b06:748) now write the bit |
| L-5 +0x4aa scripted ticket | STILL OPEN | `scriptedActionLookupNonNull` read at b06:317, b06:1005, b11:238; no writer in src. `ai_do_scripted_*`, `ai_suggest_action`, `character_take_human_shield_do` still create no ticket, `ai_clear_scripted_action` clears none, so is_complete stays true and is_ready never blocks on it |
| B-1 flee | FIXED (target) / PARTIALLY | b27:80-95: target via the second resolver (default `#PLAYER1#`), no order if either fails, dead-actor skip removed. Still: actor `#PLAYER#` literal excluded by name, a port choice not in the spec; order stays a counter only |
| B-3 seat-0 record | PARTIALLY FIXED | vehicle_state.h:272-290: `seat0Claim`, `seat0Pending`, seated = `seatOccupants[0]`; `seat0HoldsCharacter` (lua_engine_callbacks.cpp:94-100) tests all three case-insensitively; `character_is_in_a_driver_seat` (b16:1466-1476) requires a known cached vehicle that exists. But claim and pending have no writer outside tests: nothing writes the claim within 250 ms of an enter order, and `commitVehicleExit` (:331-337) just erases the seated entry instead of writing pending (+0x08) on EXIT. So an entering character reads false and a character that just exited reads false (spec: pending written on exit; see spec-ai-runtime 22 and the refinement in spec-lua-api-behaviour 21.26). `commitVehicleSeat` clearing claim and pending when the seated one is written is not stated in the spec |
| B-4 seat-0 at m06 start | FIXED | `seat0OccupantPresent()` (vehicle_state.h:282-286) defaults false; used at b05:328, b14:531, b15:1469. A test-set `seat0Occupied` still wins |
| take_cover gate | FIXED | b21:~933-945: 0x00540b90 modelled as `!coverStayDownActive` (false only inside a stay-down window, default no window), true -> action 8 dispatch count and return true, false -> return false, no action. `coverStayDownActive` is never set by the host (no cover system), by design |
| get_team table | PARTIALLY FIXED | `teamNameForId` engine_state.h:~8260 matches 0 Playas ... 7 Civilian, 9 and out-of-range "None" (verified against the list in the note). `get_team` (b16:597-612) returns "Playas" only for names starting `#PLAYER`; every other resolved object still throws OPEN (no per-character team id, so Saints homies are also not "Playas") |
| hidden bit | FIXED | `character_hidden` (b05:425-430): bit default 0 (visible) for a resolved object, `character_hide` / `character_show` write it, test-set value wins. Unresolved still true |
| navpoint effect names | FIXED (effect_play, effect_play_finisher) | b07:151-158 and b02:752-756: name resolves if found among loaded zone objects case-insensitively (`resolveZoneObject`), else the old lookup. `effect_play_finisher` success now returns a port-allocated handle (`nextVisualEffectHandle`) when no value is test-set; spec 28.9 packed identifier is port-own, acceptable |
| icon effect names | unchanged, OK | no change in this merge; B-6 stands (viewer-only load) |

New mismatches:
- N-7 (MC, low): player on_death timer starts at the first tick that sees the death, not at the finalizer; a world that ticks rarely (or only at mission update) adds that latency to the 4 s. Arm it in `finalizeLethalCharacter` instead.
- N-8 (MC): `fireCharacterHitCallbacks` is called from `applyDamageModelHit` and the lethal sinks, but the grab / melee / explosion callers do not pass 0x1 / 0x8 / 0x4000, so `from_melee`, `from_explosion`, `from_sonic` are always false except in tests; the default `flags = 0x300` for non-Lua callers also suppresses nothing wrongly (0x200 set means on_take_damage never fires for any internal damage-sink caller that omits flags). Check that engine-side callers pass a flags value without 0x200.
- N-9 (WV, minor): `fireCharacterHitCallbacks` is skipped when max hit points is 0 or unknown, so a character with unmodelled health gets no on_take_damage on `character_damage`, while the lethal sink in `applyGuaranteedLethalHit` fires with a fraction of 0 regardless. Spec fires for every non-0x200 hit.
- N-10 (info): `character_is_in_a_driver_seat` now also demands `hasVehicle(currentVehicleName)`; harmless, the spec reads the vehicle through the attached handle.

Summary: of the 13 items above, 6 FIXED (on_take_damage, on_damage, seat-0 default false, take_cover gate, hidden bit, navpoint / effect names), 5 PARTIALLY FIXED (player on_death, character_is_ready, flee, seat-0 record, get_team), 2 STILL OPEN (on_detection, +0x4aa ticket). The seat-0 claim and pending writers (enter order, exit) are the main remaining gap for m-series scripts that poll `character_is_in_a_driver_seat`.

## Re-review of b6afc89 (fix/review-radio-2, 853ceac), 2026-10-15

| Item | Status | Where |
|---|---|---|
| R-11 unknown intro/outro name | **FIXED**: stored as 0xFFFF (capped at 3 per record), with a warning, per §14.6.6 | `src/radio_content.cpp` (853ceac) |
| Vestigial `initiallyDisabled` | Removed, consistent with the §14.6.2 clarification | `include/sr3radio/radio_sim.h` |

No new mismatches. Still open from earlier: R-3; R-7 (`postNewsBreak` has no caller); N-5 (station switch set on every becomeAudible); N-6 (ghost byte possibly per list rather than per song record).

## Review of e11f57f (Merge feature/freeroam-population), 2026-10-15

Scope: `include/sr3airuntime/freeroam_population.h` (new), `tests/freeroam_population_test.cpp`, `tools/sr3_viewer.cpp` (runPlaytest wiring, ring candidates, ped gates), `near_field_ped_population.h` (near reject 90 -> 10). Read-only review; nothing was built or run. Specs: spec-ai-runtime.md §20.6.0-§20.6.2, §20.6.4, §20.6.6; spec-zone-data-format.md §13.2a.

**Implemented correctly**
- TOD index with midpoint boundaries (01:00/07:15/12:00/17:45, 00:59 -> 3) and the {night, avg, day, day} blend (§20.6.0a); `TodDensityIndexFromDayFraction` minute rounding is safe.
- Notoriety table 1/1/.75/.5/.25/.1 (§20.6.0c); vehicle target `max(1, trunc(12|18 x min(d x scale, capV)))` and ped target `min(40, max(1, trunc(40 x s)))` with weather PedDensity. All §20.6.4 vectors (6/9, 7/11, 9/14, Yearwood, Cutter 12/18/40, Rosen Oaks 3/5/12) are asserted in the test; I re-derived them in f32 and they agree.
- Moving cone: 2.2352 m/s threshold, minDot = 0.5 x min(v, 13.4112)/13.4112 (0.1864 at 5 m/s, 0.3728 at 10 m/s, 0.5 at >= 13.4112 m/s).
- 130 m chord/circle crossing and 65 m retry; one spawn per pass, gated by live < target.
- Vehicle despawn 140 / 65 (not visible, moving, behind) / 90 / 250, visible -> at least 140, +60 m driving with horizontal distance, 3-D when on foot, x scale.
- Ped rejects in spec order (far 90, near 10, visible < 70, crowd >= 7 within 20 m); the bulk path skips near and visible; despawn 90 m horizontal; lateral offset uniform +-width/2; bulk fill trunc(0.33 x min(target-live, headroom)).
- §20.6.6 rows `near_field_ped_population.h:186` (near reject 90 -> 10) and `:197` (far reject 90) are applied; the ped annulus is now 10-70 m (edges within 70 m).

**Mismatches (all in the merge's own code)**

| # | Severity | Spec | Team B | Code vs spec |
|---|---|---|---|---|
| F-1 | missing case | §20.6.1(b) step 2, §17.4c | `sr3_viewer.cpp` ~22905 (`RunNearFieldVehicleMaintenancePass(..., /*runDespawn=*/false, ...)`) | The `vehicle_despawn.xtbl` schedule (0x0090eaf0) is switched off; the merge replaced the old 100 m predicate with the distance rule but the schedule pass runs in addition to it, not instead of it. `nfRunDespawnSchedule` is now only a trigger. |
| F-2 | missing case | §20.6.1(c) "Inner ring" | `sr3_viewer.cpp` ~18230 (`tryRing`, `if (out.empty()) tryRing(65, true)`) | Spec: retry at R/2 only when no road crossing exists at R. Code retries when the cone filter empties the list (and the synthetic geometry always has a crossing). The hidden-only rule is `dot > 0.3` and applies only while moving, so a stationary camera accepts visible 65 m points. The spec wants a +-1.5 m box not visible (frustum + occlusion). |
| F-3 | missing case | §20.6.1(c), §20.6.6 row 176 | `sr3_viewer.cpp` ~18228 | Candidate loop stops at `kNearFieldSpawnCandidateSlots` (3); spec: up to 32 candidates, then up to 3 attempts. Not applied: the closing test, obstruction points (40 m), zone-loaded check, spawn-time 3/5 segment capacity, bit-4 highway rule, include/exclude masks. Chords are synthetic radials, not segments. |
| F-4 | wrong value | §20.6.2(c) "Point" | `sr3_viewer.cpp` ~23070 (`PedLateralOffsetFromRand(..., 3.0)`) | Width is fixed at 3 m, not the edge width (`+0x04`/256); one random value per pass instead of per candidate point. |
| F-5 | missing case | §20.6.2(c) 3 | `sr3_viewer.cpp` ~23080 (`isVisibleToCamera`: `Distance < 70 && ApproxVisibleToCamera`) | The visibility cone is a 50 deg stand-in (not frustum + occlusion), and it only suppresses points within 70 m, which matches the spec. The ped box is +-0.425 m; the stand-in tests the centre point. Local camera only (no co-op partner). |
| F-6 | missing case | §20.6.2(b) | not wired | Regular maintenance: visible-count <= (target+1)/2 outside mode 4, resource gate, and the 3-attempt length-weighted edge pick are absent. `PedBulkFillCount` has no caller. |
| F-7 | wrong value (stand-in) | §20.6.0(c),(d),(e) | `sr3_viewer.cpp` ~23077, ~22884 | Notoriety is hardcoded 0, weather 0.5 (Light Rain), category `Default Spawn`. Spec §20.6.8 (hood -> sp_cat via 33 Hood objects) is not used. Test vectors for other categories are unreachable at runtime. |
| F-8 | missing case | §20.6.1(d), §20.6.2(d) | `freeroam_population.h` ~92, ~114 | Fades (500 ms vehicles, 1000 ms peds) absent. Ped 50 m behind-player (mode 2), 8 m height rule and eligibility predicates absent. Vehicle `visible` is always false in the viewer. |
| F-9 | ambiguity, not a code bug | §20.6.1(d) "behind the camera (dot < 0)" | `sr3_viewer.cpp` ~22900 | Code takes dot(vehicle offset, camera velocity) < 0. Spec text does not say whether the reference direction is camera velocity or camera forward. See clarification below. |
| F-10 | wrong value | §20.6.1(d) | `sr3_viewer.cpp` ~22900 | Despawn distance uses the traffic reference position (player/camera stand-in), spec measures from the camera to every player. Same value in single player. |

**Not in this merge (no code exists; spec items still unimplemented in Team B)**
- §13.2a road and sidewalk graph: `sr3zone/traffic_graph.h` exists but the merge does not use it. The 0.0625 scale, 64-byte segments, 3x s16 shape points, 0xffff / <11455 / pooled links, 24-byte sidewalk nodes, 8-byte edges with width x 256, 36-byte crossings, the flag-bit table (3 and 8 never spawn, 4 highway, 16 offroad, 18 water, 25 no opposite lane) and tile 1215/1517 vectors are untested by this merge. The audit file (rows 11, 13, 20) states this honestly.
- Lane cruise 10 m/s class 0, follow law 0x00b51950 (5 m stop, ~11 m gap), ped walking speeds (§20.6.9), roadblocks at 135 m (§20.6.10), 32-candidate list, chase 4-6 s timer: untouched (`traffic_lane_follower.h`, `police_roadblock_spawn.h` unchanged).

**[2026-10-15 clarification]** added to §20.6.1(d) in spec-ai-runtime.md for F-9.

## Review of Team B bb92cfe (merge f8eb31e): the sweep OPENs from the a166141 spec sync (2026-10-15)

- m02 +0xccd spawn default 0, so character_is_jumping returns false: CORRECT (spec-lua-api §20.28).
- m06 vehicle_pathfind_navmesh_do returns 1.0 on the authority and 2.0 otherwise: CORRECT. Minor: the authority/non-authority split reuses forceFlagGatePasses as a proxy. Single-player is always the authority, so in a real run this should be 1.0 unconditionally. 0.0 (more than 25 points, or the target is unresolved) is not modelled; that is acceptable for now. Completion is only via check_done, as the spec says.
- m10/m16 checkpoint vehicle: CORRECT in effect. The spec reads the player's cached vehicle link (+0x2880) when the player is seated; Team B derives it from stateEnum==3 plus currentVehicleName resolving. Those are equivalent while the two fields stay in sync. The explicit test override still wins, which is fine for tests only.
- No regressions are visible; the w21 test was updated consistently.

## Note on Team B 51cf702 (merge 6799e45): combat AI audit, a document only (2026-10-15)

- The audit is consistent with spec-ai-runtime §21 and §22. The first-sight target writer is correctly left out, because the spec marks it OPEN pending live item 10.
- The audit points out a gap in the spec: §21 gives no figures for NPC weapon accuracy, fire rate or grenade use. Team A has recorded this as the next AI spec item.

## Review of Team B 84087da (merge cef1fc0): post-6799e45 OPEN stops (2026-10-16)

- Case-insensitive named-object map: CORRECT. The engine's by-name lookup lower-cases the name before hashing and compares case-insensitively.
- get_team for a zone script_npc with an authored team: CORRECT per spec-lua-api §20.23. An unknown name gives id 9, which reads "None".
- move_to_do with arg5 false reuses character_is_ready's predicate: CORRECT (it is the same predicate called with false). With arg5 true it stays OPEN, which is acceptable.
- vehicle_is_tank tests vehicle class 14 (Special_Vehicle_Type = Tank): CORRECT.
- No issues found.

## Review of Team B bd3c86a (road graph) + e19990e (NPC AI loop) + 0a6640b (2026-10-16)

Scope: `git diff bd3c86a^1 bd3c86a` (road_graph.h/.cpp, road_graph_placement.h, hood_volumes.h/.cpp, traffic_lane_follower.h real-graph constructor, sr3_viewer.cpp wiring, the three new tests) and `git diff e19990e^1 e19990e -- src include tests tools` (npc_ai_runtime.h, ai_npc_runtime.cpp, lua_engine_npc_ai.cpp, engine_state.h, tests/npc_ai_*.cpp, sr3_viewer.cpp), plus the `results/verify_npc_ai/*.summary.txt` and `red_*_tests.txt` files. Read-only: nothing was built, run or edited in Team B. Specs: spec-zone-data-format.md §13.2a; spec-ai-runtime.md §20.6.0-§20.6.8, §21.1-§21.4, §21.9, §21.10.1, §22.11, §22.12. Team B paths are relative to the Team B repo; line numbers are at the merge commit named.

**0a6640b:** build flag only (`/bigobj` on `sr3_viewer` under MSVC). No behaviour change; nothing to review.

**Results files:** every summary shows peak private memory 412 MB (free roam) to 2.73 GB (m02/m06/m21), all under the 6 GB limit, with an empty `exit=` field. `red_host_tests.txt` (13 failures) and `red_library_tests.txt` (18 failures) are the expected red-first logs written before the implementation existed.

### A. Road graph, placement, hoods, traffic (bd3c86a)

| # | Verdict | Spec | Team B | Finding | Fix |
|---|---|---|---|---|---|
| RG-1 | CORRECT | §13.2a(a)-(h), (l), (n) | `src/road_graph.cpp`, `tests/real_road_graph_test.cpp` | Tags `0x80002236`/`0x80002238` with the third header dword 0; 14 count/offset pairs, strides 64/12/2 and 24/2/8; the link encoding (`0xffff`, below N = single, otherwise a pool index `{n, n x idx}`); shape points at node A + 0.0625 x s16 (n = next.`+0x3e` - this.`+0x3e` interior); the AABB codec; the §10.6(g) tile lattice. The test asserts the city totals (11,455 / 10,141 / 6,216 / 7,659 / 8,703), the tile 1215/1517 counts, segments 33 and 6900/6902, the fork at node 6289, edge 260 with crossing 10, and 14,325 symmetric links. | none |
| RG-2 | CORRECT | §13.2a(d), §20.6.1(c) | `include/sr3zone/road_graph.h` flag constants | Exclude mask `0x0080010C`; bits 0, 3, 4, 8, 10, 16 and 18 named as in the spec. | none |
| RG-3 | DEVIATION (minor) | §20.6.1(c) include mask | `road_graph_placement.h:34` | The include mask is fixed at `0x400 \| 0x10000`. The spec ORs in the `Spline_Type` bit of every streamed vehicle slot (table {0x10, 0x40000, 0x10000, 0x400000, 0x20000, 0x200000}). The code comment cites the single `distant_vehicle_traffic_types` row, but that is the distant tier, not the near-field streamed groups. A streamed `Highway Only` group adds 0x10. | Build the mask from the streamed groups' `Spline_Type` and fall back to 0x10400 only when nothing is streamed. |
| RG-4 | DEVIATION | §20.6.1(c) "Segment full" | `sr3_viewer.cpp:18241-18255` (query setup), `road_graph_placement.h:117` | `q.occupancy` is never set, so the default of 0 applies and the 3-vehicle (5 on bit-4 segments) capacity never rejects a segment. The spec calls this the only car-to-car spacing rule at spawn time. | Count the live near-field vehicles per segment (from `nearFieldVehicleLaneState`) and pass that count as `occupancy`. |
| RG-5 | DEVIATION (F-2 carried over) | §20.6.1(c) "Inner ring" | `road_graph_placement.h:154`; `sr3_viewer.cpp:18249` | The 65 m pass runs only when the whole candidate list is empty after the cone and closing filters. The spec's trigger is the absence of a crossing at R. The "hidden" test is the stand-in "ahead of a moving camera, dot > 0.3", so a still camera accepts visible 65 m points. | Retry at R/2 only for the no-crossing case, and reject 65 m points whose +-1.5 m box is inside the camera frustum. |
| RG-6 | DEVIATION | §20.6.1(b) speed, §20.6.1(d) 2026-10-15 FINAL note | `sr3_viewer.cpp:23118-23121`, `:23144`, `:23149` | Camera speed is the horizontal displacement divided by the real frame dt, and "moving" uses its magnitude. The spec uses the displacement times 30, a fixed factor rather than dt. For the despawn gate, moving = dot(camFwd, camPos - prevCamPos) x 30 > 2.2352. A camera that strafes or reverses therefore triggers the 65 m behind rule here, but the engine treats it as still (140 m). At 60 fps the engine gate needs about twice the true speed. The behind test itself (dot(offset, delta) < 0) is correct. The centre is the player focus, not the camera (F-10). | Keep prevCamPos per frame. Use \|delta\| x 30 for the spawn cone and dot(camFwd, delta) x 30 for the despawn gate. Measure from the camera position. |
| RG-7 | NOT-YET-IMPLEMENTED | §20.6.1(c) | `road_graph_placement.h` header (labelled), `freeroam_population.h:82` | Not wired: the zone-loaded test, obstruction points within 40 m, the bit-4 highway rule (`highwayOnly` is never set), and the co-op partner test. These are labelled in the header. Not labelled: only the first chord/circle crossing per segment is kept, so a segment that crosses the ring twice yields one point. The 3-attempt loop is moot because the pick cannot fail. 32 candidates and the 128-segment query are correct. | Wire the highway rule (player's current segment plus the 0.75 x target count of vehicles on bit-4 segments) first; keep both crossings. |
| RG-8 | CORRECT (F-4 fixed) | §20.6.2(c) | `road_graph_placement.h:161-190`, `near_field_ped_population.h` width path, `sr3_viewer.cpp:18332` | Candidates are real sidewalk edges within 70 m, at most 100. The lateral offset is edge width x rand[-0.5, 0.5], drawn per candidate point. Nearest-first ordering and the per-tick rebuild (instead of 500 ms / 30 m) are labelled and harmless. | none |
| RG-9 | CORRECT | §20.6.8 | `src/hood_volumes.cpp`, `tests/real_hood_spawn_category_test.cpp`, `sr3_viewer.cpp:20247` | The BSP blob layout, s < -0.0001 goes behind, child 0 means outside (behind) or inside (front), the first hood in list order wins, and no hood falls back to `Default Spawn`. The test asserts 33 objects (14 Water, 18 HOOD) and all four §20.6.8 vectors, including `Water_03`, plus the HenryMills/Ashwood densities and targets (8/12/20, 12/18/40, 9/14/32). Minor gaps: the cached last hood is not tested first, which only matters for overlapping volumes. The global, per-hood and alternate overrides are not wired; this is labelled, and the alternate category is parsed. | Add the override chain when the `spawn_override_*` natives are hosted. |
| RG-10 | DEVIATION (F-7 remainder) | §20.6.0(d) | `sr3_viewer.cpp:23370` | Ped density still uses a hardcoded weather PedDensity of 0.5 (Light Rain). `Default Spawn` at 14:00 gives 16 peds instead of 32. | Use the current weather stage's `weather.xtbl` PedDensity (Clear Skies 1.0). |
| RG-11 | CORRECT | §20.6.6 (on-disk links), §13.2a(e)/(f) | `traffic_lane_follower.h` real-graph constructor | Each lane's polyline is the decoded point list. Adjacency is the on-disk successor list, replacing geometric coincidence. | none |
| RG-12 | DEVIATION | §20.6.3(a)/(b), §20.6.6 rows `:743-:756` | `sr3_viewer.cpp:22423`, `:23309`; `traffic_lane_follower.h:766-779` | On the real graph, ambient cars move at 6.0 m/s x `drivingThrottle` 0.5 = **3.0 m/s** when aligned, and 0 otherwise. The spec: every shipped lane's cruise limit is 10 m/s, under the throttle law (0.70 from rest, cap 0.7-1.0). Still CHOSEN: arrival radius 1.5, lookahead 15, re-query 3.0. The comment calls the re-query constant off-limits, but §20.6.3(d) gives 15.0 m linear. | Drive toward a 10 m/s target with the §20.6.3(b) law (or a kinematic 10 m/s at minimum); set re-query 15 m and a 5000 m walk lookahead. |
| RG-13 | NOT-YET-IMPLEMENTED | §20.6.7(b)/(c), §20.6.3(c), §13.2a(i) | `road_graph.cpp` (signal arrays `+0x48..+0x68` and crossings not parsed) | Missing: the car-following law (5 m stand-off, about 11 m steady gap), the junction yield, and the signal controller. Segment `+0x30` keeps its disk value (3 on every phase-listed lane). On the real graph, ambient cars therefore pass through each other and through junctions. | Parse lights/junctions/phases, run the §13.2a(i) controller, and add the §20.6.7(b) law to the lane follower. |

**Earlier findings (review of e11f57f) after these merges:** F-1 not fixed (`sr3_viewer.cpp:23159` still passes `runDespawn=false`). F-2 not fixed (RG-5). F-3 partly fixed: real segments, 128/32 caps, masks and the closing test are done; capacity (RG-4) and zone/obstruction/highway (RG-7) are open. F-4 fixed (RG-8). F-5 not fixed (50-degree cone stand-in). F-6 not fixed. F-7 partly fixed: the category through the hood lookup in bd3c86a, live notoriety in e19990e (`liveNotorietyLevel`, max over the 4 tracks); the weather is still open (RG-10). F-8 not fixed. F-9 is superseded by the 2026-10-15 FINAL note; the remaining gap is RG-6. F-10 not fixed. The e11f57f "not in this merge" item for the §13.2a reader is fixed (RG-1).

### B. Per-NPC AI loop (e19990e)

| # | Verdict | Spec | Team B | Finding | Fix |
|---|---|---|---|---|---|
| AI-1 | CORRECT (CHOSEN, labelled) | §21.2, §21.9 | `npc_ai_runtime.h:25-27`, `ai_npc_runtime.cpp:377-391` | The first-sight target writer is OPEN in the spec. Team B uses the §21.9 headless recipe and labels it CHOSEN: a hostile NPC whose list 0 holds the player gets that target, refreshed every 2000 ms. A newly added NPC starts with no target (`targetSet` false) and gets one only through perception or damage. This matches the FINAL rule that mission enemies start target-less. | none |
| AI-2 | CORRECT | §21.1, §19.4 | `ai_npc_runtime.cpp:31-152`, `lua_engine_npc_ai.cpp:52-61` | The loader matches the spec: monotone seen_in_last, `Gang Average` 4/5/10/20 s, cover 3/5/15/20/4 s, defaults for `Civilian Unarmed`, Repeat_min raised to Min_repeat_time, action 14 always enabled, at most 24 goal actions. The gate and chooser come from decision.h. Notoriety is not an AI input; its only effect is the team-matrix flip (level >= 1 gives relation 0, level 0 gives 1). The kill-near-player goal is disabled because its radius is OPEN (labelled). The forced 1000 ms re-evaluate cadence is labelled CHOSEN. | none |
| AI-3 | DEVIATION | §21.1 perception thresholds, §10 | `ai_npc_runtime.cpp:281`, `:304`; `lua_engine_npc_ai.cpp` and `sr3_viewer.cpp` never set `lineOfSight` | Neither production host sets the line-of-sight seam, so it defaults to "clear". The sense test is omnidirectional; the cone is labelled, the LOS default is not. A hostile mission NPC within 45 m notices the player through walls and, within 30 m, shoots through them. The spec uses a 45 m x 150 x 80 degree view cone plus an asynchronous LOS answer (700-4000 ms re-check). | Wire a collision-ray LOS in both hosts, or at minimum gate perception and fire on it; add the facing cone once NPC facing exists. |
| AI-4 | DEVIATION (minor) | §21.1, §10 sense pass step 7 | `npc_ai_runtime.h:108`, `ai_npc_runtime.cpp:301-303` | The sense pass runs every 650-800 ms for every NPC. The spec uses 350-450 ms for a non-civilian below activity level 6, which is the TV1 case of a gang NPC near the player. | Use 350-450 ms for combatants within the near test. |
| AI-5 | DEVIATION | §10 list 16 / list 8, notice routine; §21.2 | `ai_npc_runtime.cpp:278-279`, `:295`; `lua_engine_npc_ai.cpp:137-139` | Any player damage sets `provoked`, and a provoked combatant is hostile for good. `provocationAtThreshold` is forced true. In the spec, an attacker at attitude 4 or more (allies, homies) produces only a list-8 friendly-fire record. The player enters list 0 only while the provocation meter is at its threshold, and never with `never_turn_on_player`. Here, one stray hit on a homie turns him on the player for the rest of the mission. | Branch on attitude: below 3, notice the attacker; 4 or more, friendly fire only, gated on the meter and `never_turn_on_player`. |
| AI-6 | DEVIATION (minor) | §21.3 timers, TV2 | `ai_npc_runtime.cpp:550-551` | Stay-down is always `stay_down_time` x U(0.8, 1.25). The 800 ms first entry (undamaged, `+0x315` bit 7 clear) is missing. The x U(0.8, 1.25) timers, the 6-8 s back-off after a failed search and abandon at `abandon_cover_time` are correct. | Use 800 ms on the first cover entry when recent damage is below 20%. |
| AI-7 | NOT-YET-IMPLEMENTED | §21.3 search and evaluator | `findCover` never set in the hosts | There is no `cover_node` source. Every cover attempt in a mission fails, backs off 6-8 s, and the NPC fires from the open. The FINAL evaluator and threat term (wA/wB 5/10 or 8/7) are not implemented. | Feed the zone `cover_node` objects into a §21.3 search (14 m box, claim by compare-and-set, evaluator). |
| AI-8 | CORRECT | §21.4 chooser, personality consumer, flee stop | `ai_npc_runtime.cpp:329-374`, `:569-581` | The flee-rather-than-fight order is followed, including the percent-against-fraction compare. The chooser rolls 0.8, or 0.5 while cowering. `cant_flee`, `cant_cower` and `always_cower` are honoured. Flee ends beyond 30 m (900 squared). Labelled CHOSEN: 8 s cower release, 30 m gunfire threat range. | none |
| AI-9 | DEVIATION (minor) | §21.4 flee tick, §20.6.9(a) | `ai_npc_runtime.cpp:352`, `:570`; `npc_ai_runtime.h:112` | Flee-from is a snapshot of the player's position at the moment of the threat. The engine measures to the resolved threat handle, i.e. the live position (a fixed point only when `+0x8d0` bit 5 is set), so a chased civilian stops fleeing early here. Flee speed is 5.0 (CHOSEN), but measured run speeds exist: 6.373 m/s (M) and 4.433 m/s (F). Beyond 20 m the engine answers gunfire with the delayed event 67 (250-750 ms); here the reaction is immediate out to 30 m. | Measure the 30 m stop to the live player; use the measured run speeds; add the 250-750 ms delay beyond 20 m. |
| AI-10 | DEVIATION | §21.10.1 (FINAL, supersedes the 21.4 third pass) | `include/sr3airuntime/combat_ai.h:173-185`, `tests/combat_ai_test.cpp:207-213` (pre-existing, unchanged by this merge) | `DownedStandsUp` / `DownedReviveHeal` encode the superseded rule that type-0 expiry stands the NPC up healed (0.25 / 1.0), and the test asserts it. FINAL: type-0 expiry is a bleed-out **death** (HP 0, stance 5). The team-percent heal belongs only to the revive and get-up paths (player hold of 2.5 s, `npc_revive` / `player_revive`, engine get-ups) and to the type-1 expiry. There is no runtime caller yet, so behaviour is not affected today. | Replace with an "expiry kills" helper for type 0, move the heal to the get-up path, and update the test to the §21.10.1 vectors 1-3. |
| AI-11 | NOT-YET-IMPLEMENTED | §21.10.1 | lethal hits never set `lifeState` 6 (no writer in src) | No downed state exists. Gang brutes (type 1: 7 s down, overkill buffer, +50 ms per damage point up to 7000, stand at 25% HP unless finished) and followers (type 0: 30 s x difficulty, player revive) die at once. This matters for brute missions (m19) and homie missions. | Add the selector (types 0 and 1 first) in the lethal sink, before the death finalizer. |
| AI-12 | DEVIATION (labelled stand-in) | §22.12 CHOSEN model, §22.12.1 | `npc_ai_runtime.h:99-104`, `ai_npc_runtime.cpp:475-496` | The form matches the CHOSEN model: linear close-to-far accuracy, clamped, read as a hit probability. The numbers are invented and the data is never read: burst 3 for every weapon, 900 ms delay, 120 ms shot gap, 8 m / 0.7, 40 m / 0.15, 4 HP per hit. The spec says to use Close_range/Close_accuracy/Far_range/Far_accuracy from the weapon's accuracy record, `Burst_Delay_ms` and `NPC_Desired_Burst_Size` (default 3 for one class, 1 otherwise), and the weapon's damage. | Resolve the NPC's weapons.xtbl row and accuracy record, and drive the model from them. |
| AI-13 | CORRECT | §22.11 arrival test A | `src/lua_engine_callbacks.cpp:167` (pre-existing); `lua_engine_npc_ai.cpp:78` | Squared 3-D distance to a point destination below 2.0 (1.414 m). The AI loop leaves an NPC alone while its move order is pending, so the order owns the motion. | none |
| AI-14 | DEVIATION (minor, pre-existing) | §22.11 port rule | `src/lua_engine_callbacks.cpp:160` | A destination that does not resolve completes the ticket. The spec: a destination that fails the gate or is dead skips test A and ends **without** completing, so `ai_scripted_action_is_complete` stays false until a new order replaces it. | Do not finish the ticket when the destination fails to resolve. Keep completion only for the case the host cannot measure, and label it there. |
| AI-15 | NOT-YET-IMPLEMENTED (L-5 still partial) | §21.5 staged orders | only `ai_do_scripted_move` stages a ticket (`lua_spec_confirmed_stubs_b08_batch1_core.cpp:379`) | `ai_do_scripted_take_cover/advance/rush/fire`, `ai_suggest_action` and `character_take_human_shield_do` stage no ticket. The new loop has no ordered-bit override, so the autonomous goals overrule these script orders. | Stage the order into the runtime's ModeState (ordered bit 3, reason `0x12`) and complete the ticket in its finish. |
| AI-16 | NOT-YET-IMPLEMENTED (L-4, L-2 follow-up) | spec-lua-api 7.27; Section F `on_detection` | `NpcAiRuntime::modeOf` exists; `character_is_ready` and `on_detection` do not use the loop | The runtime now knows each NPC's mode and when it notices the player, but `character_is_ready` still ignores `Stunned` (34) and `Idle` with sub-state `trans to stand`. `on_detection` still has no fire site. | Query `npcAi()->modeOf` in `character_is_ready`; fire `on_detection` from the list-0 notice. |
| AI-17 | DEVIATION (unlabelled inventions) | §10, §21.1 | `ai_npc_runtime.cpp:295` (`provocationAtThreshold = true`), `:419` (`flag985cd0 = true`), `:476` (fire allowed out to weapon range b + 5 m); hosts' LOS default (AI-3) | These values fill OPEN or unmodelled inputs without a CHOSEN label, unlike the rest of the file. | Label each one CHOSEN next to its value, or remove it. |

**Tally:** RG: 5 CORRECT, 6 DEVIATION, 2 NOT-YET-IMPLEMENTED. AI: 4 CORRECT, 9 DEVIATION, 4 NOT-YET-IMPLEMENTED. Priorities: AI-3 (LOS) and AI-5 (friendly fire) change mission outcomes. RG-12, RG-4 and RG-6 shape the look of free roam. AI-10 must be fixed before any downed state is wired.

## Note on Team B b99630f (merge e2eb5b8): --auto-combat harness (2026-10-16)

- This is test-harness player input, not engine behaviour, and it is correctly labelled CHOSEN throughout. It is consistent with spec-ai-runtime §21.8: most K1 kill/destroy stalls need harness player input.
- Hits go through the real hitscan and the damage model. A target whose HP is still OPEN refuses the hit rather than having damage invented: CORRECT.
- Caveats:
  - The fallback character_kill after 60 s hides AI gaps. Count fallback kills separately in the verify logs; the summary already counts them.
  - Until AI-3 (line-of-sight default) is fixed, NPCs in combat-range runs engage through walls, so the engagement statistics from this harness overstate how often NPCs see the player.
  - A downed type-0 homie must die when its timer expires (§21.10.1); the harness should not count that death as a player kill.

## Re-check of Team B ff8ec85/8615cf3 (road graph fixes) + 383d8d1/ade8004 (NPC AI fixes) (2026-10-17)

Scope: `git show ff8ec85` (freeroam_population.h, road_graph_placement.h, new traffic_following.h, sr3_viewer.cpp, road_graph_placement_test.cpp) and `git show 383d8d1` (combat_ai.h, npc_ai_runtime.h, ai_npc_runtime.cpp, lua_engine_npc_ai.cpp, lua_engine_state.cpp, lua_engine_callbacks.cpp, engine_state.h, character_state.h, tests). Read-only: nothing was built, run or edited in Team B. Specs: spec-ai-runtime.md §20.6.1, §20.6.3, §20.6.7, §20.6.13 (new, FINAL), §21.10.1 (new, FINAL), §20.6.9(a), §22.11. Line numbers are at ade8004.

**Tally:** 30 findings re-checked. FIXED 9, PARTIAL 8, NOT ADDRESSED 4, still CORRECT and untouched 9, REGRESSED 0. New deviations: RF-1..RF-6.

### A. Road graph (ff8ec85)

- RG-1: CORRECT, untouched.
- RG-2: CORRECT, untouched.
- RG-3: PARTIAL. `road_graph_placement.h` now has `SplineTypeFlagBit` (table Highway Only 0x10, Boat 0x40000, Offroad 0x10000, Indoor 0x400000, Baggage 0x20000, Taxi 0x200000) and `BuildRoadSpawnIncludeMask`, matching §20.6.1(c). Nothing calls it from `sr3_viewer.cpp`; the viewer streams no vehicle-slot set, so the 0x10400 fallback still applies. Left: pass the streamed groups' Spline_Type names.
- RG-4: FIXED. `SegmentOccupancy` is rebuilt each pass from `nearFieldVehicleLaneState` and wired as `q.occupancy` (`sr3_viewer.cpp` near 18255 and 23236).
- RG-5: FIXED. The 65 m pass runs only when `pass(R)` found no crossing at all; the hidden test is `BoxInFrustum` with a +-1.5 m box on the previous frame's view-projection. Left (labelled): the occlusion test is not modelled, and the first frame falls back to the old stand-in.
- RG-6: FIXED. The camera position is sampled per frame; spawn velocity = delta x 30; despawn gate = dot(camFwd, delta) x 30 > 2.2352; behind = dot(offset from camera, delta) < 0; the ring centre is the camera. But see RF-1: the despawn pass is still switched off.
- RG-7: PARTIAL. Done: both chord crossings (`ChordCircleCrossings`), the bit-4 highway rule (`HighwayRuleActive`, trunc(0.75 x target)), with the player segment found by a CHOSEN 10 m nearest-lane stand-in. Left (labelled): the zone-loaded test, obstruction points, the co-op partner test.
- RG-8: CORRECT, untouched.
- RG-9: CORRECT, untouched.
- RG-10: PARTIAL. The factor is now read from `weather.xtbl` `PedDensity`, but `kWeatherStageForPlaytest` (`sr3_viewer.cpp:20258`) is still "Light Rain", so the value stays 0.5 and `Default Spawn` at 14:00 still gives 16 peds, not 32. Fix: default the playtest stage to the game-start stage (Clear Skies, 1.0).
- RG-11: CORRECT, untouched.
- RG-12: PARTIAL. Real-graph cars now run to the 10 m/s lane limit with `AmbientThrottleLaw` (§20.6.3(b), `traffic_following.h`) and a CHOSEN 3 m/s^2 body. Left: `reQueryDistanceThreshold` is still 3.0 (`traffic_lane_follower.h:773`; spec 15 m linear) and the walk lookahead is unchanged.
- RG-13: PARTIAL. Done: `FollowingLaw` matches §20.6.7(b) line by line (a_c = 0.75 a_lim, dEff = d - 5 or d - 3 for kind 4, the 0.75 / 0.5 hysteresis, the 300 ms memory), plus a stand-in obstacle probe (L = v^2/(2 k a_b) + 2.5, +6 m with memory). Not done: the whole §20.6.13 signal controller and the junction yield (see the check below).

**RG-13 against §20.6.13 (checked item by item; all absent, none wrong):**
- Phase timing from zone data and the controller tick (§20.6.13(a)/(b)): absent. `road_graph.cpp:108` keeps only the raw `+0x30` byte; the junction (`+0x50`), phase (`+0x58`), light (`+0x48`) and the two list arrays are not parsed, and no phase timer exists.
- Amber = the last 3000 ms, no all-red, green = duration - 3000, and old red and new green written in one call: absent.
- All junctions start in phase 0 with the deadline armed to phase 0's duration, lightless phases released (§20.6.13(e)): absent.
- Car stop rule: the 5 m stand-off exists in `FollowingLaw` (kind 2), but no kind-2 source feeds it. The stop-if v^2/(2d) <= a_b test (d = distance to the approach-lane end, floored 0.001; amber and red share it) is missing, so cars never stop for a light.
- Stop signs, segment flag bit 7 (§20.6.13(g)): absent. Kind 4 exists in the law but has no caller: no 3 m stand-off, no served mark at < 4 m and < 2 m/s, no 1200 ms hold at brake 100 and cap 0.
- Mode gate (§20.6.13(f)): absent. The ambient cars here have no mode byte. The light and stop-sign tests must run only for mode 8 (HIGH also 14) with `obey_traffic_lights` set; chase (4), flee (2) and rejoin (6) must skip them.

### B. NPC AI (383d8d1)

- AI-1: CORRECT, untouched.
- AI-2: CORRECT, untouched.
- AI-3: PARTIAL. `EngineState::npcAiLineOfSight` is added and wired into `npcAi_->lineOfSight` (`lua_engine_npc_ai.cpp:186`); the runtime gates on it (`ai_npc_runtime.cpp:290`). No production host assigns it (a search of src and tools finds only the definition and the reader), so the default "clear" still applies in the viewer and mission runs. The facing cone is still absent. Fix: assign a collision-ray LOS in `sr3_viewer.cpp` (mission cells), and add the 150 x 80 degree cone.
- AI-4: FIXED. 350-450 ms when `combatant && !targetSet`; the activity-level-6 test is a labelled CHOSEN approximation.
- AI-5: FIXED. `attitude < 3 && !neverTurnOnPlayer` sets `provoked`; attitude 4+ no longer turns on the player; `provocationAtThreshold` is now false (CHOSEN, labelled); the attitude comes from the team-relation matrix and `never_turn_on_player`.
- AI-6: FIXED. 800 ms on the first cover entry when `damageAccumulator < 0.2`, tracked by `coverEnteredBefore`.
- AI-7: NOT ADDRESSED. `findCover` is still not set by any host.
- AI-8: CORRECT, untouched.
- AI-9: FIXED. `fleeFrom` follows the live player position; the cower-run speeds 4.554 (M) / 5.745 (F) match §20.6.9(a) for the within-30 m case; the 250-750 ms delay applies beyond 20 m. Caveat: no host sets `NpcSpawn::female`, so the female speed never applies (labelled CHOSEN default). See RF-5 for the delay gate.
- AI-10: FIXED. `DownedStandsUp` is gone; `DownedType0ExpiryKills` plus `killDownedCharacter` (HP 0, stance 5) implement the type-0 expiry as death (§21.10.1). The heal is `DownedGetUpHeal`, applied only for types 1 and 2 (type 3 +30 percent and type 4 no heal are labelled OPEN).
- AI-11: PARTIAL. The selector `SelectDownedType` (first match wins, matches §21.10.1), the type-1 predicate, the overkill buffer, the 50 ms per point timer extension capped at 7000, and the timers (type 0 30000 ms x scale, type 1 7000, type 2 10000, type 3 8000, type 4 10000) are implemented. Only type 1 dies from damage while downed (types 0/2/3/4 ignore it), as the spec says. But `CharacterState::downedFacts` and `bleedOutHitPoints` are set by no production code (`tables_progression.cpp:1265` reads Bleed_Out_Hit_Points but nothing copies it), so in a real run the selector always returns -1 and every NPC still dies at once. Fix: derive isBrute, isGangMember and follower from the NPC descriptors at spawn and copy the record's `Bleed_Out_Hit_Points`.
- AI-12: NOT ADDRESSED. Invented fire numbers are unchanged.
- AI-13: CORRECT, untouched.
- AI-14: FIXED. A destination that no longer resolves, or is dead, clears the move and does not complete the ticket (`lua_engine_callbacks.cpp:142-155`). Completion without a measurable position is kept and labelled.
- AI-15: NOT ADDRESSED. Only `ai_do_scripted_move` stages a ticket.
- AI-16: NOT ADDRESSED. `character_is_ready` and `on_detection` do not use the loop.
- AI-17: PARTIAL. `provocationAtThreshold` is now labelled CHOSEN. `flag985cd0 = true` (`ai_npc_runtime.cpp:454`) and the fire range of b + 5 m are still unlabelled invented values.

### C. New findings

- RF-1: `sr3_viewer.cpp:23294` still passes `runDespawn=false`, so the RG-6 camera gate and the 140 / 65 m rule never run; vehicles are not recycled by distance. Fix: pass `true` once the gate is verified.
- RF-2: The stop-light and stop-sign stop rules of §20.6.13(f)/(g) have no data path. `FollowingLaw` kinds 2 and 4 have no caller, and road graph segment `+0x30` is read but not consumed. Fix: parse the §13.2a(i) arrays, run the §20.6.13(b) tick (switch, amber < 3000 ms, no all-red, init in phase 0), add the v^2/(2d) <= a_b test at 5 m before the lane end, add the bit-7 3 m / 1200 ms hold, and gate both on mode 8 (14) with the obey bit.
- RF-3: The obstacle probe runs every frame, with a fixed a_b of 8.0 (`kNearFieldVehicleBrakeDecel`), rather than the §20.6.7(a) re-arm timer of trunc(max(0, (15 - v)/15 x 140)) + 60 ms and the vehicle's `AI_Max_Braking_Decel` x condition scale. Fix: read the decel from the vehicle info row and re-arm the probe on that timer.
- RF-4: `absorbDamageWhileDowned` (`lua_engine_state.cpp`) always passes `lethalConstant=false`, so a finishing hit (damage == -1.0, the lethal constant) does not kill a downed brute; and the type-1 entry omits the +1500 ms anim time when HP > 0 at entry (§21.10.1). Fix: pass the lethal flag from the finisher path and add the anim length when HP > 0.
- RF-5: The delayed event 67 gate in `ai_npc_runtime.cpp` (~336-355) measures distance to the player rather than to the resolved threat handle, and releases only when `combatant == false`, so a pending reaction waits while the NPC is in combat. Fix: measure to the threat, and drop the combatant condition.
- RF-6: Type-2 and type-3 downed expiry hooks (`m21_killbane_damaged` value 6 to `M21_Killbane`, Lua `m24_killbane_on_undowned`) are not fired in the diff; the NPC only stands. Fix: fire both at expiry when the mission scripts are hosted.

**Top remaining issues:** (1) no signal controller, stop rule or stop sign, RF-2/RG-13; (2) downed facts never set, so no real NPC goes down (AI-11); (3) LOS seam unset in production and `runDespawn=false` (AI-3, RF-1).

## Review of Team B 0f25023 (merge eb0a9bb): ambient ped navigator (2026-10-17)

- **PN-1 Path search: CORRECT** per §20.6.9(b). The search is best-first over sidewalk nodes. Both ends of the start edge are seeded with their straight-line distance, edges cost their Euclidean length, and costs are capped at 50 000 m. The A* heuristic and the 6000-node expansion bound are labelled CHOSEN; they are harmless because the heuristic is admissible.
- **PN-2 Crossing gate: CORRECT.** A crossing is closed when its light's vehicle state is 2, 3 or 5, or its pedestrian state is 2, and an unlit crossing is never closed (§20.6.9(c)). Its states match §20.6.13: a ped crosses only on green. The kerb-wait, committed and past-far-side handling goes through the existing PedCrossingGate.
- **PN-3 Arrival: OK, with an extension to label.** The spec's 0.425 m arrival radius and event 8 posted 1000 ms later are used. Applying the 0.425 m radius to every intermediate route node, not just the destination, is an extension of the spec; label it CHOSEN in the header.
- **PN-4 Walk speed: DEVIATION (minor).** The viewer passes a constant kWanderWalkSpeedMsChosen = 1.4 m/s. The spec gives clip-driven speeds (§20.6.9(a)): male walk 1.386 m/s, female 1.368 m/s, and a per-ped walk variant (states 26-35) picked uniformly at random at initialisation, with variant speeds from about 0.70 to 2.26 m/s. Fix: take the speed from the ped's walk-state clip, or at least pick a per-ped variant from the table.
- **PN-5 Goal choice: acceptable as labelled.** A random node 10-80 m away is CHOSEN, because the wander-target distribution of sub-state 33 is not specced.
- **Also noted:** a signal controller (	raffic_signals.h) now exists. RG-13 and RF-2 from the 2026-10-17 re-check should be re-checked against §20.6.13 in the next review.

## Review of Team B 019f824 (HUD/minimap) + 5421028 (save flow) + b01145c (respect progression) + 828bed1 (opens-78fb40a) (2026-10-17)

Method: read-only. Diffs read with `git diff <merge>^1 <merge> -- src include tools tests`, compared against spec-hud-runtime.md H.1-H.4, spec-save-format.md A.1-A.4, spec-tables-progression.md sections 5 and B.1-B.3, and spec-lua-api-behaviour.md "Sweep e2eb5b8 OPEN answers". Nothing was built or run. Unit-test bodies were not re-derived; the checks are code against spec text and constants.

Totals: CORRECT 21, DEVIATION 2, NOT-YET-IMPLEMENTED 5, INVENTED 0.

### HUD and minimap (019f824, commit 21e609b)

- **HM-1 CORRECT** - H.1 dataitem field lists, order and type codes. include/sr3hud/dataitem.h (status 13, infrequent 9, respect 5, weapons 25, frequent weapon 8, notoriety 3 per item x 4, hood 4) and src/hud_dataitem.cpp MakeStatus/MakeInfrequent/MakeRespect match the spec. `DataItem::set` returns early on an equal value and sets the dirty flag only on change (hud_dataitem.cpp:44-50), as the compare-then-dirty rule requires. Weapons and frequent-weapon type codes are labelled CHOSEN where the spec gives none.
- **HM-2 CORRECT** - Producers (src/hud_producers.cpp). Health pct is trunc(1000*HP/maxHP)/1000 and is 0 when maxHP <= 0. Cash is cents/100 as a float. Notoriety is L + fraction/T[L + 5*(police)] with the 5.0 cap at L >= 5, and the fake override replaces it. The alias names are brotherhood, ronin, samedi, police. Respect pct uses 22000 or need <= 0 -> 1.0, then minus floor, so 1.0 publishes 0.0. show_upgrades is state == 2. The force-show counter is decremented per push. Radio is pushed only when the record changes. Dual wield halves both ammo counts by arithmetic shift.
- **HM-3 CORRECT** - Display model constants (display_model.h/.cpp). Health start angle 6.3 and end angles 3.142/2.6774/2.2128/1.2836/0.819, bg = fill - 0.035, the |delta| > 0.001 reaction gate, vignette 0.5 - pct below 0.5 and 0.70 - pct below 0.3. Respect meter end = 2.65 - 5.25*clamp(pct), bg = end - 0.03, shown level = L + 1. Cash tick: |diff| > 5 -> floor(diff*0.5). Ammo string forms. Map group alpha = clamp(1.2 - fine_aim). This reproduces the H.4(b) vectors (4.721/4.686; 0.55/0.52; $6,172 ... $12,345).
- **HM-4 CORRECT** - Minimap transform (src/hud_minimap.cpp). screen = C + R(theta_m)*k*(-dX, +dZ). The polar form for blips uses a = atan2(dX,dZ) - 3pi/2 + theta_m, clamped at R67 = 67*UI scale, with e = clamp((r - R67)/(2*R67)). theta = -(h + pi), h = atan2(fwd.x, fwd.z), camera heading when rotating and player heading when static. In static mode the map rotation is 0 and the glyph is rotated by -theta. The north marker is at angle pi on the ring. C = (191,566), ring 148, alpha 0.75, mph factor 2.23694, 13 units/s smoothing, snap-up and the T > speed + 5 reset, cap 100. Zoom = max(m*m, m - s*(m - m*m)/100) with m = 0.75, so k = 0.75 on foot and 0.5625 at 100 mph. Interior x1.75.
- **HM-5 CORRECT** - Base texture and bounds. Pads of 160 m in x and 116 m in z. Tile name `minimap_<name>.tga` with the `sr2_water_minimap.tga` fallback. No valid zone -> `mapsaintsrow.tga` with the world box. UV = ((xmax - X)/w, (Z - zmin)/h). City tile box cx = 320*col - 4000 - 160*(row mod 2), cz = 3640 - 280*row. The interior branch uses the (0,0) centre by default, with `correctedInterior` as an opt-in. That follows the H.3 RESOLVED note and is a faithful port. Category gate bits (10->0x4, 8->0x2, 7->0x1, 0->0x8, 2->0x40, 3->0x80, 9 always) match the pass order in H.3 point 7.
- **HM-6 CORRECT (note)** - Minimap centre selection. Priority is special camera (global in {1,3}), then vehicle, then player. The spec lists player, then vehicle override, then the {1,3} override, so the effective order is the same.
- **HM-7 NOT-YET-IMPLEMENTED (low)** - `MinimapSpeedZoom::update` takes the speed as an input. The vehicle pick (current +0x16D8, else last +0x16C0, else target 0) and the clip-forced-0 single-round weapon case from H.2 are caller concerns and are not coded. Not a value error.

### Save flow (5421028, commit 51abbf7)

- **SV-1 CORRECT** - Offsets (snapshot_blocks.h). Zone name 0x014, respawn crib handle 0x058, clock 0x08C (20 bytes: bytes 4-6 h/m/s, 0x0C fixed time of day, 0x10 play float), play-time u32 0x0A0, flag set 0x0A4 (32 bytes), mission-object count 0x37E8 and records 0x37F0 at 16-byte stride with the flag word at +8, respect 0x4418/0x441C/0x4420, multiplier 0x4A5C, cash 0x4A70, cribs 0x4E60/0x4E62/0x4E68 at 24-byte stride, version 94. Time of day uses floor(sec*2^32/86400) (14:30 -> 0x9AAAAAAA).
- **SV-2 CORRECT** - Load plan (saveflow_load.cpp). Order is streaming wait, resets, version gate, entries 0..31 ascending, load finished, then the post-start wait. The respect multiplier is reset to 1.0 and then overwritten by 0x4A5C unconditionally. Play time is float(u32 0x0A0) on a full load only. Respect is verbatim, with no recomputation. Cash goes through the +-2,000,000,000 clamp. Placement runs only on a full load as host or with no session. Branch (a) "tss01" needs a record with the handle and flag bit 2 clear. Branch (b) resolves the 0x058 handle, else the player-start marker. The stream centre is the explicit point or the marker. Notoriety, vehicle and weather are not restored.
- **SV-3 CORRECT** - Save (saveflow_save.cpp). 0x058 is zeroed first, then set to the nearest unlocked crib by 3D straight-line distance, sr3_city only. No position is written. Clock, cash, respect, multiplier and cribs are patched, then the hash is stamped. The ten crib handles in `kKnownCribs` match the A.3 handle map exactly (names, handles and coordinates).
- **SV-4 CORRECT** - Save availability. Horde or machinima -> not offered. Autosave in progress -> refused. Mission and activity warnings show once and then proceed. Vehicle, notoriety and combat never refuse. `AutosaveAllowed` refuses on startup unfinished, already saving, the two extra flags, and an active mission for `game_autosave` only. `mission_autosave` bypasses the mission gate.
- **SV-5 CORRECT** - New game. Zone sr3_city, 10:00:00, play time 0, respect 0/0/0, multiplier 1.0, respawn crib zero. Starting cash 5,700 is the spec's recommended constant ($57.00) and is commented as such (A.3 says OPEN with a recommendation).
- **SV-6 NOT-YET-IMPLEMENTED (low)** - Unlabelled new-game cribs. `BuildNewGameSnapshot` writes all ten cribs as locked with the "cribs" variable 0 and the known handles. A.3 is silent on this; it is plausible but should carry a CHOSEN tag. Also, the checkpoint branch (`LoadKind::Checkpoint`) is reached only by an option and has no spec-backed caller.

### Respect progression (b01145c, commit fddcfd6)

- **RP-1 CORRECT** - Curve and cap. `ShippedCosts` gives Respect(n) = 2115 + 115*(n-1) for n = 1..49 and 0 for row 50. Cap = sum of all rows minus the last row = 238,875 (checked: 49*2115 + 115*49*48/2). `cost(L)` is record L, with 22,000 for L >= 50. `cumulative` is the sum of the first L rows. B.3 vectors hold: 2,115 -> L1, 10,000 -> L4 with 850 in level, 238,875 -> L49 with 0.
- **RP-2 CORRECT** - Award routine (progression_respect.cpp:73-99). Steps follow B.1.1: amount < 1 or horde -> 0; not locally owned -> network message with the amount returned; overflow when total >= cap or level >= levelCount - 1 converts 1:1 to dollars x100 clamped to +-2e9 and returns 0; otherwise the grant is clamped to cap - total, the total is floored at 0, and hint 0x44 carries L + 1. Sequence B (grant 100 -> 75; grant 50 -> +$50.00) is reproduced.
- **RP-3 CORRECT** - Level-up loop (progression_respect.cpp:44-69). `r = in_level + grant`; loop only when c > 1 and r >= c; L++; milestone when L % 10 == 0 in sr3_city; unlocks of record L applied once; r -= c; c = cost(L); L >= last level -> r = 0 and stop; else stop when r < c. Hint 0xA9 is raised when an unlock is new or the force global is set. in_level = max(r, 0). Matches B.2.
- **RP-4 CORRECT** - Grant sources (progression_grants.cpp). The multiplier scale is round(m*amount). The activity pair is Props_Award then round(Props_Award*(m-1)). Stunt reward: trunc(fraction*Max_Respect), or round(fraction*Max_Cash) once the lifetime stat >= Max_Lifetime_Respect. The shipped rows match all 11 B.1.2 entries (including stat ids 0xB4/0xC0/0xD3/0xB3/0xBB/0xBE/0xC4/0xB0). Toast cash is dollars*100 clamped. The kill table picks rank >= 3 kingpin, 2 gangsta, 1 killa, else thug, and gives 0 for non-gang or excluded victims. Store respect is round(round(price*mult)*m). The constants 100/250/50/100/1000 and 4000/8000/12000 match.
- **RP-5 CORRECT (labelled)** - The 1e-6 "about zero" tolerance and the kill-factor rounding are tagged CHOSEN in code comments. The spec gives neither value.
- **RP-6 NOT-YET-IMPLEMENTED (medium)** - Wiring item 1 of B.3 (replace the test-supplied table in `engine_state.h` `awardRespect`/`applyRespectLevelUp` with the real `respect_levels.xtbl`) is not in this merge. `RespectTable::FromParsed` exists but no engine caller was in the diff, so the new routine is not yet the one the Lua natives use.
- **RP-7 NOT-YET-IMPLEMENTED (low)** - The respect-multiplier additive raise (Respect_Bonus_Purchase1..4 +0.05, Respect_Bonus_Modifier +0.10) is only the one-line `AddRespectBonus`. The unlock-apply wiring to the stored 0x4A5C is absent.

### Opens answered by 78fb40a (merge 828bed1)

The seven OPENs correspond to spec-lua-api-behaviour.md "Sweep e2eb5b8 OPEN answers" (dlc2_m01, m08, m09, m10, m13, m21, m24).

- **OP-1 DEVIATION** (Sec17.29, dlc_gis_film_grain). src/lua_spec_confirmed_stubs_b22_wave2_sec22.cpp:350-355 sets `dlcFilmGrainAvailable` to true the moment `dlc_gis_film_grain_load` runs. Spec port value: the flag becomes true one frame later, and only with no blocking menu. It is reset to false on unload or when the fade ends. The unload stub (b21 wave2 sec17_18, line 850) never clears it: `dlc_gis_film_grain_unload(false)` still leaves the flag true, so a second unload takes the "loaded" path with the fade state set. Fix: set the flag false in the unload stub (immediately for arg 1 false, or at fade end for arg 1 true) and set it true on the next frame tick. The initial false value in lua_spec_initial_state.cpp:209 is correct.
- **OP-2 CORRECT (partial model, labelled)** (Sec21.22, move_to_do, 0x00993630). In a vehicle -> false; arrival < 0.3 m horizontally (dx^2 + dz^2 < 0.09) with record state 1 -> false. The leash, route existence and the record flag +0x58 bit 0x20 are named as not modelled. A character that has arrived with flag 0x20 set should return true per spec. This is a small gap and comes from the missing flag.
- **OP-3 DEVIATION (minor, labelled approximation)** (Sec28.8, get_char_vehicle_is_in_air). b02 sec27_28 lines 706-717 gate on vehicle class 2/3/4 and `!landed && !inWater`. The spec definition at 0x00ad4040 has "no vehicle-class gate": no static-world contact, no wheel contact and not in water. A flying helicopter is airborne, as in the m09 value. With no contact physics the class gate is a reasonable stand-in and is commented as such, but it should carry a CHOSEN tag. A ground vehicle launched off a ramp will read false. Fix: tag it CHOSEN, or drive it from a contact flag when one exists.
- **OP-4 CORRECT** (Sec46.1, +0x1d7a). Spawn value 0 for both bits (lua_engine_state.cpp:223). Bit 0x01 is set iff the zone `script_vehicle_flags` token list contains `invulnerable`. Tokens are split on space, tab, comma or semicolon and compared case-folded. Bit 0x02 is never set at spawn. Not modelled: the release hook that clears bit 0x01 (0x00a397b0), a minor gap.
- **OP-5 CORRECT (partial)** (Sec12.14, vehicle_get_driver). Returns the seat-0 occupant name, nil when empty, and never "". A test-set name wins. The default arg-2-true path should prefer the remote-control controller (+0x1dec flag, +0x1df0 pair); that is not modelled (NOT-YET-IMPLEMENTED, low). `get_char_in_vehicle` seat 0 was also switched to `seat0OccupantName()`; no OPEN resolution cites that change (b23 sec24_25, line 889), so it is an unreferenced extension.
- **OP-6 CORRECT** (Sec30.6, turn_to_do). The refusal predicate is modelled only as "the human occupies a vehicle", which is one of the listed false conditions. For the on-foot NPC with no cutscene it does not fire, as the port value requires. The other conditions (action tags, player and cutscene, 0x009aa360) are named as unmodelled. Polarity (refused -> release the id and push true) is right.
- **OP-7 CORRECT** (Sec7.9, character_exists). `known(name) && get(name)` returns false and never throws for an unregistered name, matching the m24 'Saint_NPC 007' port value. Caveat: the rule is now global to every name never registered. A real zone character that was spawned without an `objectResolves` entry reads false instead of OPEN. This is acceptable only if zone load registers every named object; it was not verified in this diff.

INVENTED: none found. Every unspecified value in all four merges (weapon field types, 1e-6 epsilon, kill rounding, DLC flag timing, class-gated in-air) either carries a CHOSEN/OPEN comment or is listed above.

## Review of Team B 45dd572/8c56b7c (vehicle camera) + b708f08/35e517f (PN-3/PN-4/RP-6 fixes) (2026-10-17)

Read-only review of `include/sr3player/vehicle_camera.h`, `src/player_vehicle_camera.cpp` and the playtest wiring (`viewer_playtest_frame_player.cpp`, `viewer_playtest_session.h`) against `spec-player-runtime.md` §4.15.2-§4.15.6. Nothing was built or run. Test vector §4.15.4 (d 7.53260, elevation 5.3322 deg, camera 1.85 above the reference point, a = 0.0302346) is reproduced by the formulas as written.

### Vehicle camera findings

- **VC-1 Follow distance: CORRECT.** d = d0 * (M + (1-M) * t), d0 = sqrt(FD^2+FH^2), t = clamp(1 - speed/S, 0, 1); S <= 0 gives d0 (`VehicleCamFollowDistance`). FD 7.5, FH 0.7, S 37.9984 (the genki row) match §4.15.4.
- **VC-2 Pivot, elevation, height: CORRECT.** Pivot = reference point + (0, 1.15, 0); base elevation atan(FH/FD); camera FD behind and FH above at zero user pitch. The Y-only rotation shortcut is labelled CHOSEN.
- **VC-3 Pitch clamp and proximity lock: CORRECT.** Total pitch (base elevation + user pitch) clamped to [-35, 55] deg; back.y raised to -0.2 and re-normalised. The exemption test `0x00ac04a0` is not modelled (no car needs it; not labelled).
- **VC-4 Speed FOV: CORRECT for the main formula.** min((base + (Max - base) * speed/S) * scale, Max), scale clamped to [0,1], no 0..1 clamp on speed/S, S = 0 leaves the FOV unwritten, no smoothing, no drunk factor.
- **VC-5 FOV secondary rules: NOT-YET.** The 0.9 * Max_FOV cap (when `0x00ad3290(V)` holds), the +/-30 deg rule and RC-base-only (§4.15.2 FOV row) are absent and not labelled. No effect on the genki test vector. Fix: list them in the header as NOT-YET.
- **VC-6 Follow swing: CORRECT.** r = 1 - exp(K/Swing_Rate) with K = -0.038376417011022570 (5 gives 0.0076459); a = 1 - (1 - clamp(q*r,0,1))^(step*120); N = C + (D-C)*a with C = pivot - committed eye (eye after collision); D = horizontal velocity normalised, times d (the 2026-10-14 resolution); no swing below |v|^2 0.01; N within 1e-5 on all axes stops; heading += (1-hr) * delta; wrap to [0, 2pi).
- **VC-7 Pitch ease: CORRECT.** User pitch times 0.01^step, only when |delta| > 0.001 and min pitch < 0, applied before the clamp. Position lag none (eye rebuilt from the pivot every update).
- **VC-8 Manual-look hold: CORRECT (pad), NOT-YET (mouse).** 10 ms moving / 500 ms stopped; the mouse rule (250 ms, else 0) is labelled CHOSEN. Swing is skipped while the hold runs, which is consistent with sub-state 4.
- **VC-9 Collision: CORRECT (labelled CHOSEN).** §4.15 specifies no collision for the driver camera. The on-foot sphere sweep (radius 0.12, pivot to eye) is reused and labelled. Open question: the original may not collide at all.
- **VC-10 Wiring: DEVIATION (minor), §4.15.2 items 4-5 / §4.15.4.** `viewer_playtest_frame_player.cpp` (camera block, about line 570): `speed` is the planar body speed (labelled CHOSEN; spec is 3-D), and the genki row (S, Max_FOV, M) is used for every vehicle because `setParams` is never called. Fix: build `VehicleCameraParams` from the driven vehicle's row when more than one model is drivable.
- **VC-11 Reference point and FOV axis: CORRECT (labelled CHOSEN).** Ground height under the vehicle as the reference point, and the FOV used as vertical, are both OPEN in §4.15.5 and both carry CHOSEN labels.
- **VC-12 Reverse / sticky: NOT-YET.** Swing_Rate_Rev, the From_Sticky ramp, the heading-retention steering term |w| and the towing exception are not implemented. Numerically identical for the three test groups (Fwd = Rev, 0.2/0.2), so no visible error.
- **INVENTED:** none. All OPEN items touched (reference point, collision, FOV axis, mouse hold, planar speed) carry CHOSEN labels. Dead constants `kVehCamDistance`, `kVehCamHeightAbovePivot` and `kVehCamFovY` were removed.

### Fix verdicts (b708f08)

- **PN-3 (label the arrival extension): FIXED.** `ped_navigator.h` header now labels the 0.425 m radius on every intermediate node as CHOSEN.
- **PN-4 (walk speed): PARTIAL.** Male 1.386 and female 1.368 are the first pool entries and the pick is uniform and stored once per ped (`PickPedWalkSpeed`). Gaps: (a) the pool is the base walk plus listed variant-clip speeds, not a pick among states 26-35 that the anim set has (labelled CHOSEN; the state-to-clip mapping is unknown); (b) the viewer always uses the male pool because gender is untracked (labelled), so the female 1.368 is never used in game; (c) the 1.4 m/s constant remains as the fallback for peds without a stored speed.
- **RP-6 (real respect table): PARTIAL.** `awardRespect` and `applyRespectLevelUp` now run `sr3progression::AwardRespect/ApplyLevelUp` when `setRealRespectTable` has been called, with level-up effects committed through `commitRealRespectLevelUps`. But nothing calls `setRealRespectTable` (the header says "Not yet called by the viewer"), so the Lua natives still use the legacy path in production, and the cash overflow is dropped. Fix: load `respect_levels.xtbl` at host start-up and call `setRealRespectTable`; route `overflowCashRaw` to the wallet.

Counts: 12 findings (7 CORRECT, 1 DEVIATION, 3 NOT-YET incl. VC-5/VC-12, 0 INVENTED; VC-8 and VC-4 are split verdicts). Fixes: 1 FIXED, 2 PARTIAL, 0 NOT.

## Review of Team B 8d6f38b/c0bd5aa (player vitals) + 2d0452d/6f114c1 (ground stand-in) (2026-10-18)

Scope: read-only diff review of merge c0bd5aa (include/sr3player/vitals.h, src/player_vitals.cpp, tools/sr3_viewer/viewer_playtest_frame_vitals.cpp and wiring) and merge 6f114c1 (include/sr3player/node_ground.h, viewer_playtest_setup_world.cpp, viewer_playtest_frame_player.cpp). Nothing was built or run. Specs checked: spec-player-runtime.md 15.3a/15.4, spec-tables-progression.md 10.2, spec-ai-runtime.md 21.10.1, spec-zone-data-format.md 13.2a/13.5.

Totals: PV 9 findings (3 CORRECT, 0 DEVIATION, 3 NOT-YET, 1 INVENTED-minor, 2 SPEC-GAP-only); GH 5 findings (4 CORRECT, 0 DEVIATION, 1 SPEC-GAP). No DEVIATION found.

### Player vitals

- PV-1 CORRECT. vitals.h / player_vitals.cpp: health clamped to [0,max], lethal at 0, damage via the existing engine sink (spec-player-runtime 15.3a). respawn_delay_ms is read from gameplay_constants.xtbl Death_and_busted (spec-tables-progression 10.2, shipped 4000); the 4000 fallback is labelled CHOSEN in frame_vitals.cpp and in the startup log line.
- PV-2 CORRECT. Death effects modelled per 15.4: controls-disabled flag set (player movement, follow camera and engine character), camera mode 9 reported by cameraMode(), respawn timer elapsed since death.
- PV-3 NOT-YET. frame_vitals.cpp: the follow camera stays active; mode 9 is only recorded, not driven (the file comment says so). Spec 15.4 / 4.1 define mode 9; the visible death camera is not implemented. Also no "You died." mission-failure text (mission mode only, correctly deferred to the mission path since step returns early when missionEnabled).
- PV-4 CORRECT. No downed state for the player (spec-ai-runtime 21.10.1: a single-player main player never goes down; type 0 only in co-op). Plain death. Co-op downed is out of scope.
- PV-5 NOT-YET. No health regeneration. Spec-tables-progression 10.2 lists player_health.restore_per_sec_sp=125, restore_wait_time_ms=6000, restore_max_pct_sp and full_restore_time_ms, plus Difficulty_Levels Health_Regen_Wait_Mult / Health_Regen_Speed_Mult, but the runtime consumer is OPEN (not traced). vitals.h correctly refuses to invent it. SPEC-GAP-1: Team A to trace the consumer of 0x014A13CC..0x014A13EC (regen start delay, rate, cap percentage, how the difficulty multipliers apply) so Team B can implement it.
- PV-6 NOT-YET. Difficulty damage scaling (Damage_Received_Mult 0.3 / 1.0 / 1.5 by difficulty, spec-tables-progression 10.2 and spec-expected-table-values) is not applied in vitals; it is only correct if EngineState::applyCharacterDamage already applies it. Not verified in this review (out of the diff). Fix: confirm the engine sink applies Damage_Received_Mult for the player, otherwise add it there (not in PlayerVitals).
- PV-7 INVENTED-minor. frame_vitals.cpp respawn block resets lifeState, isDeadHighConfidence, killFinalizedFlagE3, deathCallbackLatch and playerDeathTimerArmedAtSeconds as "the inverse of finalizeLethalCharacter". This is an inference from the engine state fields, not a spec fact, and has no CHOSEN label. Fix: add a CHOSEN comment (spec-lua-api-behaviour 17736 says the on_death latch is cleared on respawn, which supports the latch reset only).
- PV-8 SPEC-GAP-2 (respawn destination). Spec 15.4 only covers the mission case (death leads to mission failure then checkpoint restore, 0x006D3780). Free-roam respawn location is OPEN; there is no spec for a hospital respawn (the 'Hospital' in spec-building-purchase-runtime is a purchasable property, not a spec'd respawn point). Team B uses the playtest spawn point, labelled CHOSEN in code (correct labelling). Team A to determine the free-roam respawn destination (hospital selection rule, nearest vs fixed) if it exists.
- PV-9 SPEC-GAP-3 (death penalties). Not implemented and not specified: cash penalty (spec-frontend-menu-runtime line 515 mentions a cash-loss/fine routine for busted/hospitalised but gives no amount or trigger), notoriety reset (Team B leaves notoriety untouched, labelled CHOSEN), weapon loss, and the busted (arrest) path versus death. Vehicle exit on respawn is labelled CHOSEN. Team A to supply these values or mark them as not existing.

### Ground stand-in (2d0452d / 6f114c1)

- GH-1 CORRECT. node_ground.h, setup_world.cpp and session.h all carry an explicit CHOSEN label (header says "CHOSEN ground stand-in ... NOT an engine fact"), plus a --no-node-ground switch restoring the old behaviour.
- GH-2 CORRECT (no contradiction). Treating a 0x2244 heightmap sample of exactly -5 as "no data" is consistent with spec-zone-data-format 13.5, which flags -5.0 as colliding with the out-of-bounds sentinel and the population global minimum and lists it as a narrow OPEN item. Team B resolved an OPEN item but labelled it CHOSEN. Risk: a real elevation of -5 m is discarded; acceptable for a stand-in.
- GH-3 CORRECT. Using the nearest road/sidewalk node Y (40 m radius) agrees with 13.2a (node positions are absolute world metres, Y within about 0.07 m of placed objects). The spec does not define terrain height from nodes, so it is a stand-in, not a contradiction. The tile-bounds filter (ignore ground renderers whose XZ bounds do not contain the spawn) is likewise labelled CHOSEN.
- GH-4 SPEC-GAP-4. No spec defines the walkable-ground source for tiles where neither the 0x2237 mesh nor the 0x2244 heightmap has data (for example tile 0915), nor the engine's own ground query fallback. Spec-zone-data-format 13.5 says the heightmap sampler chain is scalar elevation queries only and that 0x2237 is a road/sidewalk decal overlay candidate. Team A to answer the four open questions in Team B's results/freeroam_ground_height_20261018.md (what supplies base ground, heightmap -5 meaning, heightmap u16 vs signed storage given the observed -5 minimum, per-tile local-space ~s mesh placement).
- GH-5 CORRECT. SR3_TRACE_PLAYER_Y is an opt-in diagnostic, off by default, no behaviour change.

## Review of Team B 7b3eb73 (traffic fixes) + fda6d81 (NPC AI fixes) + 5b30de9 (jump/crouch) + 63945cb (enter ambient cars) (2026-10-18)

Method: read-only. Diffs read with `git diff <merge>^1 <merge> -- src include tools tests` for the four merges (commits 086d609, 2305fce, 4483cca, 9b6fdc9), compared with spec-ai-runtime.md 20.6.3, 20.6.7, 20.6.13, 21.3, 21.10.1, spec-player-runtime.md 3.6.3, 3.6.4, 5.6.5, 13.3, 13.4 and spec-lua-api-behaviour.md 7.23 / 7.27. Nothing was built or run. Test bodies were not re-derived.

### A. Traffic fixes (7b3eb73)

| # | Verdict | Spec | Where | Finding | Fix |
|---|---|---|---|---|---|
| TF-1 | PARTIAL (RG-3) | 20.6.1(c) | `viewer_playtest_support.cpp` (`g_streamedVehicleSplineTypes`, `q.includeMask = BuildRoadSpawnIncludeMask(...)`) | The mask is now built per gather by the right function, but the list is always empty, so the 0x10400 fallback still applies and a Highway Only group never adds 0x10. Labelled CHOSEN (streamed-set cadence OPEN, 20.6.1(e)). No behaviour change yet. | Fill the list when a vehicle-slot set is streamed. |
| TF-2 | PARTIAL (RG-12) | 20.6.3(d) | `traffic_lane_follower.h:766-779`, `viewer_playtest_frame_traffic.cpp` (`cmd.headingAligned`) | Done: re-query threshold 15.0 m, linear, with a unit test; cars run to the 10 m/s lane limit under the 20.6.3(b) throttle law. Not done: the 5000 m route walk (lookahead stays 15, labelled CHOSEN with the reason). Car motion now uses a new heading-only flag instead of the aligned gate, a CHOSEN change that drops the "5.0 distance" half. | Add a route walk separate from the steer point; then drop `headingAligned`. |
| TF-3 | FIXED (RF-3) | 20.6.7(a), 20.6.3(b) | `traffic_following.h` `ObstacleProbeRearmMs`, `ConditionScaledBrakingDecel`; `viewer_playtest_session.h` `kNearFieldVehicleBrakeDecel` | The probe re-arm is trunc(max(0,(15-v)/15 x 140)) + 60 ms (200 / 106 / 60 pinned by tests) and the result is reused between runs. a_b = AI_Max_Braking_Decel x {1, 0.8, 0.7, 0.6, 0.5}[level]; the fixed 8.0 is replaced by 12.06 at level 0 (labelled CHOSEN stand-in: one model, no vehicle row). | none (swap in the real vehicle row when it exists). |
| TF-4 | FIXED (RF-2, mode gate) | 20.6.13(f)/(g) | `traffic_following.h` `RunsSignalRule`, `RunsStopLineRule` | The light test runs for mode 8 and 14 with the obey bit; the stop-line rule for mode 8 only with the obey bit. Chase 4, flee 2, rejoin 6 and the rest are excluded (tested). The viewer passes mode 8 and obey = true for all cars (CHOSEN, labelled; no other modes exist in the population). | none. |
| TF-5 | CORRECT (new feature: junction yield gates) | 20.6.7(c) | `traffic_yield.h` | Look-ahead D = v^2/(2 f a_lim) + 5 with a_lim = min(a_b,10) and f = 0.5 / 0.75; the route is walked adding chord lengths to the first segment with a non-empty conflict list; no yield if v^2/(2 a_b) > distance; windows predicted at a constant 3 m/s^2 up to the target speed; the 2 s separation test; applied as law kind 3 at the conflict start (the law gives the 5 m stop); 150-300 ms timer; skipped while a kind-2 memory entry is live. | none. |
| TF-6 | CORRECT (labelled stand-ins) | 20.6.7(c) | `traffic_yield.h` header | CHOSEN and labelled: heading-matched successor at a fork (no route object), predecessor chain capped at 3 segments, timer draw from a hash of id and run count, and an anti-deadlock tie-break (only the car entering later, or the higher id on a tie, yields). The tie-break is not in the spec, where both cars yield when the windows overlap. It is labelled and a question was asked; keep it labelled until Team A answers. | Answer the question: the spec has no tie-break, so cars with overlapping windows both brake (window terms are HIGH only). |
| TF-7 | DEVIATION (minor) | 20.6.7(c) skip rule; 20.6.13(g) | `viewer_playtest_frame_traffic.cpp` yield block | The yield is skipped for a live kind-2 memory or a stop-line hold, but the spec skips it for a live memory entry of kind 4 as well; the approach to a stop sign (memory set, not yet holding) still runs the yield. `StopLineStep` is also called with `recentlyBraked = false` always, so the 0.75/0.5 hysteresis of the kind-4 law is never used. | Check the kind-4 memory entry (id and 300 ms) in the yield skip and pass it into `StopLineStep`. |
| TF-8 | CORRECT (RG-13 re-check: signal controller, `traffic_signals.h`) | 20.6.13(a)-(h), (k) | `traffic_signals.h`, `road_graph.cpp:128-176` | Signal arrays at +0x48 (16 B), +0x50 (20 B), +0x58 (6 B), +0x60 and +0x68 are now parsed. Init is exactly (e): lights 3/2, every junction in phase 0 with the deadline armed from phase 0, phase lanes 1 when phase 0 or lightless, else 3. The tick is (b): amber is the last 3000 ms (vehicle byte only, ped untouched, lanes 2), no all-red, old red and new green written in one call, next deadline timed from the actual switch. The cursor (c) stops at the first true tick; lightless junctions are frozen; hold and release (i) follow the spec including the latent bug; the crossing test (h) is veh 2/3/5 or ped 2. The car rule is `SignalRuleStops`: d floored at 0.001, amber and red share v^2/(2d) <= a_b (test vectors 4, 5, 7 give the right answers: 100/8 = 12.5 > 12.06 drives through). Stop line: 3 m stand-off, arm at d < 4.0 and v < 2.0, 1200 ms hold at brake 100 and cap 0, then drive on (vector 8). Only gap: the "first light of the current phase is resident" gate is a CHOSEN distance radius around the player (the light record has no position), labelled. RG-13 is therefore CLOSED apart from that gate; RF-2 is FIXED. | Use the resident test of the tile that holds the light, once light-to-tile membership is decoded. |

### B. NPC AI fixes (fda6d81)

| # | Verdict | Spec | Where | Finding | Fix |
|---|---|---|---|---|---|
| AF-1 | PARTIAL (AI-3) | 10, 12, 21.1 | `viewer_walk_support.h` `RealPropLineOfSight`, `viewer_playtest_setup_audio_hud.cpp`, `ai_npc_runtime.cpp:362-380` | The seam is now assigned in the viewer, using real prop collision (closest-hit rays), and the answer is cached and re-asked every 700-4000 ms (+500 ms after a visible answer), as specified. Ray height 1.6 m is CHOSEN and labelled. Remaining: the 150 x 80 degree view cone is still absent (labelled), and building shells are not `.clmesh_pc` props, so walls do not block (labelled in the class comment). A hostile NPC still sees the player through a building wall. The uniform draw of the re-ask interval is labelled CHOSEN. | Add the facing cone when NPC facing exists; add street/building collision to the ray set. |
| AF-2 | PARTIAL (AI-7) | 21.3 | `ai_npc_runtime.cpp` `searchCover`, `lua_engine_state.cpp` `loadZoneObjectsSection` | Done: a cover_node pool from the zone (positions only), AABB half-width 14.0 and half-height 4.0, unclaimed filter, failure list 20, claim by owner handle, release on death and on abandon, back-off 6-8 s on failure. Deviations: (a) the brute half-width 15.0 is not modelled (labelled), (b) the survivors are not put on the failure list when a search fails (unlabelled), (c) the evaluator 0x00509e50 is replaced by "the ray from the player to the node is blocked": the arc test with node yaw and the cover_angle / cover_def_angle bytes is not used (labelled), (d) the hard gate is "the player is alive and a LOS seam exists", not "a list-0 entry with flag 0x8 resolving to the player" (labelled), (e) the threat term is forced to 0 and the cost is plain distance, so wA/wB (5/10, 8/7) never matter (labelled). The ranking is usable but different from the engine's. | Add (b); decode node yaw and the four angle bytes, then implement the arc test and the threat term from the list-0 entries. |
| AF-3 | PARTIAL (AI-11) | 21.10.1 selector | `lua_engine_state.cpp:392-405` | Type 0 now fires for a non-player follower: the character is on a leader's party list, the player is excluded. That is the selector's second row. Brutes (type 1: brute AND gang member) still have no source for their two facts, so m19's brutes still die at once; the Killbane and avatar rows have no data source either. A `downedEntered` counter is added. | Derive isBrute and isGangMember from the NPC descriptors at spawn. |
| AF-4 | CORRECT (AI-16, on_detection) | spec-lua-api-behaviour 7.23 | `ai_npc_runtime.cpp` (`step.detectedPlayer`), `lua_engine_npc_ai.cpp` | Fires on a NEW list-0 entry for the player (edge on `hasEntry(KnownHostiles)`); a refresh does not fire; arguments (detector_name, target_name); only with a registered callback. The edge is detected per tick, so a remove-and-recreate inside one tick would be missed; harmless. | none. |
| AF-5 | CORRECT (AI-16, character_is_ready) | spec-lua-api-behaviour 7.27 | `lua_spec_confirmed_stubs_common.h:496-515` | An NPC tracked by the AI loop is not ready when its mode is Stunned (34), or Idle (0) with sub-state 0x26. The player is never tracked. Untracked NPCs keep the old rule. | none. |
| AF-6 | NOT-YET (AI-17 carried over) | 10, 21.1 | `ai_npc_runtime.cpp` (`provocationAtThreshold`, `flag985cd0`, fire out to weapon range b + 5 m) | The three unlabelled fill-ins of AI-17 are not touched by this merge and are still unlabelled. | Label each CHOSEN. |

### C. Jump and crouch (5b30de9)

| # | Verdict | Spec | Where | Finding | Fix |
|---|---|---|---|---|---|
| JC-1 | CORRECT (launch) | 5.6.5 | `jump_crouch.h` `JumpLaunchSpeed` | speed = sqrt(2 x \|g\| x h), velocity = -gravityDir x speed with gravity (0,-9.8,0); gated on h > 0; the controller clears the grounded state on launch. Tested. | none. |
| JC-2 | DEVIATION (minor, labelled) | 3.6.4 step 2 | `jump_crouch.h:68-70`, `JumpGateInputs.blocked` | The gate checks the jump press, the 0x200 bit, no melee and the controls-disabled skip: correct. But `blocked` stands in for 0x00989670 as "unnamed", and the airborne input is wired to the "already jumping" predicate 0x0098b970. Per the spec, 0x0098b970 only suppresses the ledge probe, and 0x00989670 is "climbing or vaulting" (spec-player-runtime.md line 1529: "Return at once while climbing or vaulting (0x00989670)"). The extra supported test (within 0.1 m of ground) is CHOSEN and labelled. Net effect: no mid-air second jump here, which is a safe stand-in. | Name `blocked` = climbing-or-vaulting state; keep the airborne and 0.1 m guards as labelled CHOSEN. |
| JC-3 | DEVIATION (labelled) | 5.6.5, 3.6.4 | `kDefaultJumpHeightMetres = 1.0` | The default jump's height argument is not in the spec; 1.0 m is CHOSEN and labelled (question T2-Q1). Absent and not listed as NOT-YET: the sprint-held argument of 0x0098bea0, the ledge probe and climb (0x0058ae80 / 0x00989ed0), the cover vault, the jump penalty hook (0x009fffa0), the launch correction when falling faster than -3 units/s, and the 1.5 x upgraded-jump factor. | List these as NOT-YET in the header; leave the height as CHOSEN. |
| JC-4 | CORRECT (crouch wiring) | 3.6.3 | `viewer_playtest_session.h` `held()`, `viewer_playtest_frame_input.cpp` | Crouch logic is the existing `PlayerMovement::crouchRoutine`; the viewer supplies a held crouch from a keyboard source (output 2, "kb"), so the hold-mode branch (step 2) runs. Permission bits 0x2000 and the new 0x200 are set in the setup. Space and C are the task's own choice and are labelled (no default key is published). | none. |
| JC-5 | NOT-YET | 3.6.3 step 3 | viewer | No CROUCH_TOGGLE press or pad toggle path is wired (toggle mode). Only hold mode is exercised. | Wire a toggle key when needed. |

### D. Enter, drive and park ambient cars (63945cb)

| # | Verdict | Spec | Where | Finding | Fix |
|---|---|---|---|---|---|
| EC-1 | CORRECT | 13.3 steps 3-4 | `ambient_vehicle_entry.h` `PickAmbientEntryTarget` | Proximity only (no facing rule), squared-distance range reject, 2-slot closest-first sort, sticky key forced to 0 (the sticky id is never supplied; the spec marks +0x1e30 as a HYPOTHESIS, so fine), up to 2 candidates validated in order, first success wins. | none. |
| EC-2 | DEVIATION (minor) | 13.3 step 4 | same, `if (!ok && fastFlag) ok = validate(c)` | The engine clears request bit 0x23 bit 0 when it retries, so only ONE retry happens across both candidates. Here the second candidate is retried again. The viewer never sets the fast flag (the spec ties it to sprint held, 13.2) and the validator is deterministic, so there is no observable effect. | Clear `fastFlag` after the first retry; pass sprint-held. |
| EC-3 | DEVIATION (labelled) | 13.3 step 3 | `kAmbientEntryRange = kVehicleEnterRadius`, XZ distance | The max-range constant at 0x01115204 has no value in the spec: CHOSEN and labelled. The spec distance is a squared distance after a local-space transform (3D); the code uses the ground plane (labelled). | Use 3D distance if the range ever matters vertically. |
| EC-4 | NOT-YET | 13.4 | `viewer_playtest_frame_input.cpp` candidate build | Only a stand-in validator runs: ambient cars are eligible unless a seat holds an "important" occupant (CHOSEN reading, labelled). Not run: the seat count and seat-scoring gate, the team-conflict scan, the entry-speed threshold (a moving car is entered at any speed; the code prints "(moving)"), the two obstruction gates and the grace-period check. | Add the entry-speed gate first: it decides whether a moving car can be entered. |
| EC-5 | DEVIATION (labelled) | 13.4, section 10 | carjack in `viewer_playtest_frame_input.cpp` | Every ambient car is treated as occupied and the driver is "simply removed" (CHOSEN, labelled); no driver ped is created, ejected or reacts, and the car is loaded into the shared rig with the rig's own physics row. The car leaves the traffic list at entry. Acceptable as a stand-in; it must not be read as the carjack behaviour of section 10. | Model the occupant exit when peds in cars exist. |
| EC-6 | INVENTED (partly labelled) | 20.6.1(d) | `viewer_playtest_frame_traffic.cpp` parked-car despawn | Cars the player leaves are kept as "parked vehicles" and despawned by the 20.6.1(d) ambient rule (140 / 65 m). Only the exemption of the original car is labelled CHOSEN. The spec says nothing about whether a vehicle the player used stays under the ambient recycle rule. When the rig's car despawns, the rig silently reverts to the original car. | Label the whole persistence rule CHOSEN, or ask Team A what the engine does with an abandoned player car. |

**Tally.** Traffic fixes: RG-3 PARTIAL, RG-12 PARTIAL, RF-3 FIXED, RF-2 FIXED; junction yield CORRECT; signals (RG-13) CLOSED except the resident gate (TF-8). NPC AI: AI-3 PARTIAL, AI-7 PARTIAL, AI-11 PARTIAL, AI-16 FIXED (both halves). Jump/crouch: JC-1 and JC-4 CORRECT, JC-2 and JC-3 DEVIATION, JC-5 NOT-YET. Enter ambient: EC-1 CORRECT, EC-2/3/5 DEVIATION, EC-4 NOT-YET, EC-6 INVENTED. Priorities: (1) the cover evaluator (AF-2) and the brute facts (AF-3) still decide mission outcomes; (2) the TF-2 route walk; (3) the EC-4 entry-speed gate; (4) label EC-6.

## Review of Team B 73e1382/3b65d6f (weapon handling) (2026-10-18)

Scope: include/sr3weapons/weapon_inventory.h, src/weapon_inventory.cpp, the refire change in weapon_fire.h/.cpp, and the viewer wiring (keys, HUD, starting loadout, weapon-state-trace). Read-only review against spec-weapons-runtime.md Sec2.2c/Sec3.1, spec-tables-weapons-combat.md Sec2.4/Sec5.1. Nothing was built or run.

- WH-1 CORRECT. Reload arithmetic: take = min(maxClip - clip, reserve), reserve -= take, clip += take (Sec2.2c, 2026-10-14 resolved note: clip at instance +0x1a4, reserve at +0x1ac). src/weapon_inventory.cpp update() matches. The header correctly states that the real maxClip also folds in upgrades and that this is not modelled.
- WH-2 CORRECT. Reserve cap = Max_in_Reserve of the weapon's Ammo row, matched case-insensitively by name (tables Sec5.1). Clamped at add() only, which is the only grant path that exists today.
- WH-3 CORRECT. Flag names and bits used: word A 0x2 infinite magazine capacity, 0x8 unlimited ammo; word B 0x80 disallow reload, 0x40000000 player instant reload (tables Sec2.4). Reload_override_time_sec read as an f32 seconds value, 0 when absent (Sec2 offset +0x3C). Per-shot Ammo_per_Shot decrement clamped at 0 is also as specified.
- WH-4 SPEC-GAP. Default reload duration (kDefaultReloadMs = 2000) and weapon switch duration (kSwitchMs = 500) are labelled CHOSEN, which is honest. The spec only says (weapons-runtime ~line 510) that equip/holster/reload-step timing lives in flat-millisecond timers in the equip state machine, with no values. Team A needs to extract the equip, holster and reload-step ms values (or confirm that Reload_override_time_sec is the whole duration). Until then the values are placeholders.
- WH-5 SPEC-GAP. The reload takes effect at the END of the timer in Team B. In the executable the reload function transfers the ammo in a single step; when that step is called relative to the animation is not specified. Needs a spec line: transfer at reload start, at an animation event, or at completion.
- WH-6 DEVIATION (minor, model). Reserve is stored per weapon slot in the inventory (Slot.reserve, seeded 3 x Magazine_Size). In the executable the reserve at +0x1ac is filled from the owner's ammo pool (0x00b860e9, Sec2.2c) and Total ammo = clip + reserve. Fix: when owner-pool behaviour is specced, key the reserve by Ammo type shared across weapons that use the same Ammo row, instead of per slot. Today two weapons sharing an Ammo row would hold independent reserves.
- WH-7 INVENTED but labelled CHOSEN (acceptable): starting loadout (pistol, first smg-slot, first shotgun-slot rows), starting reserve of 3 magazines, keys (R, 1-9, Q, Tab), auto-reload on dry-fire, switch cancels an in-progress reload, firing blocked while switching or reloading. The spec gives no PC default keys. Switch-cancels-reload has partial support (reload cancel exists for vehicle entry, Sec10.2). Auto-reload and block-while-reloading carry no spec support; keep them labelled CHOSEN.
- WH-8 CORRECT (labelled OPEN). Per-weapon refire gate fed from Time_Management.Refire_Delay: Sec3.1 states a HIGH-confidence gate fed by a weapon-definition-derived u16; the header says equality with Refire_Delay is OPEN. Fine as labelled.
- WH-9 DEVIATION (low). Infinite ammo: IsInfinite uses inst.infiniteAmmo(), but the flag constants kFlagAInfiniteMagazine and kFlagAUnlimitedAmmo are never applied anywhere in the diff, so a row with word A 0x8 (for example the sp_gat01_w special case) is only infinite if the caller sets it. The start-loadout picker excludes such rows (flagsA & 0xA) and, oddly, word B 0x1 (flagsB & 0x1 is unexplained; the spec has no bit-0 meaning cited). Fix: set inst.setInfiniteAmmo(true) in add() when flagsA & 0x8 (and 0x2 for no-reload clip), or drop the unused constants. Spec: instance +0x1a0 bit 0x40 is the infinite-ammo state and the clip is set to 0x7fffffff.
- WH-10 NOT-YET. Ammo pickup (walk-over grant, and the heavy-weapon pickup-only path; spec-weapons-runtime ~lines 834-980 are still OPEN on the real trigger), ammo-capacity and infinite-ammo upgrades, secondary ammo (+0x1b0/+0x1b4), weapon radial and Inv_Slot-based slot grouping (Inv_Slot is only printed, not used for selection), and the "drops with full reserve" word-B bit 0x20000000. No spec-gap on the first two beyond what the spec already marks OPEN.
- WH-11 SPEC-GAP. Selection order and the radial: the spec gives no per-Inv_Slot selection order or "last weapon" rule, so selectNext/selectPrev by add order is an unspecified stand-in (covered by CHOSEN, but not individually labelled in the header).

Tally: CORRECT 3 (WH-1, 2, 3) plus WH-8; DEVIATION 2 (WH-6, WH-9); NOT-YET 1 (WH-10); INVENTED-labelled 1 (WH-7); SPEC-GAP 3 (WH-4, 5, 11). Priorities: extract the reload and switch timing values (WH-4/5) and the owner ammo-pool model (WH-6).

## Review of Team B b67ada6/07c0776 (weather schedule) + 98e803b/d6df169 (Genki) (2026-10-18)

Scope: `git diff <merge>^1 <merge> -- src include tools tests` for both merges. Weather: include/sr3render/weather_schedule.h (new), tod_lighting.h/.cpp (ComputeTodLightingBlended), the viewer wiring (weatherTick, --weather / --weather-seed / --weather-time-scale / --tod-seconds-per-day) and tests/synthetic_weather_schedule_test.cpp. Genki: include/sr3activity/genki.h, src/activity_genki.cpp, the viewer StepGenki and testGenki. Checked against spec-tables-environment.md Sec2, Sec4.1 and Sec18; spec-ai-runtime.md Sec20.6.0(d); and spec-activity-runtime.md Sec6.9. Where the spec was silent, this pass read the executable statically (weather tick 0x005A6FE0, blend 0x005A6300, accessor 0x005A6920, smoothing 0x00DAD960). Nothing was built or run.

Weather schedule (07c0776):

- WS-1 CORRECT. Load step (Sec2): each Chance is divided by the sum of all Chance values (a zero sum is left as raw values, as in the spec); Next_Stage names resolve case-insensitively and unmatched names are dropped (the spec stores NULL; same effect).
- WS-2 CORRECT. Each entry's duration is a uniform random value in [avg - var*avg, avg + var*avg]. The schedule advances when elapsed passes the duration, carrying the overshoot (elapsed -= duration); this matches 0x005A73FD, which re-arms the next entry's timer at duration - elapsed. Planning on the last entry is equivalent to the engine moving the last two entries to the front. Two negligible differences: the engine advances at most one entry per tick and uses a strict ">".
- WS-3 DEVIATION. Initial clock priming, weather_schedule.h:258 (banner :27): `elapsed_ = -Rand01() * d`, so the first stage lasts LONGER. In the executable (0x005A735E-0x005A73E6) the entry timer is armed with -(r * d * 0.48 * 1000) ms (0.48 is the double at 0x0111EAF0). The timer arm 0x00D9E140(v) stores now + v, so the elapsed time starts at +0.48*r*d and the first stage is SHORTER. Spec Sec4.1's "primed with a negative random fraction" describes the arm argument, not the elapsed value, which is how Team B misread it. Fix: `elapsed_ = 0.48 * Rand01() * d;` and correct the banner.
- WS-4 CORRECT. Ped-density coupling (spec-ai-runtime Sec20.6.0(d)): the factor is the PedDensity of the CURRENT schedule entry's stage, not blended (viewer_playtest_session.h:818). The engine accessor 0x005A6920 returns entries[current].stage, which confirms this. It replaces the earlier fixed "Clear Skies" density / "Light Rain" lighting split, so Light Rain = 0.5 now applies only while the schedule is in Light Rain, and lighting and density use the same stage.
- WS-5 SPEC-GAP, answered here. The smoothing primitive is labelled CHOSEN as smoothstep, `3b^2 - 2b^3` (weather_schedule.h:107-109). The engine's 0x00DAD960(t, 1.0, 0.5) is a piecewise-quadratic ease: S(t) = 2t^2 for t <= 0.5, and 1 - 2(1-t)^2 above (HIGH: the acceleration branch is read; the deceleration branch is by symmetry). Both curves give 0.5 at the boundary, so the blend stays continuous. Fix: change WeatherSmooth's body. The corner layout is CORRECT given a symmetric ease. The engine uses (A = previous, B = current, t = f + 0.5) in the first half of an entry and (A = current, B = next, t = f - 0.5, clamped to [0, 0.5]) in the second; that equals Team B's "neighbour weight = S(|f - 0.5|)".
- WS-6 CORRECT (by analogy, labelled). Target pick and route: a Chance-weighted pick, then a shortest path over Next_Stage_List with edge cost |Chance(u) - Chance(v)| * duration(v), bounded by the stage count. This is the Sec4.1 wind mechanism, which Sec18 says weather reuses. The weather planner itself was not re-read in this pass.
- WS-7 CORRECT. ComputeTodLightingBlended does the Sec18 2x2 corner blend (segment low/high x stage A/B, two scalars). If any corner is missing it falls back to the old fixed defaults, which is labelled.
- WS-8 SPEC-GAP (all labelled CHOSEN, not INVENTED). These items are labelled CHOSEN: the RNG (the engine uses a 0x2000-entry precomputed table, 0x00DAB5E0, which cannot be matched); real versus game time for the clock (the engine timer is the ms clock 0x01320D94; whether it pauses is open, as in spec-ai-runtime); the same-stage re-roll (8 tries) and the direct append when the target is unreachable; and the Clear Skies lighting stand-in (Clear Skies -> Overcast cells). The spec does not say what an absent weather_time_of_day cell inherits; that is a Team A question.
- WS-9 NOT-YET. The Sec18 Temp_Settings temperature roll per schedule entry is not ported (the engine writes a random day/night temperature into each 20-byte entry; a forced stage uses the min/max average). Nothing consumes it yet. A forced stage in the engine fills three entries and is re-applied every tick; Team B's Pin (two entries, never advances) behaves the same for --weather.

Genki (d6df169):

- GK-1 CORRECT. Win/lose (Sec6.9): reaching the exit with cash >= Required_cash (Required_cash_coop in co-op) before Time_limit wins; timer expiry or death loses. Level fields read from Level_Exit; Reward2 Homie_Pierce present; reward via the Sec6.1 grant on success.
- GK-2 CORRECT, now confirmed. Kill cash = victim-row Base + every applicable bonus, additive (genki.cpp:109-120, labelled CHOSEN). This pass confirms the additive sum in the kill callback 0x0064C1F0. Still missing (NOT-YET, part of question 7): each term is multiplied by the combo factor in force and truncated, trunc(value * M), before summing.
- GK-3 CORRECT. Target and trap-evade values match Sec6.9 (Ethical +5 s/+$50, Unethical -$100/-10 s, Money +$100, Time +25 s, Button +$100, Health 225, electric +$70, fire +$50). The trap evades' Bonus_Time is read from data (shipped as 0).
- GK-4 SPEC-GAP, answered in spec-activity-runtime Sec6.9.1. Unethical target (genki.cpp:132, "not clamped (6.9 gives no floor)"): the engine floors the run total at 0 (if total + delta < 0, total = 0). The cash deduction is trunc(-100 * M), and the target ends the combo. Fix: `cash_ = std::max(0.0, cash_ + std::trunc(-deduction * factor));`, then break the combo. Money, Ethical, Button and trap-evade cash are also multiplied by M (time never is).
- GK-5 NOT-YET. The combo tables are read but not applied (genki.h:9, GenkiComboFactor at genki.h:50). The table lookup itself is right (largest Threshold <= count, else 1.0). Usage: pay kill k with the factor for count k-1. The full rule is now in Sec6.9.1.
- GK-6 NOT-YET. Presents (Explosion_Timer, Alternative_Cash_Amount), Event_Multiplier, trap damage, and applying the Health target's HP to the player (target() only returns the value) are not ported.
- GK-7 CORRECT (test aid). --genki-test-kill-every adds a synthetic Hunter kill and immediately tests the exit (viewer_playtest_activity.cpp:257). It is labelled as a test aid and is off by default.

Question 7 (Genki combo) answered in spec-activity-runtime §6.9.x (written as §6.9.1 "Combo rule (2026-10-18)"). Only kills count. Each kill is paid at the multiplier in force before its own count step and refills trunc(trunc(Base_decay_rate * F) * 4.0) ticks, capped at Max_combo_ticks. Each frame the ticks drop by trunc(trunc(Base_decay_rate * F) * dt), where F = Decay_rate[d].Factor and d steps (once per call) as the combo's banked cash passes 200/400/600/800/1000. The combo ends when the ticks reach 0, on an Unethical target, or at level start. Seven test vectors are included.

Tally: weather CORRECT 5 (WS-1, 2, 4, 6, 7), DEVIATION 1 (WS-3), SPEC-GAP 2 (WS-5, WS-8; WS-5 answered here), NOT-YET 1 (WS-9), INVENTED 0. Genki CORRECT 4 (GK-1, 2, 3, 7), SPEC-GAP 1 (GK-4, answered), NOT-YET 2 (GK-5, 6), DEVIATION 0, INVENTED 0. Priorities: (1) the WS-3 priming sign and factor; (2) port Sec6.9.1 (GK-5) together with the GK-4 floor and the multiplied target cash; (3) the WS-5 ease curve.

## Review of Team B 398713b (vitals §18) + 9f14648 (walkable ground) + 8c4d173 (activity-heli) (2026-10-19)

Read-only review (git show, spec greps). Team B repo untouched, nothing built or run.

### 398713b vitals (spec-player-runtime.md §18)
- V18-1 CORRECT: regen wait 6000 ms x Health_Regen_Wait_Mult (SP 0.6/1.0/1.3), re-armed on every damaging hit on the player (include/sr3player/vitals.h kRestoreWaitTimeMs, RegenWaitMs; src/lua_engine_state.cpp armHealthRegenWait).
- V18-2 CORRECT: rate 125 HP/s x Health_Regen_Speed_Mult (1.3/1.0/1.0) x unlock factor, fixed 1/30 s step (RegenRatePerSec, HealthRegen). Unlock factor defaults to 1.0 (host must supply 1.25..2.0): NOT-YET (progression bits not exposed).
- V18-3 CORRECT: Damage_Received_Mult 0.3/0.75/1.5 applied in the damage sink for the player and followers (damageReceivedDifficultyMultiplier, ApplyDamageSink).
- V18-4 CORRECT: respawn_delay_ms 4000 for on_death, fade reuses 4000 ms (fadeMs), so respawn at about 8 s.
- V18-5 CORRECT: four level_respawn death-flag hospitals with the spec coordinates; PickRespawnPoint nearest by flag.
- V18-6 CORRECT: penalty min(cash, $500) via DeathPenaltyCents; weapons kept; followers dismissed; HP to max.
- V18-7 NOT-YET: SP notoriety reset is cleared via notoriety tracks (HIGH in spec); alert-ring clear present. Weapon slot-0 select not applied (no backend).
- V18-8 NOT-YET: §18.10-18.18 (respawn wipe 0.83 s, on_respawn, player_start fallback (1220.94, 12.16, 1047.19)) post-date the commit; not implemented, not a deviation.
- V18-9 CHOSEN-LABEL gap: Busted_cost/Bust_HP_Pcnt constants present but unused by this commit; no INVENTED values found.

### 9f14648 walkable ground (spec-zone-data-format.md §13.5.1)
- WG-1 CORRECT: ray from +0.05 m to -500 m, closest (highest) hit (kGroundRayUp/kGroundRayDown, groundHit).
- WG-2 CORRECT: source is the ~s 0x2237 tail collision mesh; heightmap 0x2244 explicitly not used as ground (removed from the character chain).
- WG-3 CORRECT: test asserts tile 0915 spawn ground 32.3-32.8 m (spec 32.4-32.7).
- WG-4 NOT-YET: clmesh objects (h0..h3) not added; code cites spec (b)2 that they are props, but §13.5.1 lists them as a second source. Needs object placement (§11). Triangle list is a strip stand-in (MOPP walker missing): SPEC-GAP/CHOSEN.
- WG-5 INVENTED (labelled CHOSEN): walkable filter |normal.y| >= 0.7 vs spec 0.715. Fix: use 0.715.

### 8c4d173 activity-heli (spec-activity-runtime.md §6.5)
- AH-1 CORRECT: Objectives_Needed win, four objective blocks keyed by Level_number, lose on death / heli destroyed / protected car destroyed, timer only when Use_Timer.
- AH-2 CORRECT: Protect_target fields (Vehicle_hit_points, Time_between_spawns, coop, Chance_of_rocket_launcher) read; Level_number 1-based labelled CHOSEN.
- AH-3 NOT-YET: Heli_Health_Multiplier read but not applied; spawns, Friendly_Fire_Mult, spawn ring unimplemented (spec §6.5 itself OPEN on the reader).
- AH-4 CORRECT/N-A: no player helicopter flight code touched; spec-physics-runtime.md §10.13 (2026-10-18) not violated.

Tally: CORRECT 12, DEVIATION 0, NOT-YET 5, INVENTED 1 (WG-5, minor), SPEC-GAP 1.

## Answers to Team B R3 questions (2026-10-19)

Each answer is a dated note "[2026-10-19 Team B R3: FINAL/HIGH — ...]" in the spec that owns the topic.

| # | Team B question | Answer in short | Where |
|---|---|---|---|
| 1 | Respawn reset calls (lifeState etc.); PV-7 | `0x00703840` phase 2, then revive `0x009e6f20`: full HP, stop ragdoll, leave downed (`0x00973700`), clear crouch, vehicle/seat exit, slot 0, notoriety reset, controls on, busted bit off, handles released, `on_death` latch and death timer cleared, in 18 ordered steps | spec-player-runtime.md §18.6, note after "Released handles" |
| 2 | `0x00985cd0` + provocation meter; AI-17/AF-6 | `0x00985cd0` = is crouched (`+0xe8` bit `0x20`), so `flag985cd0 = true` is wrong. The meter is the 150 HP / 6 s friendly-fire rule: sum of min(dmg, 150) per player hit, 6000 ms expiry re-armed per hit, no decay, provoked while sum >= 150 and the timer runs; enabled only for non-hostile humans without `never_turn_on_player` | spec-ai-runtime.md §24.9 friendly-fire bullets (also §10.2/§10.3) |
| 3 | 8-entry memory list needed?; TF-7 | A traffic driver's 300 ms braking memory (`+0xee8`), not an NPC attacker list. Needed for the brake hysteresis and junction-yield skip; `recentlyBraked` is equivalent; the cap of 8 (new targets dropped when full) almost never matters | spec-ai-runtime.md §20.6.7(b) |
| 4 | `overflowCashRaw` wallet; RP-2 | Same wallet, player `+0x1ca0` (cents), via `0x0094D920`: saturating add to ±2,000,000,000, then floored at 0. Max cash $20,000,000; the excess is dropped, not banked | spec-tables-progression.md, respect award step 3 |
| 5 | VC-5 formulas | fov = base for RC, else min((base + (MF − base)·v/S)·scale, MF); then watercraft cap 0.9·MF; then +/-30° by a mode global (2 → −30, 3 → +30); S = 0 writes nothing | spec-player-runtime.md §4.15.2, note after the table |
| 6 | `vehicle_mark_as_players` host-side? | No: `+0x2880` is written on the machine that owns the player (non-owners forward it by network record); only the "player2" streaming hold (priority 5) is host-only. Single-player does both | spec-ai-runtime.md §20.6.14(b); pointer in spec-lua-api-behaviour.md §22.5 |

## Review of Team B 176363f..HEAD (weapons 5.4, R3 batch, activity e2e) (2026-10-19)

Scope: f11dba1 (weapons 5.4), e133edf (R3 batch), 873d5f6 (activity e2e), plus 1074bf4 (Genki combo, not reviewed in depth). Police-response cb1a2a3 and HANDOFF/spec-sync commits are out of scope. The 2026-10-19 answers (5.4.x, respawn steps, 0x00985cd0 = crouched, cash cap) post-date these commits, so conflicts are NOT-YET, not DEVIATION.

### Weapons 5.4 (f11dba1)
- W54-1 WH-5 FIXED. The transfer runs once at the end of the timer; cancel moves no ammo; idle top-up 5000 ms re-armed by shots and noteBusy; shotguns load the whole clip. Matches 5.4.1.
- W54-2 WH-6 FIXED. Reserve is an owner pool keyed by Ammo row, take = min(maxClip - clip, pool). Matches 5.4.1 completion step 2. Gap: the "owner not player type -> straight to maxClip" branch (step 1) is not modelled for NPCs. NOT-YET, low.
- W54-3 WH-4 PARTIAL. No flat switch timer, old weapon stowed at once, slot memory kept: CORRECT. PlanReload frames (Pistol 44, Rifle 71, Shot Gun 96, SawedOff 85, Sniper 79, RPG 38) CORRECT. NOT-YET from 5.4.x item 1: AK, Stun Gun and Minigun have no clip, so reload is instant (Team B uses kDefaultReloadMs = 2000); DualPistol 44 f/1466 ms, StagRifle 70 f/2333 ms, Genki 38 f/1266 ms (Team B also uses 2000); SMG rows resolve via their own reload group (Pistol or Rifle), so check SMG-Gang uses 44 f.
- W54-4 WH-4 NOT-YET. PlanEquip knows only Pistol and the Rifle chain. Missing per 5.4.x item 2: melee 2h 24 f/f12 (400/800 ms) and brute 41 f/f1 (33/1366 ms); the Pistol-group SMGs (tec9, stun_gun) are covered.
- W54-5 DEVIATION (5.4.x item 3: tag 0x1d86 is in neither reload gate list). weapon_inventory.h header: "reload is refused while the new weapon is still in its draw window". Fix: allow startReload during the draw window. The "firing blocked while reloading" line is labelled no-source; it stays INVENTED-labelled.
- W54-6 WH-9 FIXED. Word A 0x2 and 0x8 set the infinite state at add(); labelled HIGH for the bit mapping, which is honest.
- W54-7 WH-11 FIXED. Selection by Inv_Slot (number key k selects slot k-1), cycle skips 8/9/empty/disabled, 50 ms repeat, 650 ms radial, last weapon = slot left. Out-of-ammo order 5,4,3,2,1 then melee fallback matches 5.4.3.

### R3 batch (e133edf)
- R3F-1 EC-6 FIXED. abandoned_vehicle_schedule.h matches 20.6.14(d): rows {5,10 s},{6,5 s},{7,0} out and {5,30},{6,20},{7,10},{8,0} in; n < min(5,5) resets the global timer; the in-view gate 10 s and fade flag; picker age >= 10 s, eligibility, visible bit for out-of-view only, farthest strictly greater, cap 20. Last-vehicle protection and team filter are left to the caller (labelled); caller-side wiring was not verified here.
- R3F-2 WS-3 FIXED (elapsed_ = 0.48 * Rand01() * d). WS-5 FIXED (S(t) = 2t^2, then 1 - 2(1-t)^2).
- R3F-3 EC-2 FIXED (fastFlag cleared after the single retry). JC-2 FIXED (blocked = "climbing or vaulting"; airborne kept as a labelled CHOSEN guard). JC-5 PARTIAL: toggle wired with a CHOSEN key X, labelled; no spec binding exists.
- R3F-4 VC-5 PARTIAL. Labelled NOT-YET (0.9 * Max_FOV cap when 0x00ad3290 holds, the +/-30 deg rule, RC base only); still not implemented.
- R3F-5 AF-6 NOT-YET (not DEVIATION): flag held true for every NPC and labelled CHOSEN; the 2026-10-19 answer (0x00985cd0 = crouched) is not applied. Fix: feed the NPC crouched state. PV-7 PARTIAL: CHOSEN label added. The respawn resets still precede the 2026-10-19 respawn steps (NOT-YET).
- R3F-6 TF-7 and RP-6: label and wiring only in this diff; not verified beyond the commit text. Cash cap (overflowCashRaw still dropped) is NOT-YET against the 2026-10-19 cap answer.

### Activity e2e (873d5f6)
- AE-1 INVENTED-labelled, acceptable. Start point = Warp_Location navpoint within 3.0 m: marked CHOSEN, spec 1 "not traced" so no conflict.
- AE-2 CORRECT/labelled. The harness (StepToward, FraudStrikeHarness: 1500 kg, 15 m/s, 1.5 s, 12 m) is declared test-only CHOSEN, not game behaviour.
- AE-3 CORRECT. The Cash_Instead empty flag element read matches 6.2 (Airtime, Linear_Dist, Windshield). Hostage recruit/handover is CHOSEN-labelled (join rule unspecified).
- AE-4 NOT-YET. 1074bf4 (Genki combo 6.9.1, GK-5) was not reviewed in depth; recheck against GK-4's floor and multiplied target cash.

## Review of Team B 1074bf4 (merge b845f22): Genki combo rule (2026-10-19)

- GC-1 CORRECT: the refill per kill is trunc(trunc(Base x F) x 4), capped at Max_combo_ticks.
- GC-2 CORRECT: the per-frame decay is trunc(trunc(Base x F) x dt), and the combo ends when the ticks reach 0.
- GC-3 CORRECT: each score is paid at the multiplier in force before its own count step.
- GC-4 CORRECT: the decay level steps up when the bank passes the next threshold.
- GC-5 CORRECT: the Unethical target deducts cash and time, with the run total floored at 0.
- No deviations.

## Review of Team B b845f22..HEAD (vitals 18.10+, cash cap, GPS, homies, prop placement) (2026-10-19)

Scope: 653be05, bf65c33, ab7779c, ad09719 and the prop placement merge (c2a3696, decode in 5c30e4e); HANDOFF and comment-only commits skipped. Read-only review against spec-player-runtime.md 18.10-18.18 (and the R3 note in 18.6), spec-ai-runtime.md 23 and 24, spec-tables-progression.md (cash cap), spec-world-streaming.md 10.7.1. Nothing was built or run.

### Vitals 18.10-18.18 (653be05)
- V10-1 CORRECT: the bust gates (police team, arrest lock clear, ragdolled, alive, HP/maxHP <= 0.6 with maxHP <= 0 failing, not in action 7/9, not blocked, squared approach distance <= 25.0) match 18.10. Gates 2 and 8 are merged into one flag and the ragdoll gate is a CHOSEN stand-in for an on-foot living player (labelled). The "ragdoll test skipped in cop sub-state 0x42" nuance is not ported (no cop sub-states). Bust is opt-in (--enable-bust), which fits the 18.10 port rule "do not implement arrest".
- V10-2 CORRECT: the cuff ramp is +1/30/1.5 per tick (45 ticks to 1.0), commit at phase >= 0.75 (the clip phase is proxied by the ramp, CHOSEN and labelled); PlayerVitals::bust arms the same 4000 ms death timer, sets the busted flag, counts stat 11 once, leaves HP alone, and does nothing if already dead (vectors 12, 21). Slot 0xa fires nothing.
- V10-3 CORRECT: the player_start point (1220.9398, 12.1574, 1047.1863), quaternion (0, -0.9827, 0, 0.185) is the fallback when no point qualifies; a bust passes flag 2 and so lands there; bail uses Busted_cost 500, stat 5 and hint 13 (vector 13). NOT-YET: the prison / power-plant hood rule (vector 14) because the island argument is always -1 (comment says "not modelled").
- V10-4 CORRECT: PlanFadeIn gives the wipe 833 ms from max(load, T0) when loaded by T0+1000, otherwise logo at T0+1000 and the wipe from max(load, logo+1000) (vectors 15 and 16 are asserted in the test: 8300..9133 and 10000..10833); the old 250 ms CHOSEN is retired. NOT-YET: the viewer only logs the window (no overlay, no camera-mode-0 snap) and treats the destination as loaded at once (CHOSEN, labelled).
- V10-5 CORRECT: the full-restore timer is 500 ms, blocks all damage while armed (src/lua_engine_callbacks.cpp:314 and the target-side check), and sets HP := maxHP on expiry (vector 17). NOT-YET: armPlayerFullRestore() has no caller, because the ambulance marker / phone service object is not ported.
- V10-6 CORRECT: Player_Health_Bonus is base = max - bonus; bonus' = trunc(base * (M - 1)); max += bonus' - bonus; HP clamped, never healed; tiers replace (vectors 18 and 19, 2339 asserted). NOT-YET: no purchase or unlock path (only --debug-health-tier), and the +0x1cb4 level is not stored. DEVIATION (18.14, src/lua_spec_confirmed_stubs_b11_batch4_flags_combat.cpp:560): set_max_hit_points still stores the raw argument; the spec's sibling sets maxHP = bonus + argument. Fix: for the local player use playerMaxHpBonus_ + argument. (Body read only to the vehicle branch; the character branch was not seen.)
- V10-7 CORRECT: on_respawn registers only for local-player names (slot 5), passes one string (the script name), and is gated on a registration (vector 20). NOT-YET: only the free-roam respawn fires it; the four other revive callers (mission start with a dead player, checkpoint loader, two activity routines) do not. Low: the hook is fired at point-pick time (tools/sr3_viewer/viewer_playtest_frame_vitals.cpp:247), before the HP, notoriety and flag resets, whereas the real hook is step 18; harmless while callbacks are queued.
- V10-8 NOT-YET: ResolveMissionDeath matches vectors 22 and 23 (Retry = checkpoint revive only, no penalty or hospital; Quit = instant wipe plus hospital plus penalty unless phase 2 already ran) and kMissionWipeToBlackMs = 1033 is right, but nothing calls them (no mission-death wiring, no 2000 ms fail-screen debounce, no phase-1 hold).
- V10-9 NOT-YET (carried from R3F-5): the 18 ordered respawn reset steps and busted = +0x1d94 bit 0x02 from the 2026-10-19 R3 note are not applied in this diff; the revive still uses the old reset set (crouch clear, vehicle/seat exit, slot 0 are absent). Co-op offset (CoopRespawnPosition: 1.5 m along row 0 of the standard quaternion matrix, negated on the non-host) is CHOSEN and labelled.

### Cash cap (bf65c33)
- CC-1 CORRECT: tools/sr3_viewer/viewer_playtest_activity.cpp:164 does clamp(cash + grant, 0, 2,000,000,000) in cents = $20,000,000, overflow lost, floor 0, as the port rule in spec-tables-progression.md.
- CC-2 NOT-YET: only the activity grant saturates. The printed running total `cash` is unclamped (cosmetic), and no other cash adder (progression overflowCashRaw wallet, mission pay, pickups) was touched in this range.

### GPS route planner (ab7779c)
- GPS-1 CORRECT: A* with f = g + 0.5 * 3D distance from the lane end node B to the goal; successor cost = chord/256 (0 for bit 31); highway successor x 0.5; lane change 6.0 m (3.0 when the target is highway); no lane change from a lane reached by a lane change; oncoming left neighbour only in GPS mode; include mask 0x08062D00, exclude bit 24 unless start or goal lane has it; bit-15 shortcuts need bit 27 (all locked by default). Vectors TV1-TV3 hold by inspection.
- GPS-2 CORRECT: the pin clears at squared XZ distance < 15 from the vehicle (else character) position, pin kind only; a mission waypoint is never cleared by arrival; clear applies only when the active kind matches; clearing a pin restores the saved mission goal; last request wins; same-kind request within 1.0 m is ignored; a kind-1 commit saves its goal (TV4, TV5, TV9, TV10).
- GPS-3 CORRECT: replan at squared distance > 625 behind the 3000 ms timer, goal moved > 225 (tracked mission targets), > 10000 behind the 2500 ms timer, both timers re-armed at commit, direct mode retries the road plan every 3 s after landing (TV6-TV8, TV11).
- GPS-4 DEVIATION (23.5 step 5, labelled C6, include/sr3airuntime/gps_route.h:386): when seated in a vehicle that is neither a watercraft nor passes the unidentified test, the spec's far-off branch does nothing; the port falls through to the 25 m / 3 s branch and replans. Fix: skip the frame when seated in a non-boat vehicle and D^2 > 10000, once that test is identified (OPEN).
- GPS-5 NOT-YET (labelled C1-C3, C5, C7): planner states collapsed into one synchronous plan(); candidate ring law and state-9 trimming not ported (40/80/160/250 m widening, 8 candidates); island ids are 1 for all land lanes (no hood_island_type) and the island step rule exempts island-0 lanes (`ci != 0`, line 218), which the spec text does not state; goal not moved onto a lane. Minor label mismatch: the C2 note says "route cost = A* g" but Plan() adds the start-to-lane distance to pick the best start candidate.
- GPS-6 NOT-YET: the viewer sets mover to Ground or OnFoot only, so aircraft direct mode and boat routes are never exercised in play (the planner itself implements the straight line); no pause-map set-GPS toggle or the >= 4 lanes within 5000 m validity check (only a debug waypoint with Y = 0); Lua waypoint_add mode bit 2 (net) ignored, bit 1 applied; tracked goals only for characters.

### Homies and followers (ad09719)
- HO-1 CORRECT: cap 5 for an NPC leader; 0 if not recruitable and allow-max clear; base 1 in a multi-player session else 3; 2 in modes 11/12; min with the in-order unlocked slot count when allow-max is clear (vectors 1-3).
- HO-2 CORRECT: drop score (1 - 1/HP) + (1 - 1/(k+1)) + persona int + 5 + 10 + 20, fixed 100 for never-taken, force-only for the two bits, lowest below 100 (HP clamped at 1 is CHOSEN, labelled). The viewer feeds simplified candidates (weapon count 1, persona 0, homie-bound true) without a label.
- HO-3 DEVIATION (24.5 step 1, 24.11 step 2, tools/sr3_viewer/viewer_playtest_frame_homies.cpp:348): `service` (notoriety-clear rows and turn-to-ped rows) skips the cap check and drop, but the spec runs the hang-up cap check before every summon. Fix: run the cap/drop for all rows; a full party then blocks a service call.
- HO-4 CORRECT: cooldown stamp = clock + 1 game day (+ 1 hour for a player-selects-vehicle row); status 6, with 0 or 7 becoming 0; released once the clock is strictly past the stamp. Caveat: kDaySeconds = 86400 assumes the game clock counts real-scale seconds per game day; check against the game's day-length unit before relying on vector 6.
- HO-5 CORRECT: spawn search order (ahead, two perpendiculars by a 0.5 coin using cross(heading, up), behind) x 200/150/100 m, heading = velocity above 5 m/s else facing, reject within 95 m of a player, road node required (vector 4). The 60 m lane snap radius and "failed spawn leaves the record available" are CHOSEN (labelled).
- HO-6 CORRECT: approach speed 10 with no vehicle, else min(top, 35), target-car speed clamped to [20, 35] when following ahead (vector 5). Top speed 40 and the 15 m / 2 m/s arrival are CHOSEN (the spec's own stand-in); the moving-car arrival tolerance is CHOSEN and labelled.
- HO-7 CORRECT: follow radius 6 m, gait = max(player gait, 1) + (d > 7.5) + (d > 15) capped at 3, squadmate rule (2 m, cos 45 degrees, 1500 ms), teleport 100 m / 150 m in a vehicle with > 4 m height (vectors 7, 11). Gait speeds 1.6 / 4.0 / 6.5 m/s, the 200 m unstreamed stand-in, 25 m regroup, 60 m enemy range are CHOSEN and labelled.
- HO-8 CORRECT: friendly-fire meter adds min(dmg, 150) (150 for dmg <= 0), re-arms 6000 ms, zeroes at expiry, hostile while the timer runs and sum >= 150 (vector 8). NOT-YET: the never_turn_on_player / team-relation enable gate; the viewer drives it from debug hits, not real player damage.
- HO-9 CORRECT: abandon 55 m x 1.2 in a vehicle the follower is not in, 20000 ms, can_leave_behind only (default false is OPEN, labelled); hospital bill uniform $150-$250 as cents (vector 12); downed 30000 ms with a 2.5 s revive hold. The revive-timer multiplier is fixed at 1.0 (CHOSEN, normal difficulty).
- HO-10 NOT-YET (labelled): per-row unlock flags (all base rows start available, DLC rows not loaded), garage-pick vehicle, repair-vehicle step, roster save/load, single-dismiss target ("first joined" is the GUESS), Homie_Health_Bonus (base HP 100), and notoriety-clear rows use the existing notoriety seam only.

### Prop placement (c2a3696 / 5c30e4e)
- PP-1 CORRECT: +12 is a byte offset into the pool at file +0x20 of length u32(file+4); the offset must be 0 or follow a NUL, the string must end in .lmeshx, the mesh name is the stem, matched case-insensitively against <mesh>.clmesh_pc.
- PP-2 CORRECT: pos = origin(SR3Z +0x0C) + s16/64; (a, b, c) = (+6, +8, +10) * 2^-12 with +10 the yaw about +Y; the matrix rows r0, r1, r2 are exactly the spec's; transform is pos + x*r0 + y*r1 + z*r2; the flat 16-float world array (rows as consecutive 4-groups, position last) is the same matrix in either storage convention. The test covers tile 1018 #0-#2 and tile 0915 #3/#4 to 1e-3 m, identity, the yaw-only formula and orthonormality. Vectors 1018 #16/#18/#22 are checked for orthonormality only, not for position.
- PP-3 CORRECT: a record belongs to a fine cell when its transformed 8-corner box overlaps the cell box in X and Z (half extents 80 x 70 m), Y ignored, as in 10.5; one instance per record, shared renderers, collision built from transformed triangles.
- PP-4 NOT-YET (labelled in comments): meshes no record places (skyline appends) are skipped, so they no longer render in placed mode; the tile-level ~l1 meshes are not used; tiles without a ~f header, or SR3_PROP_GRID=1, fall back to the old CHOSEN grid (y = 0).

## Review of Team B 3e43057 (air/water/freefall) + 2527aa7/904d533 (R4 fixes) (2026-10-19)

Scope: read-only, `git show` of 3e43057 (merge 9f3fe12), 2527aa7 (merge 1073c45) and 904d533 in the Team B repo, against spec-physics-runtime.md 10.13.0-10.13.8, spec-player-runtime.md 18.6 (R3 note) and 19.1-19.6, spec-ai-runtime.md 23.2/23.6 and spec-tables-progression.md (respect overflow, cash cap). Nothing was built or run; unit-test expectations were not executed. Files: include/sr3physics/aircraft_watercraft.h (AWH), include/sr3player/freefall.h (FFH).

### Air, water, freefall (3e43057)

- AW-1 CORRECT: altitude ceiling (AWH ApplyAltitudeCeiling). 1333 m, 6000 ms ramp, t reset below the ceiling, k = min(1, 1000*t/ramp), vy *= 1-k, then g*k*dt. Note: the gravity term is added only inside the vy > 0 branch; the spec sentence (10.13.1) is ambiguous on that, so it is left as is.
- AW-2 CORRECT: helicopter collective (HeliCollective). v_t formula, limiter with decel Elevation_Accel^2/(2*Elevation_Damp), a = a_lim + 9.8 when (not settled or c > 0) and rotor healthy and driver/remote, else a_lim + 4.9. Settled-timer rules (800 ms re-arm, 499 ms cut at U.y < 0.766) match.
- AW-3 CORRECT: twelve ring forces (HeliRotorForces). phi_i = 2*pi*i/12, w_i = 1 - 2*delta/pi, g = cos(e)^4 for e <= 0.785385, k_i zeroed while settled, F_i = M*(a/12 + k_i)*D at each point, D = (0, cos tilt, sin tilt). Tail, extra-speed (0.70711 cap), drag, auto-level (sqrt(2*theta/pi)) and the (1.0, 5.0)/(0.4, 0.05) damping pairs match. NOT-YET (small): auto-level x5 while timer +0x9a0 runs, rotor wash (gains unread in the spec, declared).
- AW-4 DEVIATION (10.13.2 step 11, 10.13.3 step 8, 10.13.5, 10.13.8 item 3; AWH:363, 521, 680): no step clamps speed to Maximum_Speed (10.11.10). HeliParams.maximumSpeed is stored but never used by HeliStep; PlaneStep and BoatStep have no clamp at all. Fix: apply the 10.11.10 clamp at the end of HeliStep, PlaneStep and BoatStep (boat adds controller +0x7c while the boost bit is set).
- AW-5 CORRECT: plane engine, lift and stall (PlaneEngineStep, PlaneLiftFraction, PlaneStep). Ramp Engine_Accel/ramp time, reverse floor, force M*a at every engine point, L = clamp(u/Lift_Min_Speed)^2, vy += W*L*9.8*dt (below Lift_Min_Speed the plane sinks), bank target pi/3 / knife edge / inverted, yaw q*W*L*Horizontal_Max, pitch (1 - 0.5*b), slip turn 600*sin(slip) all match P1-P8. Choices Q-A3/Q-A4 are labelled.
- AW-6 DEVIATION (10.13.3 step 4, P9; AWH:489): the barrel roll uses +Barrel_Roll_Max_Angular_Speed with no sign for the left/right direction (bytes +0x14c/+0x14d), and has no blend-back or 0.349 rad end rule (declared as Q-A10). Fix: sign the rate by the remembered turn direction and end when within 0.349 rad of the target bank.
- AW-7 CORRECT: VTOL (VtolApplyRequest, VtolJetKickAccel, VtolKickStep, VtolMachineStep). Hover to jet sets +0x9b0 = 0 and arms 1200 ms; k = 50 m/s^2 for u <= 290, (300-u)/10*50 above, none at u >= 300; impulse M*k*dt along forward (V3 = 60 m/s, V4 = 25); jet to hover is immediate; camera re-base 2000-e; rotor always healthy. Small gap: the toggle should be ignored while vehicle +0xf8 bit 1 is set.
- AW-8 CORRECT: boat thrust (BoatThrustStateStep, BoatStep). Thrust state ramps at the up/down rates and resets on sign disagreement; force M*(fwd or rev accel)*state*D only when the prop point is under water; LBS radius v^2/(9.8*tan(lean)) with the pitch modifier and Min_Radius floor (B4-B6 reproduce); coasting steer 5*clamp(|v|/Max_Speed)*steer (B8). Thrust geometry is CHOSEN (Q-A7, spec residual).
- AW-9 DEVIATION (10.13.5 step 3; AWH:680-700): BoatStep never runs the spin damping (0x00ad9500) that the spec runs after both the water and air branches. Fix: call ApplySpinDamping at the end of BoatStep with the Collision/Normal_Spin_Damping values.
- AW-10 DEVIATION (10.13.5 Thrust; AWH:690): with Lean_Based_Steering and lean target = 0 (steer centred) the code falls back to a force at the thrust point P. The spec applies the LBS force at the centre of mass and only skips the yaw-rate step. Fix: test only `p.lbs` for ForceCom, and skip BoatLbsYawRate when the lean target is 0.
- AW-11 CORRECT: buoyancy (BuoyancyForce). F = (0, -1000*V*(-9.8)*Buoyancy_Modifier, 0) at the centre of buoyancy; rest volume mass/(1000*k) = 10.75 m^3 for boat_speed01 (W1). The slab volume and the v*(1-c*dt) drag are labelled CHOSEN/GUESS as 10.13.8 item 2 allows.
- AW-12 CORRECT: freefall constants and start (FFH Constants, CanStartFreefall, StartFreefall). T 45/67/36 and 9/11.5/6.7, 30/3/20, 0.6, 0.75, 2/8/20, 1.0, accel limit 10, lerp 2.0, 1500 ms, 10 m clear, 10 m/s start speed, 8/-24 FOV, 44.704 default cap. Start requires fall speed > 10 and no ground within 10 m.
- AW-13 CORRECT: chute button and locks (PressActionButton, MayDeploy, Deploy, CutAway). Open with the lock running does nothing; otherwise cut away; deploy and cut-away both re-arm 1500 ms; deploy starts freefall without a lock; chute timer 1000 ms. Vectors 9 and 10 (re-deploy from 3.1 s) hold.
- AW-14 CORRECT: movement law (UpdateTerminalSpeed, UpdateMovement, StepVertical). b blend (full dive at B > 0.2 in freefall), T = N + (Fw-N)*b or N + (Bk-N)*(-b), D formulas, lock-limited chute speed 8, accel limit 10*dt, bank follow 0.75, turn angle; vertical g*(1-(sink/T)^2) gives +235.2 at sink 45, T 9. NOT-YET: the rig does not take T (CharacterProxyDriver), no turn-rate follow (spec GUESS), no +0x20f8 state restore.
- AW-15 NOT-YET (10.13.8 items 1-3): tools/sr3_viewer/viewer_craft_wiring.h is compile-only. Heli, plane, VTOL and boat laws are not used by the vehicle step; freefall is only a read-only trace (SR3_TRACE_FREEFALL), and the probe treats "falling state" as always true. The header says so; nothing is claimed beyond that.

### R4 fixes (2527aa7, 904d533)

- R4F-1 HO-3 FIXED (viewer_playtest_frame_homies.cpp): the cap check and drop now run for every row, service rows included.
- R4F-2 V10-9 PARTIAL (include/sr3player/vitals.h RespawnResetOrder; viewer_playtest_frame_vitals.cpp about 278-345): the 18 steps are listed in the spec order and applied in that order, with seat exit (9) and crouch clear (10) before the teleport (12), notoriety (14), controls on (15), busted clear (16) and latch plus on_respawn hook last (18). The busted flag is set at the bust commit (+0x1d94 bit 0x02, kBusted = 0x02) and cleared at step 16. Gaps: (a) steps 2, 3, 7, 8, 11, 13 (slot 0) and 17 are logged only; step 4's second pair (+0x1324 := +0x1320) and +0x1d99 bit 0x40, and step 16's +0x1598 := 0 are not modelled; (b) the respawn flags still come from v.busted() (line 243), not from reading +0x1d94 bit 0x02, so the stored bit is write-only; fix: pass `(b1d94 & kBusted) ? 2 : 1`; (c) INVENTED, harmless: CrouchClear (line 314) also clears the crouch-toggle latch (+0x1d96 bit 0x10), which step 10 does not list; remove.
- R4F-3 CC-2 FIXED for the adders reviewed: awardRespect now pays overflowCashRaw (already x100, cents) through AddCashSaturating (clamp +-2e9 then floor 0); cash_add and the crib cash stash use it; the activity grant pays both respect overflows and Cash_Award through the same cap (904d533, merged at HEAD). Residual: debits (`playerCash() -= price`, about a dozen sites) are not floored at 0 as the setter does, and the non-real-table awardRespect branch is untouched.
- R4F-4 V10-6 FIXED (lua_spec_confirmed_stubs_b11_batch4_flags_combat.cpp:584): set_max_hit_points adds playerMaxHpBonus_ for the local player and overwrites plainly for others, as 13.29 #7 (0x0094d1f0) says.
- R4F-5 GPS-6 PARTIAL (gps_route.h MoverForVehicleClass; viewer_playtest_frame_gps.cpp:25): class 2 gives Airplane, 3/4 Helicopter (direct mode), 5 Boat, other Ground, not seated OnFoot, matching 23.6. Gaps: 23.2 step 1 uses the "current or last vehicle" while the code switches only while seated; the pause-map toggle, the >= 4 lanes within 5000 m validity check and Lua waypoint_add bit 2 are still open; the rig only drives ground vehicles, so the air and boat modes stay unexercised in play.

## Review of Team B 9fb9a82 (R5 fixes) + 522f78e (air/water rig) (2026-10-19)

Scope: read-only; `git show` of src, include, tools, tests. Nothing built or run. Specs: spec-physics-runtime.md 10.13.x, spec-player-runtime.md 19. Abbreviations: AWH = include/sr3physics/aircraft_watercraft.h; RIG = tools/sr3_viewer/viewer_craft_rig.h; CRF = tools/sr3_viewer/viewer_playtest_craft.cpp; FF = include/sr3player/freefall.h.

### Part 1: 9fb9a82 (merge 4d79a29), status of earlier findings

- R5F-1 AW-4 (speed clamp) FIXED. AWH ApplyMaximumSpeedClamp rescales all three components to cap + bonus; called at the end of HeliStep (line 373, 31.29 m/s default), PlaneStep (548) and BoatStep (734, boost bonus from +0x7c). Caveats: PlaneParams.maximumSpeed defaults to 0 = no clamp (CHOSEN) and RIG never sets it, so planes are clamped only by the rig's own post-gravity clamp (RIG:414); the boat bonus meaning stays OPEN and the boost bit is never driven.
- R5F-2 AW-6 (signed barrel roll) PARTIAL. AWH:498-517 now signs the rate by `barrelDir`, runs pi/rate seconds, blends back below pi/2 error and ends under 0.349 rad (P9); the blend curve is CHOSEN. Gap: nothing in RIG or the input code calls PlaneBarrelRollStart (no CBA_VDC_PLANE_ROLL_TURN_SELECT 0x4b key, no remembered direction), so the law is unreachable in play. Fix: bind a key and pass the signed direction.
- R5F-3 AW-9 (boat spin damping) FIXED in the law. AWH:733 calls ApplySpinDamping after both branches (step 3). The three damping values default to 0 with threshold 1e9 and RIG never fills them from the vehicle entry (+0x848..+0x850), so it is a no-op in play. Fix: load them from the row.
- R5F-4 AW-10 (LBS at lean 0) FIXED. AWH:719-726: ForceCom whenever `p.lbs`; the yaw-rate step is skipped only at lean target 0, as in 10.13.5.
- R5F-5 R4F-2 PARTIAL. Respawn flags now come from the stored +0x1d94 bit 0x02 (viewer_playtest_frame_vitals.cpp:243) and the invented crouch-latch clear is gone. Still open: steps 2, 3, 7, 8, 11, 13, 17 log-only; step 4's second pair, +0x1d99 bit 0x40 and step 16's +0x1598 := 0 not modelled.
- R5F-6 R4F-3 FIXED. awardRespect's non-real-table cap branch now pays 1:1 dollars x100 through AddCashSaturating (engine_state_progression.cpp:611). All the `playerCash() -=` debit sites go through the new debitPlayerCash (clamp +-2e9, floor 0); a grep finds no remaining direct subtraction.
- R5F-7 R4F-5 PARTIAL. MoverForPlayer (gps_route.h:89) implements "current or last vehicle" for classes 2/3/4 (23.2 step 1) and viewer_playtest_frame_gps.cpp feeds it (the merge uses the flown craft's class). Waypoint mode bit 2 is only logged (no network layer: acceptable). Still open: pause-map toggle and the >= 4 lanes within 5000 m validity check.
- R5F-8 Vitals log newline FIXED. All trace buffers are now 512 bytes (the death line is about 215 chars, was 200).

### Part 2: 522f78e (merge 83b0883), air/water rig and freefall

Step order (10.13.1): each law runs in RIG SubStep through HeliStep / PlaneStep / BoatStep, which own the order; VTOL runs the machine, request and kick before the model (step 5 before 6-8). Gravity (-9.8) and a speed clamp run after the law, integration after that. The substep count is ceil(dt*60) (CHOSEN).

- AR-1 Heli inputs CORRECT. RIG HeliLaw: collective = up - down; cyclic from X = -LEFT_RIGHT, Y = -FORWARD_BACKWARD (10.13.2 table); yaw from keys; Space turns A/D into yaw; keys follow 18.3 (W/S, D/A, L-Shift, L-Ctrl, Space, F). Q/C yaw is CHOSEN and labelled.
- AR-2 Plane inputs CORRECT (pitch sign HIGH only). Roll/pitch go through PlaneCircleToSquare (X', Y' remap), rudder = clamp(-turn), throttle = accelerate - brake. The key-to-sign mapping of pitch rests on a test comment (spec: "S/W inverted pair").
- AR-3 Barrel roll NOT-YET (see R5F-2).
- AR-4 Plane ground step DEVIATION (10.13.1 step 2, 10.13.3). The wheeled step 0x00ad97c0 is replaced by an invented ground model (lateral kill, rolling drag, nose-wheel yaw, upright levelling; the RIG header says so). The partial-ground-contact engine bound u/13.4112 is also unmodelled. Fix: route plane ground contact through the existing wheeled step.
- AR-5 Boat input DEVIATION (10.13.5 Player inputs). BoatInput.pitch = clamp(-fwd, 0, 1) (RIG:392) keeps only the braking half; spec is brake - accelerate in [-1, 1]. Fix: drop the 0 lower clamp. Stop-assist is hard-wired false (no spec source, CHOSEN).
- AR-6 Boat water INVENTED / PARTIAL. 10.13.6 is PARTIAL in the spec: flat water plane at 0.0 m, hull cells, drag v *= (1 - c dt) (10.13.8 item 2, a GUESS), plus an invented keel side-slip kill, angular damping and heave damping (RIG:398-405) and a 1.2 m hull-height cap. All carry CHOSEN comments but no spec section.
- AR-7 Speed cap DEVIATION (10.11.10). RIG:248 uses the row Maximum_Speed else 44.704 m/s (not a 10.11.10 value); the clamp runs after gravity, so it also limits a diving plane's vertical speed. Fix: take the cap only from the row / entry and skip it when absent.
- AR-8 Altitude ceiling CORRECT. Applied inside each Step (1333 m, 6000 ms, 10.13.1 step 9).
- AR-9 Craft class selection CORRECT. KindForVehicleClass maps 2/3/4/5 (Vehicle_Type); other classes fall back to the ground rig; the VTOL starts in hover (+0x9b0 = 1). The GPS mover uses the craft's class.
- AR-10 Enter / exit INVENTED (CHOSEN, labelled). Entry is the existing 3 m E radius. Exit keeps the craft's altitude (an air exit falls), the empty craft is set down at once, a boat floats 0.3 m below the plane. Crash damage (10.13.7) and rotor wash are not wired.
- AR-11 Freefall start CORRECT with one gap (19.4 step 1). CanStartFreefall tests fall speed > 10, locomotion 6, cc8 != 2, the 10 m downward ray (clearance <= 10 m counts as a hit), then T = 45, b = bank = 0, r = velocity, lock = 1500 ms. Gap: fall speed is a position difference over dt (one-frame lag, CHOSEN) and locomotion is hard-wired to 6 whenever the player is not grounded, so ladders/ragdoll/etc. never block it. Test vector 11 (9 m/s, 8 m) passes.
- AR-12 Deploy / cut-away DEVIATION (19.4 step 2). Toggle in freefall is right: chute open + lock running does nothing, open + lock expired cuts away (lock re-armed 1500 ms, chute timer 1000 ms, T back to the freefall pair); closed deploys through MayDeploy with a re-armed lock. Deviations: (a) `cc8IsTwoOrSkydive` defaults to true, so the "+0xcc8 = 2 or mission skydiving" requirement is never evaluated; (b) CRF:408 deploys while falling but before the start test passes with only `fall > 0`, bypassing MayDeploy (alive, lock, cc8) and starting freefall with no lock; the spec's fall > 0 test applies only on a moving support, and the 3 m/s "falling" threshold in viewer_playtest_frame_input.cpp:359 is INVENTED. Fix: always go through MayDeploy and take cc8 from the character state.
- AR-13 Movement law mostly CORRECT (19.5). b and T per step 1 (B > 0.2 full dive only when the chute is closed), D = B*F*(30 or 3) + A*Rt*20 (freefall), s = 8 / 8+(20-8)B / 8+(2-8)(-B) with the lock holding s = 8 (chute), accel limit 10*dt, bank rate 0.75, FOV offset and the ground-near early return all match; FF UpdateMovement reproduces vectors 1-7. DEVIATION: under the chute the facing passed in is the direction of r once speed > 0.1 (CRF:469), so D just continues the current velocity; spec step 5 says the path follows the camera heading offset by the bank. The turn angle is computed but never applied, so steering under the chute has no effect. Fix: turn the facing toward camera heading + bank each step (rate stays a GUESS).
- AR-14 Vertical law CORRECT. CharacterProxyDriver::setTerminalFallSpeed(T) feeds the 5.6.5 law each frame (gravity x (1 - (sink/T)^2)) and resets to 44.704 when the override is off; the end sets T = 44.704.
- AR-15 Landing / end PARTIAL. CheckEnd ends on the grounded flag and starts the chute fade (1000 ms); the "airborne > 1 s with fall speed <= 0.01" alternative and +0xec bit 0x100 are not fed. Landing animation, chute object, wind sounds and blur are NOT-YET (cosmetic).

## Answers to Team B 7436a78 questions, batch A (2026-10-19)

Static disassembly of the shipped exe plus the shipped tables. Team B's code was read only to understand Q8.

- Q8 car-rig heading convention: YES, a real mirror wherever a (cos θ, sin θ) sim heading reaches the engine-convention yaw builder (mesh +Z → (sin yaw, cos yaw)) without conversion. Engine: Y up, nose +Z, h = atan2(fwd.x, fwd.z), forward (sin h, 0, cos h), right (cos h, 0, −sin h), +h = right turn (clockwise from above); convert with h = π/2 − θ. Test vectors: h 0 → (0,0,1), +90° → (1,0,0). spec-physics-runtime.md §10.15 (plus a pointer note in spec-world-streaming.md §10.7.1).
- R5 (a) log-only respawn steps 2/3/7/8/11/13/17: unhide; death overlay off + `Player_Respawned` + HUD Lua resets; anim override −1, climb abort, end anims, sober up; intoxication / hit-reaction / ragdoll-rest latches and the intoxication timer cleared; animation set and gait re-seated, action stopped, state 0/0, combat-ready off; step 13's lookup is a no-op plus help-text close; three owned objects deleted. spec-player-runtime.md §18.6 (R5 note).
- R5 (b) `+0x14c` = RIGHT and `+0x14d` = LEFT barrel roll (the old text had them reversed); request = double tap within 500 ms; blend-back after π/rate s: below e = π/2, ω = Roll + (Barrel − Roll)·clamp((e − 0.349)/1.222, 0, 1), end below 0.349 rad; right roll negates ω. spec-physics-runtime.md §10.13.3 (R5 note).
- R5 (c) plane `Maximum_Speed`: descriptor `+0x878` from the `_veh.xtbl` element (mph × 0.44704, cap 44.704 m/s), default the Airplane group row 80 mph = 35.763 m/s; plane_standard01 37.998 m/s; same 3-D clamp `0x00a86620`. spec-physics-runtime.md §10.13.3 (R5 note).
- Camera-relative heli byte `0x011807dc`: live, a constant 1. There are four reads, no writer and no pointer in the image, so no option or tweak can change it. spec-physics-runtime.md §10.13.2 (note under the input table).

## Spec corrections from Team D emulation (2026-10-19)

All FINAL (emulation of the original routines; Team D spec-differential-results.md D-1..D-21). Each site carries the marker "[2026-10-19 corrected per Team D emulation D-n: FINAL]".

- D-17 engine torque curve `0x00ae3330`: field names NOT crossed; T = Torque(1 + (MinTF-1)k^2d^2), R = Torque(OptRes + (MinRes-OptRes)kd), coasting rule. spec-physics-runtime.md §10.2 (+ two changelog strikes).
- D-18 RPM update `0x00ae31f0`: not up-only; transRpm >= target gives transRpm, else two-way ease. spec-physics-runtime.md §10.2.
- D-19 gear machine `0x00aece20`: no per-direction cooldowns, timer does not block, strict comparisons, downshift before upshift. spec-physics-runtime.md §10.3.
- D-20 limiter seed `0x00dae970`: a = s*seed while moving away; full algorithm. spec-physics-runtime.md §10.11.11 item 9 (and §10.11.7 PARTIAL note).
- 1a heading math FINAL with test values; 1b steering sign: the setter `0x00AE5290` negates its input, cancelling chassis right = (-1,0,0); "+h = right turn" is now FINAL for the stick/keyboard path (polar-remap branch untraced). spec-physics-runtime.md §10.15.
- 1c barrel roll: first binding slot = positive half (Key_Pos = D); +0x14c = RIGHT confirmed. spec-physics-runtime.md §10.13.3 (R5 note).
- D-9 shotgun pellet factor `0x00963920`: random draw rand(t^2, 1); table RNG `0x00dab6a0`, MT19937 seed 5489, reseeds. spec-weapons-runtime.md §4.5.3.
- D-10 base damage test order and inverted range. spec-weapons-runtime.md §4.5.3.
- D-11 / D-12 / D-13 mercy maxHP 0, regen wait truncation (7,799), single-precision regen ticks. spec-weapons-runtime.md §4.5.4 steps 10c and 19, §4.5.5.
- 1d New Game story flag `0x006D74B0` / wrapper `0x00707380` result table, free-roam route, press-start auto-accept `0x007AE9E0` conditions: FINAL (first-entry accept HIGH); starting cash has no code constant. spec-frontend-flow.md §4.3 and the Enter note of state 2.
- D-22 cash add `0x00960190` wraps in 32 bits then clamps and floors (not saturating); local-player amount scaled by the cash multiplier `0x012F5D00` (up to 1.35). spec-tables-progression.md B.1.1 (Team B R3 note) and §4.4 row 38.
- D-1 round_half_away(float32(product)) via `0x00DAD900`; D-4 32-bit sums. spec-tables-progression.md B.1.1 (and B.1.2 rows 4, 5, 13).
- D-2 diversion cash = floor(float32(f*Max_Cash)), threshold 0.001f. spec-tables-progression.md B.1.2 row 1 (and port step 4).
- D-3 toast `0x007FA320`: respect OR cash; cash only if > 0. spec-tables-progression.md B.1.2 row 1.
- D-8 negative Props_Award as unsigned. spec-tables-progression.md B.1.2 row 4.
- D-21 notoriety: Points <= 0 not skipped, floor keeps remainder, entry floor with fractional part. spec-ai-runtime.md §19.8 G3, G4, G5, decay roll-down.
- D-16 xtbl parser `0x00DC5820`: trims bytes 1..32, first run only, 5 entities, UTF-8 rule, rejection list; template.xtbl and xbox360_text.xtbl do not load. spec-xtbl-format.md §7; spec-tables-traffic-ai.md §1.2 (+ changelog); spec-tables-environment.md §1 node-layout note.
- D-14 float grammar `0x00DACB20`: no '.' ignores exponent, first '.' anywhere, signed int32 part, CRT atoi exponent, double multiply-add per digit, squaring in `0x007D9B00`. spec-tables-traffic-ai.md §1.3; spec-xtbl-format.md §8.
- D-15 integer grammar `0x00DAB8B0`: hex branch reads past the terminating NUL. spec-tables-weapons-combat.md §1; spec-tables-traffic-ai.md §1.3; spec-tables-environment.md §1.
- D-5 rotate-6 hash `0x00DA7890`: signed bytes, NULL gives 0xFFFFFFFF; `0x00DB09D0` same hash, NULL gives 0xFFFF. spec-vpp-container.md §2.2; spec-texture-format.md §10.6.
- Multiply-33 hash `0x00DAB330`: CRT tolower, unsigned byte, unsigned modulo. spec-texture-format.md §8.2.
- [2026-10-20 per Team D emulation: FINAL] Team B CHOSEN answers: phone `0x0083d150` (ring 30, connect 31, summon 32, close 91 updates later), homie car arrival `0x00b61990` (|v| < 1.0 strict), attitude `0x004fe0f0` / dispatch `0x00506c20` (matrix, gangs give 1 -> non-hostile handler), downed selector `0x009735d0` (brute bit 0, gang team <= 4 and relation <= 1; CORRECTION: Bleed_Out threshold belongs to `0x00972790`, not the selector). spec-cellphone-menu-runtime.md §16a.2; spec-ai-runtime.md §12.7, §21.10.1, §24.5.
- MOPP cross-check (tile 0915) [2026-10-20 per Team D emulation: FINAL]: blob header, key decode `0x00D55110`, ray (-1280,100,-560) -> key `0x82040601` y = 32.6763, 13,503 unique keys, max vertex 9420 all agree with our text; section CONFIRMED by emulation, no text change. spec-zone-data-format.md §13.5.3.
- D-24 weather time-of-day cell merge in `0x00BA8540`: base from curve sets or override record; from/to cells from one segment; out = f32(f32(F x f32(1-t)) + f32(T x t)); flag `0x012E6B78` cleared by the script weather override. The Residual line is closed. spec-tables-environment.md §19.1; spec-looks-like-the-game.md §3 item 3.
- D-25 suspension `0x00C59630`: out = f32(f32(spring - damping x v) x M), M = chassis mass (1/invMass); damping = compression if v < 0 else relaxation (+-0 = relaxation); record order {strength, compression, relaxation}. Wrong text corrected in place. spec-physics-runtime.md §10.4.
- §10.8 slip velocity `0x00C64252..0x00C64306`: a full Jacobian-row relative velocity (linear + both angular rows), not only a linear difference on two tangent rows. Corrected in place. spec-physics-runtime.md §10.8.
- SUS-1 (finding, Team B): Team B's suspension (cf. 33d1d2b) lacks the mass factor M and has the damper branch reversed (compression chosen on positive speed); fix per D-25: out = f32(f32(spring - damping x v) x M), compression if v < 0.

## Answers to Team B 7436a78 questions, batch B (2026-10-19)

Static only. The full text with test vectors is at each pointer.

- Water height source: FINAL. Bodies carry Havok property 0xd = {height, flags}.
  - Ocean: sea level = the top (centre y + half y) of the ocean-flagged (`+0x44` bit 0) `0x2247` volume with the largest x-z overlap. City sea level is 0.0 m: 676 of 681 ocean volumes have top 0.000.
  - Elsewhere: the top of the overlapped water-volume phantom.
  - Waves (HIGH): a bilinear grid inside the ocean simulation window; outside it, the ripple h = 0.05 sin(0.1232(x+P)) sin(0.3696(z+P)) + 0.05 sin(0.1698(x+P)) sin(0.2732(z+P)), P = 6 t_s.
  - `0x2249` is not read by the physics.
  - spec-physics-runtime.md §10.13.9 Q1 (W7-W10); spec-zone-data-format.md §13.3 (layouts of `0x2247`/`0x2249`, census).
- Buoyancy shape and drag law: FINAL except the face weight and the Jacobian step.
  - The shape is the body's own collision shape, clipped per vertex; there are no sample points and no hull boxes.
  - Per face: drag F = -175 A |c| u v_p (quadratic) and lift 75 A u^2 g(alpha) t_hat, with a 15-degree knee. The solve is implicit Newton, at most 4 iterations, tolerance 1e-4.
  - There are no Water_Linear/Angular_Drag_Modifier fields; they are not in the exe.
  - spec-physics-runtime.md §10.13.9 Q2 (W11-W13). Supersedes the §10.13.8 item 2 placeholder.
- Inertia tensor and pitch/roll limiter: FINAL for the tensor; derived tilts HIGH.
  - The tensor is I = Mass x diag(Pitch_UI, Yaw_UI, Roll_UI) for every class; boats re-apply it via `0x00aee580`.
  - No angle limiter exists. Heli steady tilt = (pi/2)(alpha_c/A)^2. Plane bank targets are pi/3, pi/2 and pi; plane pitch is rate-only.
  - spec-physics-runtime.md §10.13.9 Q3 (I1, I2).
- Wheeled plane ground step `0x00ad97c0`: FINAL. This addresses finding AR (Team B's invented taxi model).
  - The car accelerator is zeroed and the clutch is disengaged (`+0x160`), so the engine force is the only propulsion.
  - The full car wheel step runs: the roll stick steers the nose wheel (35 degrees), throttle -1 applies the wheel brakes (300 N m per wheel) plus engine reverse, and the handbrake acts on the rear wheels.
  - The velocity damper is off while the plane is not fully on its wheels (pilot assist on). Liftoff happens at u >= Lift_Min_Speed with the nose raised.
  - spec-physics-runtime.md §10.13.9 Q4 (G1-G4). OPEN: the airborne extra-gravity branch for the Extra_Gravity -20 jets.
- Watercraft §10.13.5 reader children: FINAL. 0x6C-byte block table with units and defaults (ramps and input rates default to FLT_MAX; audio defaults 3 / 0.2 / 1; Air_Control at `+0x28`; Spawn_Height_Offset at `+0x68`). Drift_Parameters is a Group-row reader, not a watercraft child. spec-physics-runtime.md §10.13.9 Q5 (X1-X3).
- Aircraft mesh group x part x LOD, and rotor transforms.
  - Mesh layout (HIGH): groups form one run of 1-3 LOD groups per sub-mesh. The LOD0 group bbox equals the part's `+0x60`/`+0x70` bbox, and runs are not in part order. Draw LOD0 only; group 0 is a small part, not the hull.
  - Rotor spin (FINAL): part type 25, rotated by -theta about the part's `+0xA0` axis; children inherit. One rotor state per vehicle (`+0x328` plane, `+0x5b8` heli): 31.4154 rad/s, ramping at 10.4718 rad/s^2 (3 s), started and stopped with the engine, independent of collective and throttle.
  - spec-vehicle-geometry.md §12.4 (R1-R4, M1-M2).

## Review of First Playable merges 604a471 + 3d5e140 + a5ed0b8 + 859fbf9 (2026-10-19)

Scope: read-only. For each merge, `git diff <m>^1 <m> -- src include tools tests`, plus `git show` / `git grep` at 3d5e140 and 97bad5c to find call sites. Nothing was built or run. Line numbers are at 3d5e140.

Specs used:
- spec-frontend-flow.md (2026-10-19)
- spec-tables-ui-controls.md §18
- spec-save-format.md A.1-A.3 and §8.4
- spec-player-runtime.md §18
- spec-differential-results.md D-1 to D-22 and the vector files

The `sr3` target links Team C's `sr3game_frontend_teamc` (CMakeLists.txt:4521). The 604a471 stand-in `src/game_frontend_default.cpp` is no longer linked. It always shows Continue and quits without asking, so delete it or keep it in step with Team C's menus.

Abbreviations:
- MAIN = tools/sr3_game/main.cpp
- FMM = src/frontend_menu_model.cpp
- FMS = src/frontend_menus.cpp
- FIN = tools/sr3_viewer/viewer_playtest_frame_input.cpp
- GS = src/game_saves.cpp
- DS = src/physics_vehicle_driving_slice.cpp
- SUB = src/physics_vehicle_substeps.cpp

Counts: FP 11 (3 CORRECT, 5 DEVIATION, 2 NOT-YET, 1 INVENTED); FE 11 (8 CORRECT, 2 DEVIATION, 1 NOT-YET); DV 11 (6 CORRECT, 3 DEVIATION, 2 NOT-YET).

### Part 1: 604a471 first-playable host (FP)

- FP-1 Boot: NOT-YET (frontend-flow §2.1-2.5, §10.1).
  - MAIN:232 and MAIN:413 only log "startup movies SKIPPED".
  - Missing: the logo movies (display.ini `SkipIntroVideo` is not read), the legal screen, the 5 s save warning and the GAME_LOADING n% screen.
  - The menu appears after the archives and render assets are loaded (hook `beforeWorld`). That order is acceptable.
  - Fix: play the two logos unless SkipIntroVideo is set, then show the legal screen, the save warning and the loading screen.
- FP-2 Pause open path: DEVIATION (frontend-flow §6; tables-ui-controls §18.1 correction).
  - FIN:105-127 opens the menu with `pushGamePauseReason(4)` and the Lua hook `dialog_pause_show`, and closes it with pop reason 4 and `dialog_pause_hide`. That chain drives the dialog.lua "PAUSED" overlay, not the pause menu.
  - The original pushes game mode 6 (an overlay above gameplay mode 5), opens screens 9 and 10 (`pause_menu_common`, `pause_menu_top`), and freezes with pause reason 0xD. The close method of `pause_menu_common` pops that reason.
  - include/sr3game/frontend.h:13 still documents the old chain.
  - Fix: push and pop reason 0xD, model mode 6 over mode 5, drop the dialog_pause_show/hide calls, and correct the header comment.
- FP-3 Pause gate: NOT-YET (frontend-flow §6, T18).
  - The menu opens only from the free-roam loop, which stands in for "top mode is 5".
  - The fade, cutscene and already-open checks are absent (the code says so).
  - Missing: the 500 ms re-press guard, and Tab opening the map (screen 25).
- FP-4 New Game straight into free roam: INVENTED (frontend-flow §4.3, T15).
  - MAIN:263 starts free roam at the player-start marker. There is no story gate and no CHOSEN label.
  - The original never starts New Game in free roam. Its gate (0x00707380, then 0x006D74B0) launches the opening mission until m01, mm_m1_5, m02 and m03 are all complete.
  - §4.3 allows a First Playable shortcut, but done its way. Mark the four missions complete (record +0x88 bit 2), or start with session flags that lack bits 0 and 2. Without that, a save made in this session is not a free-roam save for the original.
  - Fix: label it CHOSEN (§4.3) and mark the four opening missions complete in the new-game state and snapshot.
- FP-5 New-game state: CORRECT (§4.4, save-format A.3). Time 10:00:00 and $57.00 cash (the spec's OPEN recommendation, stated as such).
- FP-6 Save list ordering: DEVIATION (frontend-flow §5.1, save-format §8.4).
  - GS:45 and GS:62 sort by file modification time, then by path.
  - The original sorts by the save's own date-time key (§8.4): autosave first, readable saves newest first, unreadable saves last.
  - Effect: a copied or restored file lands in the wrong place, and Continue can pick the wrong save (Team C's SortSavesForLoad also uses the file time).
  - Fix: read the §8.4 key from the save and sort on it.
  - The autosave-first rule is NOT-YET: there is no autosave marker or writer yet (labelled in menu_model.h).
- FP-7 Save from pause: DEVIATION (frontend-flow §7.1-7.6, T19).
  - MAIN:301-304 writes the save at once, with no list.
  - The first save takes the next free `sr3save_NN`. Every later save overwrites that file without asking, and so does every save after a Load (MAIN:259 makes a loaded game save over its own file, labelled CHOSEN).
  - Missing: the `pause_save_game` list with `SAVELOAD_NEW_SAVE_GAME` (handle 99) first, the overwrite confirmation (`SAVELOAD_SAVE_GAME` / `SAVELOAD_SAVE_GAME_CONFIRM_EXPOSITION`), and the Saving dialog.
  - The CHOSEN label cannot remove a FINAL confirmation.
  - Fix: open a save list (the New row, then manual saves, autosave hidden) and confirm before an overwrite.
- FP-8 Load placement: DEVIATION, though labelled CHOSEN (save-format A.1/A.2, player-runtime §18).
  - viewer_playtest_saveflow.cpp:78-84 and writeSaveTo add a `.sr3pos` sidecar file that puts the player back at the exact saved position.
  - The original stores no position. A load places the player at the respawn crib or the player-start marker.
  - Fix: put the sidecar behind an opt-in flag and default to crib/marker placement.
- FP-9 Quit to main menu: CORRECT in outcome (§8.1, T21).
  - Team C asks for confirmation, the session is torn down, and the main menu returns without press start (MAIN:445-446).
  - The 1 s and 250 ms fades are missing (cosmetic, NOT-YET).
- FP-10 Loading screen and load failure: DEVIATION (§10.2-10.3, T23).
  - There is no `city_load` screen (mode 4) between the choice and gameplay (NOT-YET).
  - A world-setup failure after the menu returns its error code, and MAIN:445 then ends the process. The original goes back to the main menu (pop_to(1)+push(2)).
  - Fix: on a setup failure after the menu, loop back to RunMainMenu.
- FP-11 Paused movement step: CORRECT as a labelled CHOSEN item.
  - viewer_playtest_frame_player.cpp feeds a 1e-6 s frame while paused (604a471).
  - 859fbf9 skips the step when dt <= 0, citing a 0xC0000409 crash whose root cause was not found.
  - No spec effect. The crash cause is still open.

### Part 2: 3d5e140 Team C front end, options, rebinding (FE)

- FE-1 Press start: CORRECT (§3.2). The top list shows at once.
- FE-2 Top list and Continue rule: CORRECT (§3.4, §3.5).
  - FMM setSaves builds: Continue (only when the newest entry is readable), Campaign, Options, Quit Game.
  - Co-op, Whored, DLC and Extras are left out (labelled CHOSEN).
  - Continue loads the newest readable save. `MAIN_MENU_CONTINUE_UNAVAILABLE` is kept as the fallback.
  - The ordering key problem of FP-6 carries over.
- FE-3 Quit from the main menu: CORRECT (§8.2, T22). Quit Game, and Esc/Back on the top list, open `PLT_QUIT_GAME` / `PLT_QUIT_GAME_WARNING` with the cursor on No. Yes quits.
- FE-4 Campaign submenu: CORRECT (§4.1, §3.6). It offers New Game and Load Game; Back returns to the top list.
- FE-5 Difficulty: NOT-YET (§4.1 step 3; tables-ui-controls §18.4).
  - FMM:142 stores the choice and FMS:389 only logs it. MainMenuResult has no difficulty field and nothing writes the difficulty global, so every New Game plays at Normal.
  - Also missing: the storage check (`SAVELOAD_AUTOSAVE_OVERWRITE`, T14). The display-calibration step is skipped (labelled CHOSEN).
  - Fix: add difficulty to MainMenuResult and apply it through `game_difficulty_select` before the world starts.
- FE-6 Load list: CORRECT in shape (§5.1, §5.2, §7.4).
  - 8 rows; a non-selectable `SAVELOAD_NOTHING_TO_LOAD` row when empty; readable saves first, unreadable last.
  - No confirmation from the main menu. An unreadable row shows `SAVELOAD_ERROR` / `SAVELOAD_FILE_NOT_LOADABLE`.
  - Not built: the completion % column, and delete (Del or pad Y with `MENU_DELETE_SAVE`).
  - Ordering key: see FP-6.
- FE-7 Pause top list: CORRECT, with labelled gaps (§18.1).
  - Header `SYSTEM_PAUSED`. Entries: Save Game, Options, Quit. There is no Resume entry; Back resumes, as in the original.
  - Load Game, Cancel Mission, Co-op and record mode are left out (labelled).
  - "Quit to Desktop" is an extra entry, labelled CHOSEN, with its own confirmation.
- FE-8 Pause Quit: CORRECT (§8.1). It asks `PAUSE_MENU_QUIT_TITLE` / `QUIT_GAME_TEXT_FULL` with the cursor on No. Yes goes to QuitToMainMenu.
- FE-9 Pause Save: DEVIATION. FMM:241 returns Save immediately, with no list and no confirmation. Same fix as FP-7.
- FE-10 Options: CORRECT (§18.2, §18.4).
  - Pages: Mouse, Gameplay, Display, Audio.
  - Difficulty applies only when the Gameplay page is left, and values above 2 are ignored.
  - Every page except Display has Restore Defaults with a confirmation.
  - The mouse and brightness UI scales are labelled CHOSEN.
  - The profile is written to `sr3def_profile` in the save directory (labelled), but nothing reads the options back into the game (NOT-YET).
- FE-11 Rebinding: DEVIATION, minor (§18.3).
  - Correct parts:
    - The six categories appear in order and the Camera set is hidden.
    - Debug rows are skipped.
    - Each row has primary and secondary columns, and each axis direction has its own row.
    - Accepted inputs are a key code below 0x100 or mouse buttons 0-6.
    - The same-set sweep clears conflicting slots to UNBOUND and records them.
    - Common_Bindings sweeps every set.
    - Reset asks for confirmation.
  - Gap (a): the "every set" sweep covers only the six loaded categories. A General rebind to the wheel therefore never clears the Camera set's wheel zoom.
  - Gap (b): CapturePoller (FMS:149) polls keys and buttons 0-4 only, so the wheel (buttons 5 and 6) cannot be bound.
  - The allowed-sharing pair lists are empty (labelled; the spec does not give their contents). Rebinding therefore strips the shipped shares of Q and Mouse Left.
  - The bindings are not applied to game input (NOT-YET).
  - Fix: include the Camera set in the sweep, and capture wheel up/down as buttons 5 and 6.

### Part 3: a5ed0b8 and 859fbf9 differential corrections (DV)

- DV-1 D-17 torque curve: CORRECT in the function, DEVIATION at the live call (physics §10.2).
  - EngineNetTorque (DS:160) is correct:
    - field roles are not crossed;
    - r = max(rpm, target);
    - T is a quadratic bump and R is piecewise linear;
    - at or above Max_RPM, T = 0;
    - coasting torque is -(mass x 0.35 x rpm / Max_RPM);
    - x0.85 for the attached-object condition, then x the 1.25 tweak.
  - SUB:863 passes transmissionRpm = -1, so the "transmission RPM below Min_RPM" scaling of R and of the coasting torque uses the engine RPM instead.
  - Fix: pass the wheel-derived transmission RPM.
- DV-2 D-18 RPM update: DEVIATION.
  - EngineRpmUpdate (DS:146) matches the rule but nothing calls it.
  - SUB:836 still ramps toward its own wheel/clutch-slip target and clamps.
  - In the original, engine RPM equals transmission RPM whenever that is at or above Min_RPM + (Clutch_Slip_RPM - Min_RPM) x throttle.
  - Fix: call EngineRpmUpdate in that step.
- DV-3 D-19 gears: CORRECT (DS:204, physics §10.3).
  - No cooldowns, the timer never blocks a decision, comparisons are strict, and the downshift and upshift are checked in the same step. The caller skips reverse.
  - The shift-window and upshift-inhibit inputs exist but are never passed. That part is NOT-YET; the spec has not traced what drives them.
- DV-4 D-20 steering limiter: CORRECT (DS:68). It has the return-pair seed on a reversal, the braking zone, the curve and cruise segments, and tolerance 1e-6.
- DV-5 D-9 shotgun pellets: DEVIATION (weapons §4.5.3).
  - These match the rule:
    - ShotgunPelletFactor with a draw (damage_model.h:68);
    - TableRandomRing (MT19937, seed 5489, 8,192 values).
  - ProducerInput.pelletDrawU32 defaults to 0 (damage_model.h:109). Nothing calls TableRandomRing::next(), and nothing in src or tools calls ProduceWeaponHitDamage yet.
  - Effect: any use gets u = 0, the old minimum factor.
  - Fix: take the draw from a session ring inside the producer when the field is unset.
- DV-6 D-10 base damage: CORRECT (damage_model.h:39). The far test comes first, squares are rounded to single, t is rounded to single and the interpolation runs in double, matching vectors C2.
- DV-7 D-11, D-12, D-13: CORRECT.
  - Damage assist keeps the evaluation order of vectors C3.
  - The regen wait has no 1e-3 guard (hard difficulty gives 7,799).
  - Regen rate, per-tick delta and health are single precision.
- DV-8 D-21 notoriety: CORRECT (ai §19.8).
  - Negative points are applied through the decay roll-down.
  - A zero-point report still runs the award.
  - Landing on the floor keeps the remainder.
  - The level-entry floor includes the fractional part.
- DV-9 D-1 to D-4: CORRECT (progression B.1.x).
  - Products are rounded to single, then half away from zero; out of range gives INT32_MIN.
  - Diversion threshold 0.001f, cash floored.
  - Toasts pay respect or cash; the notification fires only for amount > 0.
  - Award and level-up sums wrap at 32 bits.
  - D-8 reads Props_Award as unsigned. No vectors exist for its edge cases.
- DV-10 D-22 cash add: CORRECT formula; the multiplier is NOT-YET.
  - Order follows the spec: a positive local amount is scaled first, as round_half_away(single(single(amount) / 100) x multiplier x 100), clamped. Then come the signed 32-bit wrap, the +-2e9 clamp and the floor at 0.
  - There are two copies of the routine: sr3player::AddCashWallet (vitals.h) and sr3progression::AddCashToWallet.
  - EngineState's cash multiplier is never written, and the activity and collectible callers pass 1.0f (labelled "not tracked yet"). So Cash_Bonus_1 to 4 and Cash_Bonus_Modifier still have no effect.
  - Fix: apply the type-38 unlock into one multiplier and keep a single wallet function.
- DV-11 D-5, D-14, D-16: NOT-YET.
  - None of the four merges touches the xtbl reader or the name hash. There is no D-5, D-14 or D-16 reference under src or include at 97bad5c.
  - Fix:
    - trim bytes 1-32 from each text run and keep only the first run;
    - adopt the D-14 float grammar;
    - sign-extend bytes in the rotate-left-6 hash.

Top risks to the first-playable loop:
- FP-7 / FE-9: saves overwrite with no list or confirmation.
- FP-2: the wrong pause path (reason 4 and dialog_pause_show instead of mode 6 and reason 0xD).
- FP-4: the unlabelled free-roam New Game.
- FP-6: save order by file time.
- FE-5: the chosen difficulty is dropped.
- FP-10: a load failure exits instead of returning to the menu.

## Review of looks-like-the-game merges d3b010d + ef57970 + 19a1502 + b11dd4b (+0f5a672) (2026-10-19)

Scope: git diff of each merge against its first parent, src/include/tools/tests, TEAM B repo, read-only. 19a1502 is "Merge commit 84b1be9": it brings the first Team C playtest sky gradient (viewer_playtest_sky.cpp), the allBlocks street draw (viewer_city_ground.cpp) and HANDOFF-C.md. It sits BEFORE d3b010d in history (f0a876d is an ancestor of d3b010d), so d3b010d supersedes much of it.

- LG-1 CORRECT (spec-tables-environment 19.2): src/sky_sun.cpp TodLightDirection matches the pseudo-code: 7.5 offset, /12, REVERSE default on, a=(t-1)pi, y=min(max(|sin a|,sin e),1), 0.53 factor on z, yaw rotation, 0.017453; SunTowardDirection uses min elevation 40 (the sun-record value) and yaw 0.
- LG-2 CORRECT (19.1, FINAL curve read): SampleTodCurve uses x=2H, slot=floor mod 48, linear blend of slot and slot+1 mod 48. ResampleTodKeys uses the 47 wrap span. colour() = rgb x intensity.
- LG-3 CORRECT with one caveat (19.1): the Clear Skies path uses default_district.xtbl base curves (west0..3, east0..3, zenith, ambient_color, tod_light_color). Caveat: a stage with override rows still uses override cells only, not merged over the base curves. The code labels this HIGH/untraced, which matches the spec's own HIGH note. NOT-YET: the merge order.
- LG-4 DEVIATION (spec-tables-environment 19.2 / 19.1 Hemisphere_colors; viewer_playtest_sky.cpp CornerColours/drawSkyGradient): the sky band elevations {0,12,30,55,90} deg and the eastness blend via +X = east are labelled CHOSEN, not spec. The spec orbitals give the sun azimuth from TodLightDirection, so eastness should be derived from the sun direction, not world +X. Fix: compute the east/west mix from the dot product of the view direction with the sun's horizontal direction. Also drawSkyGradient divides all nine colours by the maximum when it exceeds 1.0, which is an invented normalisation (CHOSEN-ish, but unlabelled).
- LG-5 NOT-YET (spec-render-pipeline 25, fog): no fog formula and no sun-light shader change is in these merges. fog_color and fog_density_new are only sampled in the test (real_tod_curves_test.cpp:77,83). The viewer draws no fog. Sky dome is a screen-space strip gradient, not the rfg_skybox mesh.
- LG-6 INVENTED (ef57970, applySceneExposure, viewer_playtest_sky.cpp): the exposure scale (peak-sum target 1.25, ambient lift x2.0, ambient clamp to 1) is labelled "CHOSEN tonemap stand-in", so it is allowed, but it is the ONLY lighting path for weather corners: it uses override cells (FindCellByStageName) and ignores the base curves that d3b010d added, so Clear Skies gets no exposure (return when any cell is missing). Fix: reuse CornerColours/ComputeSceneColours for ambient and sun.
- LG-7 CORRECT with caveat (zone-data 13.5.2, d3b010d + 19a1502 viewer_city_ground.cpp): every Mesh sub-block and every channel used by a draw range is drawn at identity; near-origin blocks are skipped. The skip test is bounds within +-50 m of the origin on x and z only (CHOSEN, labelled). The spec says 98 world-space and 45 near-origin blocks in tile 0915; the test is per-channel, not per-block, so a block mixing channels could be partly drawn. The 19a1502 version (first layout-0/2 channel per block, no near-origin skip) is superseded by d3b010d. Fix: none needed, but confirm the 98/45 split with a count log.
- LG-8 CORRECT (H.7): b11dd4b viewer_playtest_hudart.cpp finds ui_hud_meter_fill via bitmap_sheets.xtbl then the sheet xtbl (StartX, StartY, ImageWidth, ImageHeight), and the page texture "<sheet>.tga" from the interface peg. UVs = sub-rect / page size. Layout of the bar is CHOSEN (labelled). Caveat: only one image is wired; the cache is keyed by the session pointer in a static map and never freed.
- LG-9 CORRECT (0f5a672): the stability fix (texture_upload.cpp, soak test, sr3_game main) touches no spec-governed values. No contradiction found in a light check.

## Review of Team B 3fcad4e (merge 6ccbae8): differential parser fixes D-5 / D-14 / D-16 (2026-10-20)

- DP-1 D-5 CORRECT: the name hash reads each byte as signed 8-bit, widened to 32 bits, before the lower-casing test and the XOR (spec-vpp-container §2.2). Not checked: the NULL input returning 0xFFFFFFFF (or 0xFFFF for the 16-bit variant).
- DP-2 D-14 CORRECT: the float grammar starts the fraction at the first '.' anywhere and ignores any exponent when there is no '.'. The integer part is a signed int32 and the exponent is read the C-runtime atoi way (spec-tables-traffic-ai §1.3).
- DP-3 D-16 CORRECT: the parser trims bytes 1-32, keeps only the first non-empty text run, decodes 2-byte UTF-8 to one byte, and rejects CDATA and DOCTYPE (spec-xtbl-format §7). Not checked line by line: the 5-entity limit, rejecting a BOM, and the last-top-level-element-wins rule. Team B's differential_tables_test covers the vectors.
- No deviations found in the parts read.

## Answers to Team B CHOSEN questions (2026-10-20)

Static disassembly only. Source: Team B HANDOFF lines 6 and 18 (current CHOSEN lists), then the older "Qs Team A" lines 159-225, 244, 264, 314-321, 343, 371-397, 440 and 582-598. Each new answer is a dated note "[2026-10-20 Team B CHOSEN Q: ...]" at the pointer. Status: ANSWERED = already in a Team A spec before today; NEW = answered today; PARTIAL = some of it on file; OPEN = not answered.

### Line 6: homies and followers

| # | Team B CHOSEN | Status | Answer in short | Where |
|---|---|---|---|---|
| 6.1 | all 24 base rows start at status 1 | NEW, FINAL | All rows start at **0**; listed only when the unlockable (type `Homie`, live byte `+0xDC`) is set or `homie_mission_unlock` ran | spec-ai-runtime.md §24.2 note |
| 6.2 | phone talk 2000 ms after a 1.0 s ring | NEW, FINAL (Team D emulation 2026-10-20) | Timers step 1/30 s (0.033333335) per update; second ring at update 30 (1.000 s), connect on the next (1.033 s), summon on the one after (1.067 s), phone closes 91 updates later (3.033 s, timer strictly below 0); no 2000 ms | spec-cellphone-menu-runtime.md §16a.2 note |
| 6.3 | drive-up arrival 15 m and < 2 m/s; speed-matched 3 m/s boarding | NEW, FINAL (arrival test; Team D emulation 2026-10-20) | Mode 3 sub-state 7 drives the route and stops by the §22.3 rule; arrival = body speed < 1.0 m/s STRICT (exactly 1.0 does not arrive; speed 0 when state byte != 1), then the crew get out (sub-state 8) and are released; no boarding from the homie car | spec-ai-runtime.md §24.5 note |
| 6.4 | direct steering, no route planner | ANSWERED | The car follows a lane route (§22 drive-to); see the §24.5 note | spec-ai-runtime.md §24.5 step 6, §22.3 |
| 6.5 | 3 free passenger seats | NEW, FINAL/HIGH | Seat count = vehicle `+0x824` (`0x00ab4af0`); free = count - driver - occupied | spec-ai-runtime.md §24.8 note |
| 6.6a | player-gait thresholds | ANSWERED | gait = max(player gait, 1) + (d > 7.5) + (d > 15), cap 3 | spec-ai-runtime.md §24.7 |
| 6.6b | gait speeds 1.6 / 4.0 / 6.5 m/s | NEW, HIGH | No constants: clip root motion x `+0xa64`; ped walk 1.386, run 6.373 (male); sprint not measured | spec-ai-runtime.md §24.7 note |
| 6.7 | unstreamed-follower teleport at 200 m | NEW, HIGH | Streaming state, not distance; distance rule is 35 (100 m / 150 m, > 4 m height) | spec-ai-runtime.md §24.7 note |
| 6.8 | dismissal removes the first joined follower | NEW, FINAL | The dismiss control dismisses the whole party (`0x009e2bc0`, forced drop loop); single dismiss only in scripts | spec-ai-runtime.md §24.10 note |
| 6.9 | revive = player within 2.5 m for 2.5 s | ANSWERED + NEW note | 3.0 m prompt radius, 2.5 s hold, release pauses the timer | spec-ai-runtime.md §21.10.1; §24.9 note |
| 6.10 | follower HP 100 | NEW, HIGH | Rank `Hit_Points` of the row's `Spawn_Info_Rank`, scaled by the homie health bonus | spec-ai-runtime.md §24.9 note |
| 6.11 | one shared enemy (nearest the player), re-copied every 2000 ms | ANSWERED + NEW note | Each follower copies the player's own target `+0x1e30` every 2000 ms, else its own §21 choice | spec-ai-runtime.md §21.2, §24.9 note |
| 6.12 | NpcFireModel default damage | ANSWERED | Weapon row damage with the §4.5 rules | spec-weapons-runtime.md §4.5; §24.9 note |
| 6.13 | can_leave_behind default false | NEW, FINAL (confirms) | Phone path never sets it; only the Lua native, the network replica and one activity set-up | spec-ai-runtime.md §24.10 note |
| 6.14 | garage-pick row uses the row's first vehicle | ANSWERED | The garage row chosen on the phone is the car | spec-cellphone-menu-runtime.md §16a.2; §24.5 note |
| 6.15 | recruitable default: viewer 1 vs EngineState false | ANSWERED + NEW note | `.data` byte `0x0130af60` = 1 (dismissable `0x0130af61` = 1) | spec-ai-runtime.md §24.3 and note |

### Line 18: aircraft, boats, freefall (AW-15)

| # | Team B CHOSEN | Status | Answer in short | Where |
|---|---|---|---|---|
| 18.1 | flat water plane at 0.0 m | ANSWERED (confirms) | City sea level 0.0 from the ocean volumes; local volumes have their own tops | spec-physics-runtime.md §10.13.9 Q1 |
| 18.2 | 2 x 5 hull cells, hull height <= 1.2 m, drag 0.5/s + keel + heave | ANSWERED (replaces) | Collision shape clipped per vertex, per-face drag and lift, implicit solve | §10.13.9 Q2 |
| 18.3 | heli radius of gyration 0.8 x rotor radius; box tensor for plane/boat | ANSWERED (replaces) | I = Mass x diag(Pitch_UI, Yaw_UI, Roll_UI) for every class | §10.13.9 Q3 |
| 18.4 | wheel model for plane taxi | ANSWERED (confirms) | The full car wheel step, clutch off, engine force only | §10.13.9 Q4 |
| 18.5 | node-height ground fallback when the walkable ray misses | ANSWERED | Use the ground triangle list | spec-zone-data-format.md §13.5.3 |
| 18.6 | no crash damage | NEW, HIGH | Ordinary vehicle contact damage and the shared destruction trigger, plus part losses | spec-physics-runtime.md §10.13.7 note |
| 18.7 | empty craft set down at once on exit | NEW, HIGH | Keep stepping: empty heli uses + 4.9 lift compensation and sinks; plane loses throttle; boat floats | §10.13.7 note |
| 18.8 | aircraft meshes draw the head of every LOD chain | ANSWERED (confirms) | LOD0 of each sub-mesh run; group 0 is a small part | spec-vehicle-geometry.md §12.4 M1-M2; §10.13.9 note |

### Lines 440 and 582-598

| # | Question | Status | Answer in short | Where |
|---|---|---|---|---|
| 440.1 | the 7 combat-sweep OPENs (dlc2_m01, m08, m09, m10, m13, m21, m24) | ANSWERED | Spec sync 78fb40a | spec-lua-api-behaviour.md §17.29, §21.22, §28.8, §46.1, §12.14, §30.6 (partial), §7.9 |
| 440.2 / 590 | which relation or order makes mission targets hostile | NEW, FINAL/HIGH | Default matrix has no 0; relation 1 gives only a list-6 (suspected) entry; hostility needs script natives or the player's own acts | spec-ai-runtime.md §12.7 note |
| 440.3 / 597 | downed selector inputs: brute, gang, follower, Bleed_Out_Hit_Points | NEW, FINAL | `character.xtbl` Flags `brute` (`+0x48` bit 0), not `zombie` (bit 7); gang = team <= 4 and relation to team 0 <= 1; follower = in a party; the Bleed_Out threshold (rank `+0x0C` or instance override) is NOT read by the selector, it belongs to the type-1 predicate `0x00972790` (Team D emulation 2026-10-20: FINAL) | spec-ai-runtime.md §21.10.1 note |
| 440.4 / 598 | type-2 counters and hook arguments | NEW, HIGH | The expiry fires Killbane's `on_take_damage` with no attacker; m21's counter only grows on attacker hits | §21.10.1 note |
| 440.5 / 598 | `human_enter_downed` duration unit | NEW, FINAL | Integer milliseconds (truncated); <= 0 keeps the default | §21.10.1 note |
| 440.6 | RG-12 aligned-gate operand (15 m re-query vs 5 m origin) | ANSWERED | Re-query 15 m linear (§20.6.3(d)); the close/aligned branch is cos 30 deg and 5.0 m with vt 3.5 m/s | spec-ai-runtime.md §20.6.3 |
| 440.7 | signal stop-line geometry | ANSWERED | FINAL | spec-ai-runtime.md §20.6.11(c) |
| 440.8 | cover-node source and evaluator (AI-7) | ANSWERED | Selection traced; search and timers FINAL, cost weights PARTIAL | spec-ai-runtime.md §15.7, §21.3 |
| 440.9 | provocation meter feed | ANSWERED | 150 HP / 6 s meter | conformance review, R3 answer 2; spec-ai-runtime.md §24.9 |
| 440.10 | radio xtbl Length vs media length | ANSWERED | The timer uses the raw `Length` | spec-audio-runtime.md §14.6.2 |
| 440.11 | The Mix 24 vs 18 songs | ANSWERED (2026-10-20, FINAL) | 18: the only shipped `the_mix_radio.xtbl` has 18 `Song_List` rows, matching the 18 media records; the table's "24" was wrong | spec-audio-runtime.md §14.6.1 note |
| 440.12 | R-6 rule-4 overread | ANSWERED | The engine reads one record past the array; Team B's re-roll stays a labelled deviation | spec-audio-runtime.md §14.6.1; review R-6 |
| 440.13 | Bink pre-stage array refill trigger/position (Y row 3 col 6) | OPEN (2026-10-20: not attempted) | Lowest priority: video, not gameplay. The refill lives in the codec DLL's case-selector code and is best settled by emulating it (see the Team D list below) | spec-bink-codec.md §5.3c |
| 583.1 | vehicle speed query `0x00a4c440` | PARTIAL | Read; unit scale still open | spec-lua-api-behaviour.md §41.6 |
| 583.2 | heli sub-object `+0x4b0` mode enum | ANSWERED | Staged destination pair | spec-ai-runtime.md §22.11 |
| 583.3 | `move_to_do` id counter `0x0130cb94` | ANSWERED | | spec-lua-api-behaviour.md §21.22 |
| 583.4 | turn busy flag `0x00987410` | PARTIAL | | spec-lua-api-behaviour.md §30.6 |

### Older lines (159-397)

| Line | Question | Status | Where / why open |
|---|---|---|---|
| 159 | vehicle reference point `0x0056e530` | ANSWERED (2026-10-20, FINAL/HIGH) | world position of the vehicle's `license_plate` prop point; vehicle `+0x40` when the prop is absent or the model is not loaded: spec-player-runtime.md §4.15.5 note |
| 159 | driver-cam collision query | ANSWERED (2026-10-20, FINAL/HIGH) | same 0.12 m sweep `0x00565d70`; filter `0x00566070` ignores the own car, its tow partner and cars within 5 m of the player: spec-player-runtime.md §4.9 note |
| 159 | velRightBody sign | ANSWERED | right = (cos h, 0, -sin h), +h = right turn: spec-physics-runtime.md §10.15 |
| 159 | reverse swing for road vehicles | ANSWERED (2026-10-20, FINAL) | reverse flag → `Swing_Rate_Rev`; D = travel direction, not mirrored, so the camera swings round to look the way the car moves: spec-player-runtime.md §4.15.2 note |
| 160 | auto-pitch `0x00570200` | ANSWERED | spec-player-runtime.md §4.15.3 (2026-10-14 note); lane meanings of `0x00a92f30`/`0x00a90f30` OPEN |
| 165 | ride height | PARTIAL | spec-physics-runtime.md §10.11.5 is PARTIAL |
| 166 | NPC turn rates (§5.4.3 step 5) | ANSWERED (2026-10-20, FINAL/HIGH) | turn = clamp(0.2·Δ, ±R·Δt), R = 5 − 3(v − 1.5)/3.5 clamped to [1.3, 5] rad/s: spec-player-runtime.md §5.10 note |
| 166 | heightmap units | ANSWERED (superseded) | the heightmap is not the ground: spec-zone-data-format.md §13.5.1 |
| 166 | mesh forward axis vs atan2 | ANSWERED | spec-physics-runtime.md §10.15 (Q8) |
| 170 | game-start weather stage | ANSWERED | spec-tables-environment.md §18/19 |
| 170 | weapon fire / dry-fire Wwise event and media | PARTIAL | dispatch `0x009ba4a0` in spec-weapons-runtime.md §2.2c; hashed ids only, names OPEN |
| 170 | runDespawn stays false (RF-1) | ANSWERED | spec-ai-runtime.md §20.6.1(d); review RF-1 |
| 179 | civilian shot / kill / run-over records and values | ANSWERED | `notoriety.xtbl` rows (spec-expected-table-values.md); consumer spec-ai-runtime.md §19.2 |
| 179 | Check_Detection witness test, x3 flag | ANSWERED | spec-ai-runtime.md §19.2, §19.8 |
| 180 | G4 floor, negative points, decay floor | ANSWERED | D-21: spec-ai-runtime.md §19.8 |
| 180 | free-roam difficulty record | ANSWERED (2026-10-20, FINAL) | no separate record: set A (single player) row `difficulty` (`0x012ece10`, default 1); set B only in two-player co-op: spec-tables-progression.md §7.3 note |
| 181 | vehicle-hit-ped trigger, run-over crime record | ANSWERED (2026-10-20, HIGH) | no run-over row; the (7,5) rule passes the driver as attacker, and a kill reports civilian/gang/police kill; a non-lethal hit is still OPEN: spec-ai-runtime.md §19.2 note |
| 181 | "driving" road-segment / water gate | ANSWERED (already on file) | driving (`0x009e82f0`, `0x009b9160`) on a road segment (player `+0x20dc`) without segment flag bit 18 (water): spec-ai-runtime.md §20.6.10(a) step 1 |
| 181 | roadblock Max_active per level, layouts, 270 m despawn | ANSWERED | spec-ai-runtime.md §20.6.10 |
| 188 | which `0x2244` record, `~s` meaning, street collision, -5 | ANSWERED | spec-zone-data-format.md §13.2, §13.5.1-§13.5.3 |
| 198 | free-roam death outcome | ANSWERED | spec-player-runtime.md §18 |
| 198 | Fall_Damage formula | PARTIAL | constants in spec-tables-progression.md §10.2; the consumer is not traced |
| 199 | free-roam hostile NPC fire trigger | ANSWERED | today's §12.7 note (what makes NPCs hostile) |
| 199 | camera mode 9 | ANSWERED | death camera: spec-player-runtime.md §18.4, §18.8 |
| 199 | health regen owner | ANSWERED | spec-player-runtime.md §18 (regen) |
| 200 | mission HUD reads 'player' while NPC fire hits '#PLAYER1#' | OPEN (2026-10-20: not attempted) | the question names Team B harness strings, not an engine routine; Team A needs the HUD native and the argument Team B passes before it can trace the resolver |
| 206 | cover_node coordinate frame | ANSWERED (2026-10-20, FINAL) | world frame; `cover_pos` is used raw, with no zone or tile offset: spec-ai-runtime.md §15.7 note |
| 206 | quaternion order + cover_angle bytes | ANSWERED (2026-10-20, FINAL) for covers | `cover_orient` is a 3×3 matrix (right/up/forward), not a quaternion, and it is the arc reference; the angles are float degrees on disk, stored as round((deg + 180)/360·255): spec-ai-runtime.md §15.7 note. The quaternion order of the generic 28-byte placement transform is still OPEN |
| 207 | brute / gang / Bleed_Out source | NEW | today's §21.10.1 note |
| 207 | LOS recheck interval rule | ANSWERED | 700-4000 ms, +500 after visible: spec-ai-runtime.md §10.3 |
| 207 | mission NPC weapon | ANSWERED (HIGH) | rank per-team `weapon_loadout`: spec-tables-progression.md §10.6 |
| 214 | vehicle-vs-character rule (7,5) `0x007663e0` | ANSWERED | spec-physics-runtime.md §10a |
| 215 | impactMagnitude `0x00762ab0`, material lookup, `+0x1588`, surface predicates | ANSWERED | spec-physics-runtime.md §10a |
| 215 | vehicle HP / destroy | ANSWERED | spec-vehicle-damage-runtime.md §1-§3 |
| 223-224 | MOPP walker / key list | ANSWERED | spec-zone-data-format.md §13.5.3 |
| 224 | "~s zone loaded" flag, ^variant selection, tail-mesh offset, AABB 0.005 m pad | PARTIAL (2026-10-20 pointers, no new code read) | the zone-loaded gate is `0x00861ec0` (spec-zone-data-format.md §13.5.1 recipe step 6); the tail mesh is world-space with no offset (same recipe, step 1); 0.005 is the value `0x0075f370` writes to `0x0131e81c` at Havok world set-up (spec-physics-format.md), but that it is the AABB pad is not checked. Still OPEN: how the active `^variant` is chosen |
| 225 | object -> .clmesh_pc binding | ANSWERED | spec-world-streaming.md §10.7.1 |
| 244 | W2 reload frames, equip groups | ANSWERED | spec-weapons-runtime.md §5.4.x |
| 264, 320-321 | MOPP triangle list; placement +12 index, +6 yaw, origin Y | ANSWERED | zone §13.5.3; world-streaming §10.7.1 |
| 314-315 | ambient pursuit producer, per-level counts and rings, cop character row, crew hand-off | PARTIAL | police spawn spec-ai-runtime.md §19.7; counts and rings OPEN |
| 315 | bust action | ANSWERED | spec-player-runtime.md §18.10 |
| 315 | cop-kill activity | ANSWERED (2026-10-20, FINAL) | `notoriety.xtbl` row 24 `police kill` (Police/Points 75), reported by `0x009e2950` for victim team 5 or 6: spec-ai-runtime.md §19.2 note |
| 343-344 | weather clock, picker, smoothing, game-start stage | ANSWERED | spec-tables-environment.md §18/19 |
| 371 | jump height `0x0098bea0`, `0x00989670` | PARTIAL | spec-player-runtime.md §3.6.4 (`0x0098bea0` = jump start, sprint flag); spec-anim-runtime.md §4.1; height comes from the clip, value not measured |
| 371 | default jump/crouch bindings, crouch capsule, clips | OPEN | no binding in the tables (review JC-5) |
| 376 | 5000 m walk lookahead | ANSWERED | spec-ai-runtime.md §18.1 |
| 376 | §18.6 5.0 aligned operand | ANSWERED | spec-ai-runtime.md §20.6.3 (close/aligned branch) |
| 377 | ambient AI_Max_Braking_Decel / condition | ANSWERED | spec-ai-runtime.md §20.6.3, §20.6.7 |
| 377 | streamed Spline_Type set + cadence | PARTIAL | set in spec-tables-progression.md §10.6; cadence OPEN |
| 377 | yield window, anti-deadlock, fork pick | ANSWERED | spec-ai-runtime.md §20.6.7(c): no tie-break, both cars yield |
| 381 | free-roam respawn / busted path | ANSWERED | spec-player-runtime.md §18 |
| 382 | melee damage event + move selection (§15.2) | OPEN (2026-10-20: not attempted) | the starter `0x00950c30` hands off to animation events; the event → `MeleeMove` → damage path is spread over several routines and needs a dedicated pass (next lead: `0x00985760`) |
| 383 | vault/ledge probe `0x009660e0` + climb states | PARTIAL | spec-player-runtime.md §3.6.4; spec-ai-runtime.md §3.3 |
| 395 | is 0.425 m also the advance test for intermediate route nodes | ANSWERED (2026-10-20, FINAL negative) | no: 0.180625 has only two readers, the crossing test and an unrelated camera test. The intermediate-node radius stays OPEN because the path follower was not found: spec-ai-runtime.md §20.6.9(c) note |
| 396 | walk-variant states 27-35 -> clips | ANSWERED | uniform pick among the states the anim set has: spec-ai-runtime.md §20.6.9 |
| 396 | ambient ped gender source | ANSWERED (2026-10-20, FINAL) | the character row's `Gender` (first letter `M` → 0, else 1; absent → 3), copied to human `+0xa41`; there is no gender roll: spec-ai-runtime.md §20.6.2(e) note |
| 396 | respect-overflow wallet | ANSWERED | R3 answer 4: spec-tables-progression.md |
| 397 | library unlocks record L vs legacy row | ANSWERED (2026-10-20, FINAL) | the engine applies the unlocks of the new level's record (L + 1) after paying record L's cost: spec-tables-progression.md §5.4 note |

### Still OPEN, and why

- ~~No code read yet: vehicle reference point `0x0056e530`; driver-cam collision query; reverse swing; NPC turn rates; free-roam difficulty record; vehicle-hit-ped / run-over crime producer; road-segment / water gate; HUD 'player' name; cover_node frame and quaternion order; "~s zone loaded" flag, ^variant, tail-mesh offset, AABB pad; cop-kill activity; melee event and move selection; 0.425 m on intermediate nodes; ped gender; library unlocks; The Mix song count; Bink pre-stage refill.~~ **[2026-10-20 second pass. Each item is marked in the table above.**
  - **ANSWERED** (each with a dated "Team B open Q" note at its pointer): vehicle reference point; driver-cam collision; reverse swing; NPC turn rate; free-roam difficulty record (this also settles set A = single player, set B = co-op); cop-kill record; cover_node frame and angle bytes (a matrix, not a quaternion); ped gender; library unlocks; The Mix (18 songs).
  - **ANSWERED from text already on file:** road-segment / water gate (§20.6.10(a)).
  - **ANSWERED for most of the question:**
    - vehicle-hit-ped / run-over. There is no run-over row, and a kill reports civilian, gang or police kill. A hit that does not kill is still OPEN.
    - 0.425 m. It is not the intermediate-node test. The intermediate-node radius is still OPEN.
  - **Still OPEN, with the reason:**
    - HUD 'player': Team A needs Team B's exact native and argument.
    - `^variant` choice: not traced.
    - The quaternion order of the generic 28-byte transform: its reader was not found by property hash.
    - Melee event and move selection: needs its own pass.
    - ~~Bink refill~~ ANSWERED (2026-10-20, FINAL, Team D emulation): refill only at the start of a block row when cursor == bound exactly; no mid-row refill; spec-bink-codec.md §5.3g note.
    - The run-over hit virtual (character vtable `+0x80`): body not read.**]**
- Partly read: ride height (§10.11.5), dry-fire event names, Fall_Damage consumer, pursuit counts and rings, jump height value, Spline_Type cadence, vault states, `0x00a4c440` units, `0x00987410`.
- Inside today's answers: the homie route goal point (GUESS: the player position on the lane graph); the sprint clip speed; the writer of vehicle `+0x824`; the phone module's update rate; a class gate in the vehicle contact damage handlers.

### For Team D (single-routine emulation would settle these)

1. `0x0083d150` (phone update): state 1, ring count 1, timer 1.0, non-zero handle; count the calls to connect, to summon and to state 0.
2. `0x00b61990` (homie car sub-state 7): a mode-3 car at |v| 1.2 then 0.9 m/s; expect sub-state 8 only at 0.9.
3. `0x004fe0f0` (attitude) with the default matrix: a team-3 human observing a player-class team-0 human, flags clear; expect 1. Then `0x00506c20` for the same pair: expect a list-6 entry and no list-0 entry.
4. `0x009735d0` (downed selector): a character-info record with `+0x48` = 0x1, team 3, not in a party; expect type 1. Same with `+0x48` = 0x81; expect -1.
5. **ANSWERED/FINAL (2026-10-20, Team D emulation; spec-player-runtime.md NPC turn note).** `0x009918b0` (NPC intent shaper): an NPC with `+0xd1c` = 1.0, 4.0 and 7.0 m/s, `+0x1a64` = 1.0, `+0xd08` = 0, heading 90°, 30° and 10° away from `+0xa50`, Δt = 1/30. Expect turns of 9.549°, 5.457° and 2°.
6. **ANSWERED/FINAL (2026-10-20, Team D emulation; spec-player-runtime.md reverse-swing note).** `0x0056f410` (swing step): a road car (class 0) with the reverse flag set, velocity (0, 0, −5) against a camera facing +z, and again at (0, 0, −0.5). Expect D = d·(0, 0, −1) in the first case, and the |h|² blend toward the camera forward in the second.
7. **ANSWERED/FINAL (2026-10-20, Team D emulation; spec-player-runtime.md driver-cam note; the 5 m test is from the player character).** `0x00566070` (camera filter): the local player seated (`+0x16d4` = 3) in car A; car B 4 m away and car C 6 m away. Expect A → 0, B → 0, C → 1.
8. **ANSWERED/FINAL (2026-10-20, Team D emulation; spec-ai-runtime.md cover_node note).** `0x008cef40` (cover reader): property angles −180, −90, 0 and 179.9 degrees. Expect the bytes 0, 64, 128 and 255.
9. **ANSWERED/FINAL (2026-10-20, Team D emulation; spec-tables-progression.md §5.4 note).** `0x009db8e0` (level-up): a player with level index 0, remainder 0 and amount 2115. Expect index 1, remainder 0, and `0x0060ebf0` called with 1.
10. **ANSWERED/FINAL (2026-10-20, Team D emulation; spec-bink-codec.md §5.3g note).** Bink pre-stage refill: emulate the codec DLL's case-selector routine over the Y-plane row 3 / column 6 call of `spec-bink-codec.md` §5.3c, and log every write to the pre-stage array.

## Review of Team B 8e2a42e + ace9314 + 602732c (answers, D-24..27) (2026-10-20)

Scope: git diff of each merge against its first parent (src, include, tools, tests). Read-only; nothing built or run.

- TA-1 LG-8 FIXED (8e2a42e, c1d23dd): the HUD art cache now lives on the session (hudArtMeterFill, hudArtTried), so no static map keyed by the session pointer. Residual: the SRV is still only released at session teardown; still one image wired.
- TA-2 SUS-1 PARTIAL (ace9314): SuspensionSpringDamperForceNoXDenominator in physics_vehicle_driving_slice.cpp:257-265 is CORRECT against 10.4 (f32 steps, damping = compression if v < 0 else relaxation with 0 and -0 as relaxation, times M, zero when not touching). DEVIATION: chassisMass defaults to 1.0 (vehicle_driving_slice.h) and no caller sets it, so M = 1 in the live path. Fix: feed the chassis mass (1 / inverse mass) into SuspensionWheelTuning.
- TA-3 D-24 CORRECT (602732c): MergeWeatherField in sky_tod_curves.cpp:120-133 gives out = f32(f32(F x f32(1-t)) + f32(T x t)), base when neither cell marks the field or the merge is off. Not verified in this review: one segment for both cells, the override record, flag 0x012E6B78 (viewer_playtest_sky.cpp not read).
- TA-4 D-25 CORRECT (see TA-2). D-26 CORRECT: weapon_inventory.cpp PlanReload follows the float32 path. D-27 CORRECT: DownedGetUpHeal returns a float, clamped, with the fraction kept.
- TA-5 Homies CORRECT: roster rows start at 0 (InitialStatus); connect 1.033 s and summon +33 ms (kRingToConnectMs, kConnectToSummonMs); arrival is strict speed < 1.0 (kArrivalSpeedMps); the revive radius is 3.0; the dismiss control drops the whole party; the matrix default and the gang-member gate use matrix(team,0) <= 1; partyRecruitable is true.
- TA-6 Phone close at 3.033 s NOT-YET: no constant for the close timer exists (only the connect and summon constants).
- TA-7 Gait DEVIATION/PARTIAL: GaitSpeed uses 1.386 and 6.373 (CORRECT); sprint = 6.5 is CHOSEN. Seats DEVIATION: kChosenSeatCount = 4 is a fixed stand-in, not a per-vehicle seat count (+0x824); fix: read the seat count from the vehicle data.
- TA-8 Follower HP from rank plus the bonus: not changed in these merges (NOT-YET/unverified).
- TA-9 G2 ground triangles and the car mirror: d8d71fe is a HANDOFF note only (commit ac38bb4 says ground triangles are held for a golden decision). The MOPP walker (v0 = key & 0x3FFFF etc., 13,503 for 0915) is NOT-YET on main; zone_collision_mesh.cpp only locates the blob. §10.8 slip and §10.15 heading are not touched by these merges (NOT-YET).
- TA-10 D-26/D-27 sources: TEAM D\spec-differential-results.md was not found at that path, so the D-26/27 verdicts rest on the code comments and the tests.

## Review of Team B f640bd0 (level-up vectors) + fdfa9c8/56ccfde (hud-art-2) (2026-10-20)

- LU-1 CORRECT: a needed value <= 1 now blocks levelling (spec-tables-progression §5.4 note, Team D emulation).
- HA-1 CORRECT: the health bar fill uses the ui_hud_meter_fill sub-image of ui_bms_01 (H.7). The police notoriety icons are placed relative to the minimap centre (H.3 (191,566)).
- HA-2 CORRECT: the respect meter is a bitmap circle with end_angle = 2.65 - 5.25 x clamp(pct) at (1135, 87) in the 1280x720 reference (H.2).
- HA-3 CHOSEN, labelled: the 16 default wedges, the icon tint and the missing add/remove/pulse animations, the background ring and the level/amount texts. These are acceptable stand-ins; the vint documents carry the real animation data when Team C gets to them.
- No deviations.
