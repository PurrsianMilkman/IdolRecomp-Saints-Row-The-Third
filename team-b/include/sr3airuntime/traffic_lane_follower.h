#pragma once

// sr3airuntime/traffic_lane_follower.h - traffic-AI lane-following logic for
// ambient traffic, spec-ai-runtime.md Sec18.1-Sec18.6 ("Traffic AI
// lane-following: consumer module, pool mechanics, and Havok integration"),
// read alongside spec-zone-data-format.md Sec13.2 (the 0x2236 Traffic
// record this file consumes via sr3zone::TrafficGraph/TrafficRecord,
// sr3zone/traffic_graph.h - not re-parsed here).
//
// EXTENDED 2026-10-10 (traffic-overtake-passside-flag task): Sec18.6a now
// CONFIRMS the real `+0xe09` bit 2 is a dynamic overtake/pass-side flag, not
// a static lane-cross-track sign - the cross-product stand-in the previous
// task below used is REPLACED with a real overtake-gate model. See file
// header item 5a for the full breakdown; items 1-5 below are otherwise
// unchanged from that prior task.
//
// REWRITTEN 2026-10-10 (traffic-lane-follow-real-mechanism task): Sec18 was
// re-opened by Team A and most of what the FIRST pass of this file labelled
// CHOSEN (because Sec18.1/18.2 were then honest negatives - "no traced
// function reads a vehicle position", "no cost/RNG choice found") is now
// CONFIRMED by name, address, and disassembly (Sec18.1 RESOLVED, Sec18.2
// REFINED, Sec18.5/Sec18.6 added). This file is REPLACED to match the new
// confirmed mechanism, not merely re-commented - the core algorithm shape
// changed (masked lookahead walk + nearest-distance junction cost +
// distance-throttled re-query, not "advance through one segment's own CSR
// points, then uniform-random-pick the next segment"). See each section
// below for exactly what is now CONFIRMED vs the few things that remain
// genuinely CHOSEN because this clean-room project's own data model has no
// equivalent field, stated plainly rather than silently kept from the first
// pass.
//
// ============================================================================
// 1. THE MASKED LOOKAHEAD WALK (CONFIRMED, Sec18.1, 0x00b1be40/0x00b1cc60)
// ============================================================================
// Sec18.1: "the forward-search function... walks the point chain forward
// from currentPoint... accumulating the per-point along-path distance...
// until it exceeds lookaheadDist... Candidate segments are filtered by
// (maskInclude & segmentKindFlags) != 0 && (maskExclude & segmentKindFlags)
// == 0, where segmentKindFlags is the 4-byte field at segment +0x1c (the
// same struct whose 2-bit 'kind' sub-field spec-zone-data-format.md Sec13.2
// already documents)."
//
// This project's own sr3zone::TrafficSegmentExtended/TrafficSegmentKind()
// (traffic_graph.h) is EXACTLY that CONFIRMED +0x1c/bits-28-29 field - the
// real mask test is reused verbatim below (PassesLaneMask(), applied to
// every junction candidate inside both WalkLookahead() and the re-query
// gate in StepLaneFollow()), not re-derived. What is NOT reused from Sec18.1's own
// "Point" struct: this project's sr3zone reader (traffic_graph.h) never
// modelled a standalone per-POINT struct with its own +0x2/+0x3 exit-index
// bytes or +0x4 along-path-distance float - TrafficPointPool only stores a
// flat float3 per index (traffic_graph.h's own CHOSEN point-pool stride,
// unchanged by this task). The walk below therefore operates over the
// SEGMENT graph (TrafficLaneGraph's own CHOSEN geometric adjacency, same as
// the first pass - see item 3 below) rather than a per-point exit-index
// chain, because the latter does not exist anywhere in this codebase's data
// layer and this task's own scope is the AI-RUNTIME logic, not a second
// zone-data-format reverse-engineering pass on top of an already-landed
// reader. The ACCUMULATE-DISTANCE-UNTIL-LOOKAHEAD-EXCEEDED mechanism itself,
// and the mask-filter test, are both now genuinely ported from Sec18.1's
// own confirmed shape - only the "which chain" substrate differs, honestly
// stated here rather than silently glossed as a 1:1 port.
//
// MASK VALUES THEMSELVES (CHOSEN, narrow scope): Sec18.1 confirms the TEST
// shape but ambient near-field traffic in this codebase has no wired-up
// vehicle-class/lane-class table feeding real maskInclude/maskExclude bits
// (that would be a lane-class xtbl, never named by Sec18 and out of this
// task's own scope) - callers default to accept-everything
// (kMaskIncludeAll/kMaskExcludeNone below), explicitly neutral, not a
// disassembly-derived value.
//
// ============================================================================
// 2. JUNCTION CHOICE: NEAREST-DISTANCE COST, NOT RNG (CONFIRMED, Sec18.2/
//    Sec18.5, 0x00b46fc0)
// ============================================================================
// Sec18.2 (REFINED): the per-exit kind-flag bits are "early-exit/cache
// conditions", NOT a discrete choice - "no cost comparison, goal-distance
// comparison, or RNG was found between... candidates in this function."
// Sec18.5 then finds the REAL choice one layer up: "0x00b46fc0... a generic
// 'pick the candidate minimizing a distance' selector... loops... calling
// 0x00b1cc60 once per candidate, measures the resulting target point's
// distance via 0x00b1ad80, and keeps the minimum-distance candidate - a
// genuine cost comparison, just not goal-seeking (no destination parameter
// reaches it)." A SEPARATE, goal-seeking variant also exists (0x00b1ce80/
// 0x00b1e5e0, Sec18.5), but Sec18.5 is explicit its own destPos feed is
// itself unconfirmed for any caller, and this codebase's near-field ambient
// traffic has no destination/route-goal concept anywhere else (checked:
// near_field_vehicle_population.h is position-only, no destination field).
// This file therefore ports the NON-goal nearest-distance variant
// (0x00b46fc0's own shape), the one Sec18.5 confirms needs no destination
// input - replacing the first pass's uniform-random ChooseNextSegment()
// outright, not merely adjusting its weighting.
//
// ONE GENUINE, FLAGGED AMBIGUITY IN THE SPEC TEXT ITSELF, NOT INVENTED
// AROUND: Sec18.5's own prose for 0x00b46fc0 ("measures the resulting
// target point's distance via 0x00b1ad80") does not say distance FROM WHAT
// reference point - 0x00b1ad80 is named only as a 2-point distance helper,
// and its own two operands are not spelled out. This file's reading
// (documented here, not hidden): distance is measured from the walk's own
// ORIGIN (the point the vehicle is currently leaving) to each candidate's
// own lookahead-advanced target - i.e. "prefer whichever legal branch lets
// the lookahead walk advance the shortest net distance from here", the
// natural pairing given 0x00b1cc60 (the function 0x00b46fc0 calls per
// candidate) is itself an origin+lookahead->target resolver (Sec18.1) with
// no second point of its own to compare against. A future pass that opens
// 0x00b1ad80's own two call-site operands directly should correct this if
// it differs.
//
// A SECOND, NARROWER JUDGMENT CALL inside that same reading: at a TRUE
// junction every legal candidate's own FIRST polyline point is, by this
// file's own adjacency construction (item 3), the SAME physical point (the
// junction node itself) - so comparing distance-to-each-candidate's-own-
// first-point would always tie. This file instead compares distance to
// each candidate's own SECOND polyline point (or its only point, if it has
// just one) - the first point that actually DIFFERS between candidates,
// consistent with StepLaneFollow()'s own "start at point index 1 of the new
// segment" convention just below (index 0 is the shared junction point).
// This is this file's own documented resolution of the ambiguity above, not
// a second independent invention.
//
// ============================================================================
// 3. LANE ADJACENCY - STILL THIS FILE'S OWN CHOSEN FACT, UNCHANGED BY THIS
//    TASK (sr3zone/traffic_graph.h explicitly declines to decide this)
// ============================================================================
// Sec13.2 confirms the Traffic record is "a genuine segment->point
// cross-reference graph" and Sec18.1 confirms the real consumer "hops
// between graph nodes/segments," but neither states exactly which bytes
// encode segment-to-segment adjacency. Unchanged from the first pass:
// segment j is adjacent-after segment i iff the LAST point of i's own
// decoded CSR polyline lies within kJunctionEpsilon of the FIRST point of
// j's own decoded CSR polyline - adjacency discovered GEOMETRICALLY from
// the already-CONFIRMED-shape/CHOSEN-stride decoded coordinates. A future
// unit that traces the real adjacency encoding should replace this
// wholesale.
//
// ============================================================================
// 4. POSITION RE-QUERY BEHIND A CACHED-TARGET DISTANCE THROTTLE (CONFIRMED,
//    Sec18.5, 0x00b4c710)
// ============================================================================
// Sec18.5: "0x00b4c710... reads a live, moving entity's own position...
// and gates the path re-query behind a cached-target distance check... It
// also reads a cached previous target point... compares it against a
// candidate position via a squared-distance computation against a
// threshold constant... and - only when the cached point is stale or the
// vehicle has moved far enough away - rebuilds a search box..., runs the
// nearest-candidate picker, calls [the masked walk's resolver], and writes
// the new result back into the same cache fields. This is a THROTTLED
// re-query mechanism, not a naive per-frame full graph-walk." Ported below
// as LaneFollowState's own cachedQueryOriginX/Z/cachedTarget plus
// LaneFollowConfig::reQueryDistanceThreshold - the VALUE is now spec-given
// (RG-12, spec-ai-runtime.md Sec20.6.3(d), FINAL): a double 15.0 m, LINEAR
// (not squared) - supersedes the earlier CHOSEN 3.0; re-query mechanism
// itself CONFIRMED and now actually load-bearing: StepLaneFollow() steers
// at the CACHED lookahead target (state.cachedTarget), recomputed via
// WalkLookahead() (item 1's own distance-accumulating walk, not merely the
// immediate next polyline point) only when this gate's own three
// conditions say to - no cached query yet, moved too far from where the
// last one was taken, or already arrived at the cached target. The walk's
// own anchor (state.currentSegmentIndex/pointCursor) is advanced to
// WalkLookahead()'s own forward-most reached point once per re-query, the
// natural "current point... advances as the vehicle does" update Sec18.1's
// own framing implies, without re-deriving it from a separate per-tick
// position check.
//
// ============================================================================
// 5. STEERING/THROTTLE OUTPUT (Sec18.3/Sec18.6, 0x00b47490 ->
//    0x00a929e0/0x00a92640 -> 0x00ae5290/0x00ae8fd0 -> the SAME
//    hk_custom_vehicle_driver_input fields spec-physics-runtime.md Sec10.6
//    already names)
// ============================================================================
// Sec18.6's own disassembly of 0x00b47490, read literally (NOT the
// orchestrator task brief's own paraphrase - the brief explicitly asked for
// this to be checked against the real text, and it does NOT match on one
// point, flagged plainly rather than silently "corrected" without saying
// so):
//   1. Computes a dot product of a heading vector and a target-direction
//      vector into a scalar (HIGH CONFIDENCE "cosine of the angle between
//      desired heading and current forward axis" - not independently
//      proven, the magnitude helper 0x00abaf10 was not opened).
//   2. Picks the STEERING SIGN (+1.0/-1.0) from "a lane-side status bit at
//      +0xe09 bit 2" - A STORED PER-ENTITY FLAG, NOT the dot product's own
//      sign. The dot product from step 1 is NOT what selects steer
//      direction in the real code.
//   3. Sends that +-1.0 value through the generic dispatcher
//      (0x00a929e0/0x00ae5290 -> writes +0x130, the exact steering
//      accumulator field Sec18.3's own disassembly of
//      hk_custom_vehicle_driver_input names) - CONFIRMED: the real command
//      is a FULL-LOCK +-1.0 value, not a proportional steer scaled by
//      heading error.
//   4. The dot product from step 1 instead gates a cos(30 deg)/5.0-unit
//      alignment-AND-distance test that picks between two DIFFERENT
//      THROTTLE-shaped calls: "far/misaligned" -> 0x00a92640(...,0) ->
//      0x00ae8fd0(driverInput, 0) - CONFIRMED value 0, i.e. throttle
//      commanded to zero; "close/aligned" -> 0x00b207d0(entity+0xc68, 3.5,
//      distance, 1) - NOT opened, purpose/units of the literal 3.5 genuinely
//      OPEN (Sec18.6's own words), not assumed here to be a [0,1] throttle
//      fraction. Then unconditionally calls 0x00a92ab0(...,0), "likely a
//      third command, set brake/handbrake 0" (Sec18.6's own "likely", not a
//      fully confirmed read) - ported below as brake=0 whenever not stuck
//      at a graph dead end.
//
// WHAT THIS FILE PORTS AS CONFIRMED: the dot-product-gated cos(30deg)/
// 5.0-unit alignment-AND-distance test picking between throttle=0
// (far/misaligned) and a driving throttle (close/aligned); brake=0 outside
// the dead-end case.
//
// STEERING MAGNITUDE (REVISED AGAIN 2026-10-11, same overtake-gate-18.6b
// task, after Sec18.6's own "Secondary item" was itself RESOLVED): the
// first merge read step 3 as "a full +-1.0, not proportional" and sent that
// literal value onward every tick. A live 900-frame playtest showed this is
// a real, visible regression once combined with Sec18.6a's bit-2 sign
// (which, for an all-same-direction ambient population, never gets
// rewritten after spawn per Sec18.6b's own root-cause finding above) -
// every affected vehicle locks onto ONE constant full-lock turn for the
// entire run. A SECOND unit's interim fix replaced that literal +-1.0 with
// a dot-product-scaled stand-in (`clamp(1.0 - dot, 0, 1)`), explicitly
// flagged as NOT a claim about the real curve. Sec18.6's own curve
// evaluator (`0x00ae5290`, `driverInput+0x20`) is now RESOLVED (see
// traffic_lane_detail::ApplySteeringCurve() above for the full CONFIRMED
// mechanics: airborne early-out, sign negation, clamp, 2-segment
// piecewise-linear response, sign reapplied) - this file now runs that
// REAL curve mechanism rather than the plain linear stand-in.
//
// ONE THING THE REAL MECHANICS CANNOT FIX BY THEMSELVES: Sec18.6's own text
// is explicit that the dispatcher's real x-input to this curve is the
// ALREADY-CLAMPED +-1.0 SIGN itself (`0x00b47490`/`0x00b2bbc0`'s output),
// and the curve's own second-slope formula is derived SPECIFICALLY so the
// curve lands at exactly +-1.0 when |x|=1.0 - i.e. feeding the curve a
// literal +-1.0 input, as `state.passSideFlag`'s own bit-to-sign mapping
// produces, is mathematically DEGENERATE: ANY threshold/slope choice still
// outputs exactly +-1.0, reintroducing the very full-lock regression this
// task exists to fix. This file does NOT feed the curve that literal sign.
// CHOSEN (same spirit as the replaced stand-in, now reshaped through the
// real curve instead of a plain linear map): the curve's x-input is
// `state.passSideFlag`'s own real sign multiplied by this file's own
// already-computed heading-to-lookahead-target alignment error
// (`1.0 - clamp(dot, -1, 1)`, step 1's `dot` - 0.0 when already aimed at
// the target, 1.0 when aimed away from it) - a continuous, non-saturated
// signal the real dispatcher's own confirmed +-1.0 sign cannot supply, so
// the curve's threshold/slope shape has something non-trivial to act on.
// Reported plainly as a CHOSEN input choice (what feeds the curve), NOT a
// claim about 0x00b47490's own real x-input, which Sec18.6's own text does
// not resolve either way. `state.passSideFlag`'s own sign is UNCHANGED and
// still the sole sign source, consistent with Team A's "no parallel sign
// system" finding - only how that sign combines with magnitude changed.
//
// A SECOND FLAGGED AMBIGUITY, NOT INVENTED AROUND: step 4's own "distance"
// operand (fed into the 5.0-unit gate alongside the dot product) is never
// pinned down by Sec18.6 to a specific pair of points - 0x00abaf10 (the
// magnitude helper) was not opened. This file's FIRST attempt used
// distance-to-the-lookahead-TARGET (item 4's own cached point, often ~
// lookaheadDistance away by construction) - this produced a genuine,
// observable deadlock under live playtest verification (a vehicle whose
// lookahead target sits ~15 units ahead can never satisfy a <=5.0-unit
// gate without already having driven closer, but it cannot drive closer
// without passing the gate first). Corrected reading, used below: the 5.0-
// unit distance is measured to the walk's own ANCHOR point (state's
// current graph position, i.e. "how far off the lane/graph is the vehicle
// right now"), decoupled from how far out the STEERING aim point (the
// lookahead target) sits - "close to the road and roughly aimed at where
// the lane goes" drives; "off the road, or aimed the wrong way" does not.
// This is this file's own documented, corrected resolution of a genuinely
// open spec detail, not a claim that the real operand is confirmed to be
// anchor-distance either.
//
// ============================================================================
// 5a. THE STEERING-SIGN SOURCE (Sec18.6a/Sec18.6b) - REWRITTEN AGAIN
//     2026-10-11 (overtake-gate-18.6b task), replacing the first merge's
//     CHOSEN distance/alignment/closing-speed thresholds with Sec18.6b's
//     real 12-step gate (0x00b47f10, fully reconstructed, real constants
//     read directly off their data addresses).
// ============================================================================
// Sec18.6a (unchanged by this task): `+0xe09` bit 2 is a DYNAMIC "which side
// to pass the other vehicle on" flag, only OVERWRITTEN while the gate passes
// - on any tick the write doesn't run, the bit keeps whatever it was last set
// to. This codebase's near-field ambient-traffic model has no `+0xc69`-shaped
// drive-state field (CHOSEN bypass, unchanged: the gate below runs every
// tick, same "no equivalent field -> neutral bypass" convention as
// PassesLaneMask()'s maskInclude/maskExclude defaults).
//
// ORCHESTRATOR COURSE-CORRECTION (2026-10-11, mid-task, two relayed Team A
// findings, BOTH independently verified against the spec text actually read
// before acting on them, per this project's own "relayed technical claims
// get verified, not dismissed" rule):
//   1. Team A traced that bit 2 is the ONLY steering-sign source in the real
//      game, always - 0x00b47490 step 2 always reads it, with NO second,
//      parallel "ordinary lane-keeping sign" computation anywhere. VERIFIED
//      against Sec18.6's own text (0x00b47490's step 2 is unconditional, and
//      the 0xe/0x10-state sibling path 0x00b2bbc0 "picks a sign" the same
//      way, not a documented second mechanism). CONCLUSION: this file does
//      NOT build a second, parallel, lookahead-derived steering-SIGN system
//      for "when the overtake gate isn't active" - state.passSideFlag,
//      unconditionally, is still the only sign source below, exactly as
//      Sec18.6a already specified. The original task brief's item 4
//      ("fall back to lookahead-based SIGN when the gate doesn't fire") is
//      NOT implemented as a second sign system because Team A's trace says
//      no such real second system exists - implementing one would be the
//      spec DEVIATION, not a faithful port.
//   2. A second relayed message hypothesized the real MAGNITUDE write is
//      `f(heading-dot-to-lookahead-target) * sign(bit2)` - a single combined
//      float sent to 0x00ae5290. CHECKED against Sec18.6's own disassembly
//      text (re-read in full for this task, not re-summarized): `0x00a929e0`
//      is called with exactly ONE float parameter (`signFloat`, already
//      clamped to [-1,1]), which is the SAME scalar `0x00ae5290` receives as
//      its own only numeric parameter - no second magnitude value is passed
//      in from 0x00b47490 anywhere in the confirmed body. **This hypothesis
//      is NOT supported by the opened disassembly and is reported here as a
//      discrepancy, not silently adopted:** the real magnitude transform (if
//      any) happens INSIDE 0x00ae5290's own curve function (parameterized by
//      a struct at `this+0x20`), which Sec18.6's own "Secondary item" already
//      flags OPEN - not independently proven at the evaluator level, and
//      NOT reopened by this task (this project's clean-room rule still
//      forbids Team B from tracing it). So neither "literal full-lock +-1.0"
//      (the first merge's reading) nor "heading-dot-scaled magnitude" (the
//      interjection's hypothesis) is a CONFIRMED port of the real curve - both
//      are CHOSEN stand-ins for a function this project cannot open. See the
//      "STEERING MAGNITUDE, CHOSEN FIX" note under item 5 below for which one
//      this file now uses and exactly why, stated as an engineering
//      simplification to resolve an observed regression, not a spec claim.
//
// `0x00b47f10`'s real 12-step gate (Sec18.6b, fully reconstructed - see that
// section for the complete step list, every constant independently
// re-verified against the live Ghidra server before being spliced into the
// spec): this file ports Sec18.6b's own "Drop-in guidance" minimal shape,
// APPROXIMATED further where this codebase's data model has no equivalent
// field, documented plainly per step:
//   (a) PORTED VERBATIM: forward-facing-divergence gate, `dot(ownFwd,
//       otherFwd) <= 0.0` (Sec18.6b step 3's own oncoming/crossing-only
//       test, constant 0x0125e270=0.0 reused exactly). REQUIRES a forward
//       vector for the candidate, which NearFieldVehicle itself has no
//       field for (position-only struct) - the CALLER (tools/sr3_viewer.cpp)
//       resolves this from its own per-vehicle heading side-car map and
//       threads it into OvertakeCandidate::headingRad (extended by this
//       task - see that struct's own doc comment).
//   (b) BYPASSED, not approximated: Sec18.6b step 4's own absolute own-
//       vehicle half-extent floor (>=64.0 squared, i.e. >=8 linear units).
//       NearFieldVehicle/OvertakeCandidate model NO bounding-extent field at
//       all (unchanged from the first pass's own same gap) - there is
//       nothing to compare against, so this step is treated as
//       always-satisfied, the SAME neutral-bypass convention already used
//       for the `+0xc69` drive-state precondition above, not a silent drop.
//   (c) PORTED VERBATIM (mechanism AND constant): Sec18.6b steps 6-7's own
//       speed-extrapolated forward closing-distance floor -
//       `predicted = pos + fwd*speed` for both vehicles (step 6, no
//       dt-scaling in the spec's own text, ported literally even though
//       this makes `speed` an implicit "per-tick" unit rather than a true
//       per-second rate - Sec18.6b does not resolve this unit ambiguity and
//       this file does not invent a dt-scaling the spec never states), then
//       `closingProjection = dot(predictedOther - predictedSelf, ownFwd) >=
//       3.0` (step 7, constant 0x01116444=3.0 reused exactly, REQUIRED).
//   (d)+(e) APPROXIMATED, NOT a literal port, flagged plainly: Sec18.6b's
//       real steps 8-10 compute `sizeDelta = otherHalfWidth - ownHalfExtent`
//       then use it twice - once for an absolute squared-distance cap (81.0,
//       step 9) on a "pass point" built from sizeDelta, once for the FINAL
//       dimensionless ratio test (`0.5 <= closingProjection /
//       clamp(fwdComponent*1.8, 0.1, inf) <= 1.0`, step 10) where
//       fwdComponent is itself derived from sizeDelta. Neither half-width
//       NOR half-extent is modelled anywhere in this codebase (same gap as
//       step (b)) - with sizeDelta forced to the zero vector (the only
//       value this file CAN supply without inventing a vehicle-size
//       number), fwdComponent collapses to 0.0, which clamps the step-10
//       denominator to its 0.1 floor and makes `finalRatio` enormous for
//       any closingProjection satisfying step (c) - i.e. step (e)'s real
//       ratio test would ALWAYS fail under this file's own zero-sizeDelta
//       substitution, reintroducing exactly the "structurally never fires"
//       bug Sec18.6b's own drop-in guidance was written to fix. Rather than
//       port a formula that is known in advance to degenerate to "never
//       passes" given this codebase's missing fields, this file substitutes
//       ONE combined approximate check in place of both (d) and (e): the
//       planar squared distance between the two vehicles' SAME predicted
//       positions already computed for step (c) must be `<= 81.0` (step 9's
//       own 9-unit-radius constant, 0x01117da8, reused numerically - the
//       only piece of (d)/(e) this file can still apply meaningfully without
//       half-width data). This is WEAKER than the real ratio test (any
//       closing pair within the radius passes, not just one in the real
//       0.5-1.0 ratio band) and is reported here as an open approximation,
//       not hidden inside the code.
//   step 11 (leadHandle-gated `0x00dab5e0>=0.25`, OPEN helper): SKIPPED
//       outright, per Sec18.6b's own explicit allowance - this codebase has
//       no "lead vehicle" handle-resolution concept anywhere (leadHandle
//       never resolves), which Sec18.6b itself calls "a normal 'no lead
//       vehicle' case, not an error path."
//   step 1 (the `+0xe08`/`+0xe0c` early-out) and step 2 (leadHandle
//       resolution itself): not portable (no equivalent fields), same
//       neutral-bypass convention as (b) above.
//
// Sec18.6b's own ROOT-CAUSE finding, carried over and expected to apply
// here too: "If a 900-frame test never saw step 3 (forward dot <= 0)
// succeed - i.e. all observed traffic was same-direction, non-crossing -
// the gate was structurally guaranteed never to fire." This file's own
// live 900-frame re-verification (see HANDOFF.md / commit message for this
// task) reports that number plainly, honest-null-result or not.
//
// DEFAULT WHEN NO OVERTAKE IS ACTIVE: unchanged from the first pass - CHOSEN
// `false` at `LaneFollowState::Begin()`. Sec18.6b does not give a real
// initial-value setter either (not retraced by this task; Team A offered to
// trace it separately, flagged here as still OPEN rather than resolved).
//
// BIT-TO-STEER-SIGN MAPPING: unchanged, CHOSEN, arbitrary but documented:
// passSideFlag==false -> steer sign +1.0, passSideFlag==true -> steer sign
// -1.0 (see "STEERING MAGNITUDE, CHOSEN FIX" under item 5 below for how this
// sign now COMBINES with magnitude, which is the actual fix for the
// full-lock regression - the sign mapping itself did not need to change).
//
// The driving throttle VALUE for the "close/aligned" branch is also CHOSEN
// (kDrivingThrottle below) - 3.5's own real units are OPEN per Sec18.6, so
// it is not reused numerically.
//
// The real "3.5"/distance-gate CONSTANTS Sec18.6 DOES give numerically
// (cos(30deg), 5.0 world units) ARE reused verbatim below, not re-chosen.
// The overtake-gate's own constants are now Sec18.6b's REAL numbers (0.0,
// 3.0, 81.0 above) wherever portable, and explicitly-flagged approximations
// only where this codebase's missing half-width/half-extent/lead-handle
// fields make the real formula degenerate - not CHOSEN-in-full as the first
// pass's three guessed thresholds were.
//
// PHYSICS INTEGRATION - unchanged from the first pass, still the one real
// write surface (Sec18.3). This file does not itself depend on sr3physics
// (matching navpoint_selector.h/cover_node_selector.h's own "standalone,
// decoupled module" convention) - it produces a LaneFollowCommand{steer,
// throttle,brake} struct in the IDENTICAL [-1,1]/[0,1] domains
// sr3physics::DriverInput already uses; a caller constructs a real
// sr3physics::DriverInput from it and feeds VehicleSubstepSimulator::Step()
// - the one real integration point Sec18.3 confirms exists, and the exact
// mechanism the task brief asks this file reuse rather than invent a second
// input path for.
//
// SYNTHETIC-ONLY, SAME CONVENTION AS navpoint_selector.h/
// cover_node_selector.h. This file's own tests build sr3zone::TrafficRecord
// fixtures from hand-written bytes via sr3zone::TrafficGraph::parseAt() -
// never from real shipped `.czn_pc` content, which this clean-room project
// does not read or possess.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "sr3zone/road_graph.h"
#include "sr3zone/traffic_graph.h"

namespace sr3airuntime {

inline constexpr double kLaneFollowPi = 3.14159265358979323846;
inline constexpr double kLaneFollowTwoPi = 6.28318530717958647692;

// CHOSEN (world units): two segment endpoints within this distance of each
// other are considered "the same graph node" by TrafficLaneGraph's own
// adjacency rule - see file header comment item 3, unchanged from the
// first pass.
inline constexpr double kJunctionEpsilon = 0.01;

// Sec18.1's own mask-test shape, neutral defaults - see file header comment
// item 1 for why the real bit VALUES are out of this task's scope.
inline constexpr uint32_t kMaskIncludeAll = 0xFFFFFFFFu;
inline constexpr uint32_t kMaskExcludeNone = 0x0u;

// Sec18.6 step 4's own CONFIRMED numeric constants (cos(30deg), 5.0 world
// units) - reused verbatim, NOT CHOSEN.
inline constexpr double kAlignmentCosineThreshold = 0.86602540378443864676;  // cos(30 degrees)
inline constexpr double kAlignmentDistanceThreshold = 5.0;

// Sec18.6b's own real overtake/pass-side gate (0x00b47f10, fully
// reconstructed) - see file header item 5a for the full step-by-step
// mapping and which steps are PORTED VERBATIM vs APPROXIMATED vs BYPASSED.
// Replaces the first pass's three fully-CHOSEN distance/alignment/
// closing-speed thresholds.
inline constexpr double kOvertakeGateForwardDivergence = 0.0;    // step (a)/3 - 0x0125e270, PORTED VERBATIM: dot(ownFwd,otherFwd) must be <= this
inline constexpr double kOvertakeGateClosingDistanceFloor = 3.0; // step (c)/7 - 0x01116444, PORTED VERBATIM: closingProjection must be >= this
inline constexpr double kOvertakeGatePassPointDistanceSq = 81.0; // steps (d)+(e)/9-10 - 0x01117da8 (9^2), APPROXIMATED single combined check, see item 5a

// Sec18.6's own "Secondary item" (RESOLVED 2026-10-11, overtake-gate-18.6b
// task) - the `driverInput+0x20` 2-float `{threshold, slope}` CONFIRMED to
// parameterize 0x00ae5290's inline curve.
// Sec18.6b: real values, CONFIRMED - one universal, hardcoded constructor
// (0x00aba420) called unconditionally for every vehicle's driverInput, no
// .xtbl read, no per-vehicle-type variation. Since the consumer clamps
// input magnitude to [0,1] before evaluating the curve, threshold=1.0
// makes the above-threshold branch in ApplySteeringCurve() below
// structurally unreachable - the curve degenerates to a pure identity
// pass-through, output = |input|.
inline constexpr double kSteerCurveThreshold = 1.0;
inline constexpr double kSteerCurveSlope = 1.0;

namespace traffic_lane_detail {
inline double Dist2D(const sr3zone::TrafficPoint& a, const sr3zone::TrafficPoint& b) {
    // Y (vertical) deliberately excluded - this project's own planar
    // vehicle model (sr3physics/vehicle_substeps.h: "ground plane = world
    // X/Z") has no meaningful vertical state to compare against.
    const double dx = static_cast<double>(a.x) - static_cast<double>(b.x);
    const double dz = static_cast<double>(a.z) - static_cast<double>(b.z);
    return std::sqrt(dx * dx + dz * dz);
}
inline double Dist2D(double ax, double az, const sr3zone::TrafficPoint& b) {
    const double dx = static_cast<double>(b.x) - ax;
    const double dz = static_cast<double>(b.z) - az;
    return std::sqrt(dx * dx + dz * dz);
}

// Sec18.6's own "Secondary item" curve (RESOLVED 2026-10-11, 0x00ae5290,
// fully inline - no separate evaluator function exists in the real game).
// PORTED mechanics, CONFIRMED by direct disassembly re-opened for that spec
// pass:
//   1. an airborne-gated early-out: 0x00ae5290 reads an airborne/grounded
//      pair off a sub-object at `this+0x150` (same shape as Sec18.6a's
//      0x00ad3820) and returns immediately, WRITING NOTHING, when airborne.
//      This codebase's near-field ambient-vehicle population
//      (NearFieldVehicle, near_field_vehicle_population.h) carries no
//      grounded/airborne field at all - CHOSEN bypass, same "no equivalent
//      field -> neutral bypass" convention PassesLaneMask() already uses:
//      `airborne` is always treated as false, the curve always runs. A
//      real caller that DOES track airborne state for a given vehicle
//      should check it BEFORE calling this function and skip the call
//      entirely (equivalent to "write nothing") rather than pass true here
//      (this function does not itself implement a "write nothing" return
//      path - there is nothing to leave unwritten in this stateless port).
//   2. the input sign is NEGATED first (the stored convention inside the
//      real function is inverted relative to its caller's own sign).
//   3. clamped to [-1,1].
//   4. (unless a bit on `this+0x164` is set - this codebase models no such
//      bit, CHOSEN bypass: the curve always runs, same convention as (1))
//      a 2-segment piecewise-linear response: for |x| <= threshold,
//      output = slope * |x|; for |x| > threshold, a second slope
//      `(1.0 - slope*threshold) / (1.0 - threshold)` continues the ramp so
//      the curve lands exactly at 1.0 when |x| = 1.0.
//   5. sign reapplied, result clamped to [-1,1] again.
// `threshold`/`slope` are the real CONFIRMED values (kSteerCurveThreshold/
// kSteerCurveSlope above, Sec18.6b) - not CHOSEN. The real `driverInput+0x20`
// config struct turns out not to be a per-vehicle config table at all: one
// hardcoded constructor builds it identically for every vehicle.
inline double ApplySteeringCurve(double signedInput, double threshold, double slope, bool airborne) {
    if (airborne) return 0.0;  // see step 1 above - not reachable while the bypass holds `airborne` false
    double x = -signedInput;           // step 2
    x = std::clamp(x, -1.0, 1.0);       // step 3
    const double sign = (x < 0.0) ? -1.0 : 1.0;
    const double mag = std::fabs(x);
    double out;
    if (mag <= threshold) {
        out = slope * mag;
    } else {
        const double secondSlope = (1.0 - slope * threshold) / (1.0 - threshold);
        out = slope * threshold + secondSlope * (mag - threshold);
    }
    return std::clamp(sign * out, -1.0, 1.0);  // step 5
}
}  // namespace traffic_lane_detail

// One segment's own decoded lane geometry - built once per
// sr3zone::TrafficRecord by TrafficLaneGraph's own constructor, not
// re-decoded per tick.
struct TrafficLaneSegment {
    size_t index = 0;
    uint8_t kind = 0;  // sr3zone::TrafficSegmentKind() - CONFIRMED extractor (bits 28-29 of +0x1c)
    uint32_t rawKindField = 0;  // the full +0x1c word - Sec18.1's own maskInclude/maskExclude test
                                // reads the FULL field, not just the 2-bit kind; kept verbatim
                                // alongside the already-extracted `kind` rather than re-deriving it.
    std::vector<sr3zone::TrafficPoint> polyline;  // sr3zone::DecodeSegmentPointList() - may be empty
};

// Builds the lane graph once from a parsed sr3zone::TrafficRecord: decodes
// every segment's own CSR polyline, then derives CHOSEN adjacency (see file
// header comment item 3) between segments whose endpoints geometrically
// coincide.
class TrafficLaneGraph {
public:
    explicit TrafficLaneGraph(const sr3zone::TrafficRecord& record) {
        segments_.reserve(record.extendedSegments.size());
        for (size_t i = 0; i < record.extendedSegments.size(); ++i) {
            TrafficLaneSegment seg;
            seg.index = i;
            seg.kind = sr3zone::TrafficSegmentKind(record.extendedSegments[i]);
            seg.rawKindField = record.extendedSegments[i].rawFieldAt0x1c;
            seg.polyline = sr3zone::DecodeSegmentPointList(record.extendedSegments, i, record.pointPool);
            segments_.push_back(std::move(seg));
        }
        BuildAdjacency();
    }

    // Built from the REAL parsed road graph (sr3zone::RoadGraph, spec-zone-
    // data-format.md Sec13.2a): each lane's polyline is its decoded point list
    // (node A, interior shape points, node B), `rawKindField` is the full
    // segment flags word, and adjacency is the ON-DISK successor list
    // (spec-ai-runtime.md Sec20.6.6 "use the on-disk links"), not the
    // geometric-coincidence rule of the record-based constructor above.
    explicit TrafficLaneGraph(const sr3zone::RoadGraph& road) {
        segments_.reserve(road.segments.size());
        adjacency_.resize(road.segments.size());
        for (size_t i = 0; i < road.segments.size(); ++i) {
            TrafficLaneSegment seg;
            seg.index = i;
            seg.rawKindField = road.segments[i].flags;
            seg.kind = static_cast<uint8_t>((road.segments[i].flags >> 28) & 0x3u);  // Sec13.2a(d): bits 28-29, 0 on disk
            const auto poly = road.SegmentPolyline(i);
            seg.polyline.reserve(poly.size());
            for (const auto& p : poly) seg.polyline.push_back(sr3zone::TrafficPoint{p.x, p.y, p.z});
            segments_.push_back(std::move(seg));
            for (uint16_t s : road.Successors(i)) adjacency_[i].push_back(s);
        }
    }

    size_t segmentCount() const { return segments_.size(); }
    const TrafficLaneSegment& segment(size_t i) const { return segments_.at(i); }

    // CHOSEN adjacency - see file header comment item 3. Empty if
    // `segmentIndex` is out of range or its own polyline is empty (no exit
    // point to match against).
    const std::vector<size_t>& NextSegments(size_t segmentIndex) const {
        static const std::vector<size_t> kEmpty;
        if (segmentIndex >= adjacency_.size()) return kEmpty;
        return adjacency_[segmentIndex];
    }

private:
    void BuildAdjacency() {
        adjacency_.assign(segments_.size(), {});
        for (size_t i = 0; i < segments_.size(); ++i) {
            if (segments_[i].polyline.empty()) continue;
            const sr3zone::TrafficPoint& exitPoint = segments_[i].polyline.back();
            for (size_t j = 0; j < segments_.size(); ++j) {
                if (i == j || segments_[j].polyline.empty()) continue;
                const sr3zone::TrafficPoint& entryPoint = segments_[j].polyline.front();
                if (traffic_lane_detail::Dist2D(exitPoint, entryPoint) <= kJunctionEpsilon) {
                    adjacency_[i].push_back(j);
                }
            }
        }
    }

    std::vector<TrafficLaneSegment> segments_;
    std::vector<std::vector<size_t>> adjacency_;
};

// Sec18.1's own mask test (CONFIRMED shape, see file header item 1):
// "(maskInclude & segmentKindFlags) != 0 && (maskExclude & segmentKindFlags)
// == 0". Applied against the FULL +0x1c word (rawKindField), the same field
// Sec18.1 names, not just the already-narrowed 2-bit `kind`.
//
// CHOSEN NEUTRAL-DEFAULT BYPASS, documented plainly: the literal formula
// above degenerates against a segment whose own rawKindField is entirely
// zero - `(anything & 0) != 0` is always false, so a real all-zero-flags
// segment could never pass ANY maskInclude value under the bare formula,
// including the neutral kMaskIncludeAll/kMaskExcludeNone defaults this file
// uses when no real per-vehicle-class mask data is available (file header
// item 1 - no lane-class xtbl is wired up anywhere in this codebase's
// near-field ambient traffic path). Real shipped segments most plausibly
// always carry SOME nonzero flag bit (this project has no shipped data to
// check), but this file's OWN synthetic fixtures are free to use a literal
// 0 for "no flags set" - so the neutral-default case is special-cased to
// bypass the AND test entirely (treated as "no restriction requested"),
// while the REAL confirmed AND-based test is still applied verbatim
// whenever a caller supplies an actual non-neutral mask.
inline bool PassesLaneMask(const TrafficLaneSegment& seg, uint32_t maskInclude, uint32_t maskExclude) {
    if (maskInclude == kMaskIncludeAll && maskExclude == kMaskExcludeNone) return true;
    return (maskInclude & seg.rawKindField) != 0 && (maskExclude & seg.rawKindField) == 0;
}

// Sec18.2/Sec18.5's own nearest-distance junction choice (CONFIRMED shape,
// see file header item 2) - REPLACES the first pass's uniform-random pick
// outright. Among `candidates` (already mask-filtered by the caller), picks
// the one whose own first DISTINGUISHING polyline point (index 1, or index 0
// if the candidate has only a single point - see file header item 2's
// second judgment call) is nearest to (originX,originZ) - "prefer the legal
// branch requiring the shortest net advance from here", this file's own
// documented reading of 0x00b46fc0's "resulting target point's distance".
// Returns nullopt if `candidates` is empty.
inline std::optional<size_t> ChooseNextSegmentByNearestDistance(const TrafficLaneGraph& graph,
                                                                  const std::vector<size_t>& candidates,
                                                                  double originX, double originZ) {
    if (candidates.empty()) return std::nullopt;
    std::optional<size_t> best;
    double bestDist = 0.0;
    for (size_t c : candidates) {
        const TrafficLaneSegment& seg = graph.segment(c);
        if (seg.polyline.empty()) continue;
        const sr3zone::TrafficPoint& distinguishing = seg.polyline.size() > 1 ? seg.polyline[1] : seg.polyline[0];
        const double d = traffic_lane_detail::Dist2D(originX, originZ, distinguishing);
        if (!best || d < bestDist) {
            best = c;
            bestDist = d;
        }
    }
    return best;
}

// Sec18.6a's own "second record" fed into the overtake/pass-side gate
// (0x00b47f10) and the bit-2 setter's own dot product (0x00b47c80) - the
// real second record's identity is Sec18.6a's own OPEN item (not resolved
// to any specific object by the spec). STAND-IN, not this file's own
// invention: purrsian-52 directed using the nearest near-field vehicle
// AHEAD of the follower as this second record - a caller (e.g.
// tools/sr3_viewer.cpp's playtest loop) resolves that candidate among its
// own live near-field-vehicle population (this header has no population
// concept of its own) and passes it in here each tick.
//
// EXTENDED 2026-10-11 (overtake-gate-18.6b task): `headingRad`/`speedMs`
// added so Sec18.6b's real step (a) (forward-divergence) and step (c)
// (speed-extrapolated closing distance) are computable at all - the first
// pass's position-only struct could not support either real step.
// NearFieldVehicle itself still carries neither field (position-only, per
// near_field_vehicle_population.h); the CALLER threads these in from its
// own per-vehicle side-car tracking (tools/sr3_viewer.cpp already keeps
// nearFieldVehicleHeading/nearFieldVehicleSpeed maps for exactly this
// purpose - see ResolveNearestNearFieldVehicleAhead()'s own updated
// signature there). Defaults (0.0 heading, 0.0 speed) make a
// default-constructed candidate read as "stationary, facing +X" - callers
// that cannot supply real values should prefer `std::nullopt` instead of a
// default-constructed candidate, same as before.
struct OvertakeCandidate {
    double x = 0.0;
    double z = 0.0;
    double headingRad = 0.0;  // NEW: the candidate's own forward-axis heading, same convention as vehicleHeadingRad below
    double speedMs = 0.0;     // NEW: the candidate's own current speed (world units/sec) - feeds step (c)'s predicted-position extrapolation
};

// Cursor state for one vehicle following the lane graph, PLUS Sec18.5's own
// cached-target distance-throttle fields (item 4) - the re-query gate that
// decides whether a tick re-runs the masked walk + junction pick at all, or
// just keeps steering at the already-cached target.
struct LaneFollowState {
    bool valid = false;  // false once the follower hits a dead end with no next segment (or never started)
    size_t currentSegmentIndex = 0;
    size_t pointCursor = 0;  // index into segment(currentSegmentIndex).polyline

    // Sec18.6a's own `+0xe09` bit 2, "which side to pass the other vehicle
    // on" - a real per-entity flag, dynamically recomputed ONLY while the
    // overtake gate (below, Sec18.6a) passes; otherwise keeps its last
    // value (Sec18.6a's own text: "there is no clear-every-frame or
    // set-once-at-spawn path"). CHOSEN default `false` at Begin() - see
    // file header item 5a, Sec18.6a does not state a true initial value.
    bool passSideFlag = false;

    // Sec18.5's own cache: the position the LAST re-query was computed from,
    // and the LOOKAHEAD-ADVANCED TARGET POINT it produced (the "+0xe38/
    // +0xe40" cache Sec18.5 names) - the point StepLaneFollow() actually
    // steers at, NOT the immediate next polyline point. A fresh tick only
    // re-runs the masked lookahead walk (item 1) + junction pick (item 2)
    // when the vehicle has moved `reQueryDistanceThreshold` away from
    // cachedQueryOriginX/Z, OR the cached target has already been reached,
    // OR this is the very first tick (hasCachedQuery == false) - Sec18.5's
    // own "only when the cached point is stale or the vehicle has moved far
    // enough away" condition.
    bool hasCachedQuery = false;
    double cachedQueryOriginX = 0.0;
    double cachedQueryOriginZ = 0.0;
    sr3zone::TrafficPoint cachedTarget;
    bool cachedTargetIsDeadEndShort = false;  // the cached lookahead walk fell short of lookaheadDistance at a real dead end

    static LaneFollowState Begin(const TrafficLaneGraph& graph, size_t startSegmentIndex) {
        LaneFollowState s;
        if (startSegmentIndex < graph.segmentCount() && !graph.segment(startSegmentIndex).polyline.empty()) {
            s.valid = true;
            s.currentSegmentIndex = startSegmentIndex;
            s.pointCursor = 0;
        }
        return s;
    }
};

// Tunables - see file header comment item 5 for which of these are CONFIRMED
// numeric constants (kept as top-level inline constexpr above, not here)
// vs genuinely CHOSEN.
struct LaneFollowConfig {
    double arrivalRadius = 1.5;  // CHOSEN (world units) - a target point counts as "reached" within this distance
    double lookaheadDistance = 15.0;  // CHOSEN (world units) - Sec18.1's own lookaheadDist parameter; no real
                                       // shipped value recovered, this is this file's own default
    // Sec18.5's re-query distance-throttle threshold. FINAL (Sec20.6.3(d), RG-12): 15.0 m, compared linearly
    // (0x01117a60, a double; not squared). A re-plan is also forced at drift > (... + 20 m) (0x01116998): not
    // modelled here (the walk anchor is advanced at every re-query).
    // CHOSEN (question to Team A): lookaheadDistance above stays 15 although Sec20.6.3(d) gives a 5000 m route
    // walk; here the walk target IS the steer point, so 5000 m would aim the car across the map.
    double reQueryDistanceThreshold = 15.0;
    uint32_t maskIncludeBits = kMaskIncludeAll;
    uint32_t maskExcludeBits = kMaskExcludeNone;
    // Sec18.6 step 4's own CHOSEN driving-throttle value for the
    // "close/aligned" branch - the real 3.5 literal's units are OPEN
    // (Sec18.6: "not opened"), so this is NOT a reuse of that number.
    double drivingThrottle = 0.5;
};

// What this tick's lane-follow step decided - feed steer/throttle/brake
// into a real sr3physics::DriverInput (see file header comment's "PHYSICS
// INTEGRATION" section); the rest is diagnostic/trace-output only.
struct LaneFollowCommand {
    bool stuck = false;            // true at a dead end (no valid next segment) - steer/throttle are 0, brake is 1
    bool advancedSegment = false;  // true the tick the follower hopped segments (diagnostic)
    bool requeried = false;        // true the tick the cached-target throttle (item 4) actually re-ran the walk (diagnostic)
    bool aligned = false;          // Sec18.6 step 4's own alignment-AND-distance gate result (diagnostic)
    // RG-12 follow-up: the heading half of that gate alone (dot >= cos 30 deg). The 5.0 distance half is measured from
    // the last plan (CHOSEN reading above); with the spec 15 m re-query it would halt every car 5 m after each plan,
    // so the real-graph viewer path drives on this heading-only flag (CHOSEN, question to Team A: what is the 5.0?).
    bool headingAligned = false;
    bool overtakeGateFired = false;  // Sec18.6b's real gate result this tick (diagnostic) - true only when state.passSideFlag was actually rewritten
    double steer = 0.0;            // Sec18.6: CONFIRMED full-lock +-1.0 (or 0 when stuck) - feed into sr3physics::DriverInput::steer
    double throttle = 0.0;         // [0,1]  - feed into sr3physics::DriverInput::throttle
    double brake = 0.0;            // [0,1]  - feed into sr3physics::DriverInput::brake
    sr3zone::TrafficPoint target;  // the point this tick aimed at (meaningless if `stuck`) - diagnostic/trace output
};

// Result of one masked lookahead walk (Sec18.1/item 1) - either a real
// lookahead-advanced target point, or a report that the walk ran off a dead
// end before accumulating `lookaheadDistance` (a genuine graph dead end,
// Sec18.1 never describes what happens here - this file's own CHOSEN
// handling: return however far the walk got, flagged, rather than failing
// outright - a vehicle approaching a dead end should still aim at the real
// last reachable point, not freeze).
struct LookaheadWalkResult {
    bool ok = false;               // false only if the walk could not even start (bad segment/empty polyline)
    bool ranIntoDeadEnd = false;   // true if the walk fell short of lookaheadDistance at a real dead end
    size_t endSegmentIndex = 0;    // the segment the walk's own anchor ended on
    size_t endPointCursor = 0;     // the polyline index within endSegmentIndex the walk's own anchor ended on
    sr3zone::TrafficPoint target;  // the lookahead-advanced (or dead-end-short) target point
    bool advancedSegment = false;  // true if the walk hopped at least one segment (diagnostic)
};

// Sec18.1's own CONFIRMED mechanism, ported: walks the point chain FORWARD
// from (startSegmentIndex, startPointCursor), accumulating the along-path
// distance between successive polyline points, until the accumulated
// distance reaches/exceeds `lookaheadDistance` - at which point it returns
// the point it stopped at as the target (interpolating between the last two
// points it walked between, so the returned distance is not overshot by a
// whole point-to-point gap - CHOSEN refinement, Sec18.1 does not specify
// whether the real function interpolates or snaps to the nearest point;
// interpolating is the more defensible reading of "advance exactly
// lookaheadDist"). Junction hops (segment exhausted) apply item 1's mask
// filter then item 2's nearest-distance pick, exactly as the anchor-advance
// loop in StepLaneFollow() below does - this is the SAME junction logic,
// just run in a disposable "preview" walk rather than mutating the real
// cursor.
inline LookaheadWalkResult WalkLookahead(const TrafficLaneGraph& graph, size_t startSegmentIndex,
                                          size_t startPointCursor, double lookaheadDistance, uint32_t maskIncludeBits,
                                          uint32_t maskExcludeBits) {
    LookaheadWalkResult result;
    if (startSegmentIndex >= graph.segmentCount() || graph.segment(startSegmentIndex).polyline.empty()) {
        return result;
    }
    result.ok = true;

    size_t segIdx = startSegmentIndex;
    size_t cursor = startPointCursor;
    // The walk's own "current position" - starts at the anchor's own point
    // (or the segment's first point if the anchor is already past its own
    // polyline, e.g. freshly hopped to index 1 of a 1-point segment).
    const TrafficLaneSegment* seg = &graph.segment(segIdx);
    sr3zone::TrafficPoint cur =
        (cursor < seg->polyline.size()) ? seg->polyline[cursor] : seg->polyline.back();
    result.target = cur;

    double remaining = lookaheadDistance;
    size_t guard = graph.segmentCount() * 4 + 8;  // bounded - see StepLaneFollow()'s own identical guard note
    while (remaining > 0.0 && guard-- > 0) {
        seg = &graph.segment(segIdx);
        if (cursor >= seg->polyline.size()) {
            // Exhausted this segment's own polyline - hop, same mask+
            // nearest-distance junction logic as StepLaneFollow()'s own
            // anchor-advance loop.
            const std::vector<size_t>& rawCandidates = graph.NextSegments(segIdx);
            std::vector<size_t> legal;
            legal.reserve(rawCandidates.size());
            for (size_t c : rawCandidates) {
                if (PassesLaneMask(graph.segment(c), maskIncludeBits, maskExcludeBits)) legal.push_back(c);
            }
            std::optional<size_t> chosen = ChooseNextSegmentByNearestDistance(graph, legal, cur.x, cur.z);
            if (!chosen) {
                result.ranIntoDeadEnd = true;
                result.endSegmentIndex = segIdx;
                result.endPointCursor = cursor;
                result.target = cur;
                return result;
            }
            result.advancedSegment = true;
            segIdx = *chosen;
            cursor = 1;  // index 0 is the shared junction point - same convention as StepLaneFollow()
            continue;
        }

        const sr3zone::TrafficPoint& next = seg->polyline[cursor];
        const double step = traffic_lane_detail::Dist2D(cur, next);
        if (step <= remaining || step < 1e-9) {
            // Consume this whole point-to-point leg and keep walking.
            remaining -= step;
            cur = next;
            ++cursor;
            continue;
        }
        // The lookahead distance runs out partway along this leg -
        // interpolate (see this function's own comment above for why).
        const double frac = remaining / step;
        sr3zone::TrafficPoint interp;
        interp.x = static_cast<float>(cur.x + (static_cast<double>(next.x) - cur.x) * frac);
        interp.y = static_cast<float>(cur.y + (static_cast<double>(next.y) - cur.y) * frac);
        interp.z = static_cast<float>(cur.z + (static_cast<double>(next.z) - cur.z) * frac);
        result.endSegmentIndex = segIdx;
        result.endPointCursor = cursor;  // still aiming to reach `next` next tick's anchor-advance
        result.target = interp;
        return result;
    }

    // Loop exited because `remaining` reached 0 exactly on a point boundary
    // (or the guard tripped - treated the same as "we got as far as we
    // could," not a hard failure).
    result.endSegmentIndex = segIdx;
    result.endPointCursor = cursor;
    result.target = cur;
    return result;
}

// Computes ONE tick's steering command and advances `state` in place.
//
// `vehicleX`/`vehicleZ`/`vehicleHeadingRad`: the vehicle's own current
// planar pose, in the SAME convention sr3physics::VehicleInstanceState
// already uses ("ground plane = world X/Z... heading... so
// (cos(heading), sin(heading)) is the chassis forward direction") - a
// caller can pass a live VehicleSubstepSimulator's own state_.posX/posZ/
// heading directly, no conversion needed.
//
// `vehicleSpeedMs`: this vehicle's own current speed (world units/sec) -
// feeds Sec18.6b's real gate step (c)'s speed-extrapolated closing-distance
// test (file header item 5a); CHOSEN default 0.0 for callers that don't
// track speed (predicted position then collapses to the raw position,
// making the closing-distance test strictly harder to pass, not a bypass).
//
// `overtakeCandidate`: Sec18.6a's own "second record", resolved by the
// CALLER (this header has no population concept of its own) - purrsian-52's
// own directed stand-in is the nearest near-field vehicle ahead of this one,
// EXTENDED (this task) to also carry the candidate's own heading/speed so
// Sec18.6b's real gate steps are computable; pass std::nullopt when none is
// nearby/eligible this tick (the gate then never passes and
// `state.passSideFlag` simply keeps its last value, per Sec18.6a's own "no
// clear-every-frame path" text).
inline LaneFollowCommand StepLaneFollow(const TrafficLaneGraph& graph, LaneFollowState& state, double vehicleX,
                                         double vehicleZ, double vehicleHeadingRad,
                                         const LaneFollowConfig& config = LaneFollowConfig{},
                                         double vehicleSpeedMs = 0.0,
                                         std::optional<OvertakeCandidate> overtakeCandidate = std::nullopt) {
    LaneFollowCommand cmd;
    if (!state.valid) {
        cmd.stuck = true;
        cmd.brake = 1.0;
        return cmd;
    }

    // ---- Item 4: the cached-target distance-throttle re-query gate -
    // Sec18.5's own mechanism, now actually gating recomputation of the
    // STEERING TARGET (the lookahead-advanced point, item 1), not merely a
    // diagnostic flag. Re-run WalkLookahead() only when there is no cached
    // target yet, the vehicle has moved beyond `reQueryDistanceThreshold`
    // from where the last query originated, or the vehicle has already
    // reached the cached target. ----
    const double distFromCachedOrigin =
        state.hasCachedQuery ? traffic_lane_detail::Dist2D(
                                    vehicleX, vehicleZ,
                                    sr3zone::TrafficPoint{static_cast<float>(state.cachedQueryOriginX), 0.0f,
                                                           static_cast<float>(state.cachedQueryOriginZ)})
                              : 0.0;
    const double distToCachedTarget =
        state.hasCachedQuery ? traffic_lane_detail::Dist2D(vehicleX, vehicleZ, state.cachedTarget) : 0.0;
    const bool mustRequery = !state.hasCachedQuery || distFromCachedOrigin > config.reQueryDistanceThreshold ||
                              distToCachedTarget <= config.arrivalRadius;
    if (mustRequery) {
        LookaheadWalkResult walk =
            WalkLookahead(graph, state.currentSegmentIndex, state.pointCursor, config.lookaheadDistance,
                          config.maskIncludeBits, config.maskExcludeBits);
        if (!walk.ok) {
            state.valid = false;
            cmd.stuck = true;
            cmd.brake = 1.0;
            return cmd;
        }
        state.hasCachedQuery = true;
        state.cachedQueryOriginX = vehicleX;
        state.cachedQueryOriginZ = vehicleZ;
        state.cachedTarget = walk.target;
        state.cachedTargetIsDeadEndShort = walk.ranIntoDeadEnd;
        // Advance the anchor itself to the walk's own forward-most point -
        // "where the vehicle's own graph position now is", updated once
        // per re-query (not every tick) since that is the only point this
        // function has any new graph-walk information about. The NEXT
        // re-query's own WalkLookahead() call starts from here, matching
        // Sec18.1's own "walks the point chain forward from a given
        // current point" framing (the current point advances over time as
        // the vehicle does, not every single tick).
        state.currentSegmentIndex = walk.endSegmentIndex;
        state.pointCursor = walk.endPointCursor;
        cmd.requeried = true;
        if (walk.advancedSegment) cmd.advancedSegment = true;
    }

    // A dead end reached with NOTHING left to aim at (the lookahead walk's
    // own target collapsed onto the vehicle's current position, e.g. a
    // single-point segment with no successor) is reported as stuck,
    // same as the first pass's own dead-end handling.
    const double distToTarget = traffic_lane_detail::Dist2D(vehicleX, vehicleZ, state.cachedTarget);
    if (state.cachedTargetIsDeadEndShort && distToTarget <= config.arrivalRadius) {
        state.valid = false;
        cmd.stuck = true;
        cmd.brake = 1.0;
        return cmd;
    }

    cmd.target = state.cachedTarget;

    // ---- Item 5: Sec18.6's own dot-product alignment gate + CONFIRMED
    // numeric thresholds, picking throttle; full-lock +-1.0 steer with a
    // REAL overtake-gated sign source (Sec18.6a, file header item 5a -
    // REPLACES the first merge's cross-product stand-in). ----
    const double dx = static_cast<double>(state.cachedTarget.x) - vehicleX;
    const double dz = static_cast<double>(state.cachedTarget.z) - vehicleZ;
    const double dist = std::sqrt(dx * dx + dz * dz);
    const double headingFwdX = std::cos(vehicleHeadingRad);
    const double headingFwdZ = std::sin(vehicleHeadingRad);
    const double invDist = (dist > 1e-9) ? (1.0 / dist) : 0.0;
    const double targetDirX = dx * invDist;
    const double targetDirZ = dz * invDist;
    const double dot = headingFwdX * targetDirX + headingFwdZ * targetDirZ;      // Sec18.6 step 1

    // ---- Item 5a (Sec18.6b): the REAL 12-step overtake/pass-side gate
    // (Drop-in-guidance port, steps (a)/(c) verbatim, (d)+(e) approximated,
    // (b)/step-11 bypassed - see file header item 5a for the full mapping),
    // recomputing state.passSideFlag ONLY when ALL ported/approximated
    // steps pass - otherwise the flag simply keeps whatever it was last set
    // to (Sec18.6a's own text, unchanged by this task). ----
    bool overtakeGateFired = false;
    if (overtakeCandidate.has_value()) {
        const double cdx = overtakeCandidate->x - vehicleX;
        const double cdz = overtakeCandidate->z - vehicleZ;

        // Step (a)/3, PORTED VERBATIM: forward-facing-divergence - the two
        // vehicles' forward axes must be >=90 degrees apart (oncoming/
        // crossing traffic only). Same-direction traffic structurally
        // cannot pass this (Sec18.6b's own documented root cause of "never
        // fires" against an all-same-direction population).
        const double otherFwdX = std::cos(overtakeCandidate->headingRad);
        const double otherFwdZ = std::sin(overtakeCandidate->headingRad);
        const double forwardDot = headingFwdX * otherFwdX + headingFwdZ * otherFwdZ;
        const bool gateForwardDivergence = forwardDot <= kOvertakeGateForwardDivergence;

        // Step (c)/6-7, PORTED VERBATIM: speed-extrapolated forward closing
        // distance. predicted = pos + fwd*speed (no dt-scaling - Sec18.6b's
        // own text does not scale this by a timestep either, see file
        // header item 5a).
        const double predictedSelfX = vehicleX + headingFwdX * vehicleSpeedMs;
        const double predictedSelfZ = vehicleZ + headingFwdZ * vehicleSpeedMs;
        const double predictedOtherX = overtakeCandidate->x + otherFwdX * overtakeCandidate->speedMs;
        const double predictedOtherZ = overtakeCandidate->z + otherFwdZ * overtakeCandidate->speedMs;
        const double closingDx = predictedOtherX - predictedSelfX;
        const double closingDz = predictedOtherZ - predictedSelfZ;
        const double closingProjection = headingFwdX * closingDx + headingFwdZ * closingDz;
        const bool gateClosingDistance = closingProjection >= kOvertakeGateClosingDistanceFloor;

        // Steps (d)+(e)/9-10, APPROXIMATED (see file header item 5a for why
        // the real ratio test degenerates to "always fails" once sizeDelta
        // is forced to zero): single combined squared-distance cap on the
        // two vehicles' own predicted positions, reusing step 9's real 81.0
        // (9^2) constant.
        const double predictedDistSq = closingDx * closingDx + closingDz * closingDz;
        const bool gatePassPointBound = predictedDistSq <= kOvertakeGatePassPointDistanceSq;

        // Step (b)/4 and step 2/11 (leadHandle path): BYPASSED - no
        // half-extent or lead-handle concept modelled, see file header item
        // 5a. Treated as always-satisfied, not silently dropped.
        overtakeGateFired = gateForwardDivergence && gateClosingDistance && gatePassPointBound;

        if (overtakeGateFired) {
            // Sec18.6a's own write: new_bit2 = (lateral_axis . (candidate -
            // myPos) < 0.0) - lateral axis CHOSEN as heading rotated +90
            // degrees (file header item 5a), halfWidth term dropped (not
            // modelled on NearFieldVehicle).
            const double lateralX = -headingFwdZ;
            const double lateralZ = headingFwdX;
            const double sideDot = lateralX * cdx + lateralZ * cdz;
            state.passSideFlag = (sideDot < 0.0);
        }
        // Gate failed this tick: state.passSideFlag is left untouched,
        // Sec18.6a's own "keeps whatever value it was last set to".
    }
    // overtakeCandidate == nullopt: same as the gate failing - no write,
    // flag keeps its last (or CHOSEN-default-false) value.

    // Item 5's own second flagged ambiguity (file header comment): the
    // 5.0-unit distance operand is measured against the vehicle's own
    // position AT THE TIME the cached target was last computed
    // (cachedQueryOriginX/Z, item 4's own cache - recomputed as
    // distFromCachedOrigin above, reused here rather than a second
    // distance helper call), NOT the far-ahead lookahead target itself -
    // see that comment for why the opposite reading deadlocks. This also
    // means the 5.0-unit gate and the 3.0-unit re-query threshold
    // (LaneFollowConfig::reQueryDistanceThreshold, item 4) are the SAME
    // kind of measurement - "how far has the vehicle moved since its last
    // real plan" - at two different purposes (one re-plans, the other
    // gates throttle), consistent with both being named in the SAME
    // Sec18.5/18.6 cached-target mechanism rather than two unrelated
    // distances.
    cmd.aligned = (dot >= kAlignmentCosineThreshold) && (distFromCachedOrigin <= kAlignmentDistanceThreshold);
    cmd.headingAligned = (dot >= kAlignmentCosineThreshold);
    cmd.overtakeGateFired = overtakeGateFired;
    // Sign: UNCHANGED, still the sole source Team A confirmed is real -
    // state.passSideFlag (Sec18.6a), recomputed above only behind the real
    // Sec18.6b gate, otherwise keeping its last value. Bit-to-sign mapping
    // itself is CHOSEN, arbitrary but documented - file header item 5a
    // ("BIT-TO-STEER-SIGN MAPPING").
    const double steerSign = state.passSideFlag ? -1.0 : 1.0;
    // Magnitude: Sec18.6's own "Secondary item" curve (RESOLVED 2026-10-11),
    // run for real via traffic_lane_detail::ApplySteeringCurve() - see that
    // function's own doc comment for the CONFIRMED airborne-gate/negate/
    // clamp/piecewise-linear/reapply-sign mechanics, and file header item 5
    // ("STEERING MAGNITUDE") for why this file feeds the curve
    // `steerSign * alignmentError` rather than the literal +-1.0 `steerSign`
    // alone (the latter is mathematically degenerate - the curve always
    // lands at +-1.0 when |x|=1.0, which would silently reintroduce the
    // exact full-lock regression this task fixes). `airborne` is CHOSEN
    // false - NearFieldVehicle models no grounded/airborne field (file
    // header item 5, same neutral-bypass convention as PassesLaneMask()).
    const double alignmentError = std::clamp(1.0 - std::clamp(dot, -1.0, 1.0), 0.0, 1.0);
    // Pre-negated by -steerSign (not +steerSign): ApplySteeringCurve's own
    // step 2 negates its input before anything else (CONFIRMED mechanic,
    // see its doc comment) - pre-compensating here keeps this file's own
    // "BIT-TO-STEER-SIGN MAPPING" convention (passSideFlag==false -> +1)
    // meaning what it says at the OUTPUT, rather than silently inverting it
    // through the curve's own internal negation.
    const double curveInput = -steerSign * alignmentError;
    cmd.steer = traffic_lane_detail::ApplySteeringCurve(curveInput, kSteerCurveThreshold, kSteerCurveSlope,
                                                          /*airborne=*/false);
    // Sec18.6 step 4: CONFIRMED throttle=0 when far/misaligned; CHOSEN
    // driving-throttle magnitude when close/aligned (the real "3.5"
    // literal's units are OPEN).
    cmd.throttle = cmd.aligned ? config.drivingThrottle : 0.0;
    // Sec18.6 step 4 tail: "likely... set brake/handbrake 0" (not fully
    // confirmed, Sec18.6's own "likely") - ported as brake=0 whenever not
    // stuck.
    cmd.brake = 0.0;
    return cmd;
}

}  // namespace sr3airuntime
