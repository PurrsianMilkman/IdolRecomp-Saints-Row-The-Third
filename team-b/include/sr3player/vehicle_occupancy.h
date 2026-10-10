#pragma once

// sr3player/vehicle_occupancy.h - player runtime "minimum drivable slice":
// U6.0 (the vehicle-interaction slot record, registry and state skeleton),
// U6.1 (the element-selection rule), U6.3 (the enter route: states 0-8, the
// routing hub and the instant route), U6.4 (the seating commit, condition-
// row hand-off and ride state), U6.5 (the animated enter states 0x0a-0x13's
// own interior bodies), U6.6 (the exit request and the six non-animated
// exit states 0x14-0x19) and U6.7 (the animated exit states 0x1b/0x1d/0x1f/
// 0x21's own interior bodies) - spec-player-runtime.md Sec6/Sec7/Sec8/Sec9/
// Sec10/Sec11/Sec14. Every rule cited below is quoted from that text;
// nothing here was read from the game .exe or any disassembly, and
// `0x2237`/`.czn_pc` are never touched.
//
// HISTORY (U6.5/U6.7 pass): the vehicle minimum-drivable-slice merge
// (bbd627d) left every state in 0x0a-0x0f/0x11/0x13 (enter) and 0x1b/0x1d/
// 0x1f/0x21 (exit) as a generic StubOnEntry() - U6.5 and U6.7 had not been
// specced yet. Both landed on 2026-10-07 (Sec11 then Sec14); this file was
// updated in place to give those states real bodies, against that now-
// CONFIRMED text. State 0x0f is U6.5's by citation only - U6.5 itself
// explains it was already fully specified by U6.3 (Sec10.4.4) as a
// byproduct of refuting the "instant route" HYPOTHESIS, and is cited
// verbatim rather than re-derived (Sec14.7).
//
// ARCHITECTURE NOTE on this pass's own external-signal fields (judgement
// call): this registry owns no real per-clip animation duration/table data
// (FindAnimation is a free function the CALLER drives - see the original
// architecture note below) and no vehicle physics/geometry model at all -
// the SAME two gaps U6.3/U6.4/U6.6 already hit (the un-reproduced "tilt
// fix", Sec10.5 step3; the un-reproduced tip-over test, Sec6.7.1). U6.5 and
// U6.7's own CONFIRMED text leans on both constantly (every state waits for
// "the held animation finished", several branch on vehicle tilt/speed/room
// geometry). Rather than inventing a value for either, every such
// dependency is a caller-supplied field (VehicleOccupancyExternalSignals,
// below, plus a few vehicle-level booleans on VehicleInteractionRecord) -
// the exact shape `vehicleInstantiated`/`hasEnterAnimationsForSeat` already
// use on RequestEnterVehicle, just applied consistently to the new states.
// Every field defaults to the ordinary/CONFIRMED-common case (e.g. a
// lookup "hit", ownership/network-authority always true in this single-
// player project per Sec6.13 point3's own precedent) so a test exercising
// only the transition skeleton never has to touch them.
//
// A SEPARATE, narrower case is the "next-state chooser" `0x00afb220`
// (Sec7.5/Sec9.5's "two choosers"; called by the per-frame handlers of
// 0x0a/0x0b/0x0c/0x0d). Only two of its internal probes are CONFIRMED
// (Sec9.5's row for it: probe (a) decides open-vs-traverse, probe (b)
// decides extract-vs-extract-fail); the full cascade that picks among its
// seven CONFIRMED return values (8, 0x0a..0x0f) was never resolved by any
// read spec section (U6.5's own OPEN item 109: "what the flag selects was
// not resolved"). Per this project's no-invented-fixes policy, the two
// probes ARE reproduced for real (ProbeOpenVsTraverse/
// ProbeExtractFailVsExtract); the full chooser itself (NextStateChooser)
// throws sr3luahost::OpenStateError unless a caller supplies an override
// (nextStateChooserOverride) - exactly the open-dependency shape
// sr3luahost::OpenValue/OpenStateError exist for, adapted for a per-call
// result rather than a persistent global.
//
// HISTORY: U6.3 (Sec10) was NOT YET SPECCED when this file's U6.0/U6.1/U6.4/
// U6.6 pieces were first written - at that point InstantEnterVehicle() was a
// named stub throwing sr3luahost::OpenStateError, exactly as this task's own
// brief asked for a genuinely-open dependency. Team A landed Sec10 on main
// (commit 4ae8d22) while this slice was still in progress; this file was
// updated in place to implement the real route (RequestEnterVehicle() below)
// against that now-CONFIRMED spec text, integrated into the SAME registry/
// dispatcher this file already built for U6.0/U6.4/U6.6 (the commit's own
// OnEnterCommit, the exit machinery's RequestExit, and ReleaseSlotInternal
// are all reused, not re-implemented).
//
// ---------------------------------------------------------------------------
// ARCHITECTURE CHOICE (judgement call, not spec content - recorded for the
// reviewer): a NEW, separate build target (sr3player_vehicle), not an
// addition to the existing sr3player library's own source list, even though
// the header lives under include/sr3player/ and the sr3player:: namespace
// (same spec document, same one-file-per-unit convention control_state.h/
// movement.h/camera.h already use - only the BUILD TARGET differs).
//   1. CMakeLists.txt discipline this round is additive-only (several
//      parallel agents are editing it; sr3player's own source list was just
//      extended for U4's camera files in the immediately preceding commit).
//      A brand-new add_library(...) block touches zero existing lines; an
//      addition to sr3player's existing list would.
//   2. The registry this file defines is not player-scoped the way
//      control_state.h/movement.h/camera.h are (the LOCAL PLAYER's own
//      per-frame machinery): Sec7's registry holds ANY human occupant of ANY
//      vehicle slot (hijack victims, NPC passengers, the player alike) - a
//      48-record, 8-slot table keyed by vehicle handle, not a per-player
//      singleton. A separate target signals that scope honestly.
//
// ---------------------------------------------------------------------------
// SCOPE - this task's four deliverables only:
//   * U6.0 (Sec7): registry/record/slot layout, find/allocate/release, a
//     scoped-down submitter (Sec7.4 steps 7/8/11 only), the three setters'
//     DISPATCH mechanism, and the four Sec7.8 "+0x16d4" phase-boundary
//     writes this slice actually performs (states 0 and 0x14's stub/real
//     on-entry, ride on-entry, release).
//   * U6.1 (Sec9): the key builder (slot -> 5 tokens, Sec9.3's full table)
//     and the wildcard matcher (Sec9.1/Sec9.4), as free functions over a
//     caller-supplied temporary record - exactly how the real rule is used
//     (Sec9.0 item 7: seat scoring, the hijack chooser, the failed-hijack
//     pick and the next-state chooser all probe it with a throwaway slot,
//     not the occupant's real one).
//   * U6.3 (Sec10): the enter route's CONFIRMED skeleton - state 0
//     (requested: condition row 0xa, the 800ms stick-abort grace, Sec10.2),
//     state 1 (the routing hub's full decision table, Sec10.3 step15/
//     Sec10.0 item1, including the force-instant pre-rules of step11 and
//     the claim-and-enqueue of Sec10.1/step7), the full instant route 0 ->
//     1 -> (2 ->) 4 -> 9 (Sec10.4.2), and state 8 (the one-shot cancel,
//     Sec10.9's CONFIRMED core: the "begun" callback fired with ok=0, claim/
//     next-claim clean-up, condition row back to 0, release last). States 3,
//     5, 6, 7 and the routing TARGETS 0x0a-0x0f/0x11/0x13 (the walk, the
//     arrival router, the dead state, and the animated/forced/fast-entry
//     bodies themselves) stay the generic StubOnEntry() - the hub correctly
//     ROUTES to them (this slice's own ask), but their interior behaviour
//     (navigation, animation choice, ...) is U6.3's walk machinery or
//     U6.5's and is not invented.
//   * U6.4 (Sec8): the seating commit's CONFIRMED, directly-stated
//     observable core - above all **condition-row +0xcc8 = 8 (seat 0) / 9
//     (elsewhere)** - plus the seat-record/phase/stateEnum bookkeeping
//     Sec8.1's own "minimal stub" list names, and the trailing set-ride
//     (Sec8.2 step 29). Weapon holstering, the ~39 vehicle-band callees,
//     AI/drunk-driver effects, player-only HUD/tutorial/cheat effects, the
//     team-check re-exit (Sec8.2 step 33) and the camera hand-off (Sec8.6)
//     are explicitly OUT OF SCOPE for this slice - each needs an inventory,
//     vehicle-AI, team or camera-submode system this slice does not touch;
//     noted at CommitSeating()'s own doc comment, never silently skipped.
//   * U6.6 (Sec6): the exit request (Sec6.3, including the permission-lock
//     rule Sec6.3.2) and the six exit states 0x14-0x19 in full for every
//     branch that needs no vehicle physics/geometry. Branches that do (the
//     tip-over test Sec6.7.1, 0x16's "move to another seat" sub-path,
//     equip-on-exit's actual inventory switch Sec6.7.3) are documented OPEN
//     at their own call site, never invented.
//   * U6.5 (Sec14): the eight animated enter states' own interior bodies -
//     0x0a prepare, 0x0b traverse, 0x0c open, 0x0d extract/hijack, 0x0e the
//     extract-fail pick, 0x0f enter (cited verbatim from U6.3, Sec14.7),
//     0x11 change seat and 0x13 fast - for every branch that is CONFIRMED
//     and needs no real per-clip animation data or vehicle geometry/AI.
//     Branches that do (the tilt-angle test Sec14.2, the hijack chooser's
//     real candidate enumeration Sec9.5, the "surface kind" door-opening
//     tail of 0x13's per-frame Sec14.13 item1 - already an OPEN item in the
//     spec itself) are caller-supplied signals or documented OPEN, never
//     invented. The next-state chooser `0x00afb220`'s own full cascade
//     (Sec7.5/Sec9.5, only two of its seven outcomes are CONFIRMED) is a
//     genuine OPEN dependency (NextStateChooser(), throws
//     sr3luahost::OpenStateError unless overridden) - see the file banner.
//   * U6.7 (Sec11): the four animated exit states' own interior bodies -
//     0x1b change seat, 0x1d extract, 0x1f exit (including its own exit-
//     kind chooser `0x00b04ae0`, Sec11.5.2) and 0x21 traverse - for every
//     branch that is CONFIRMED and needs no real vehicle physics/geometry.
//     The exit-kind chooser's own vehicle-geometry predicates (tip-over,
//     dive-room, side-clearance, speed) are caller-supplied booleans
//     defaulting to the cascade's own named "implicit default" (kind
//     0x14, Sec11.5.2's own HIGH-confidence reading) - not invented
//     physics. The eject kind's actual velocity write (Sec11.5.4 step6,
//     shared with U3 Sec5.4) is NOT reproduced - this project has no
//     physics-velocity field on a human at all; only its OBSERVABLE,
//     CONFIRMED state effect (fast-forward straight to the finaliser,
//     Sec11.5.4 step4) is.
//
// EXPLICITLY NOT THIS SLICE:
//   * U6.2/U6.8 state BODIES beyond U6.5/U6.7's own scope above: state 3's
//     change-seat WAIT (not state 0x11's own change-seat body, which IS
//     this slice's, per U6.5), state 5's walk, state 6's arrival router and
//     state 7's dead-state network wait. These remain live, holdable
//     states (Sec7.5) whose dispatcher entry is still the generic
//     StubOnEntry() - the ONE universal, U6.0-owned mechanical fact every
//     on-entry handler shares ("each on-entry handler stores its own state
//     number", Sec7.0 item 2) and nothing else. The owning unit's real
//     per-state behaviour (navigation, the arrival snap-to-point, ...) is
//     never invented here. U6.3's own ROUTING to these states (the hub's
//     decision table) and to every U6.5/U6.7 state above IS this slice's
//     responsibility and IS implemented.
//   * Real vehicle physics/geometry, vehicle rendering, AI-driven vehicle
//     behaviour, and any Lua native wiring (vehicle_enter_do/vehicle_exit_do
//     and friends) - none of that is modelled here at all.
//
// ---------------------------------------------------------------------------
// INTEGRATION NOTE on the condition-row field (+0xcc8): spec-lua-api-
// behaviour.md Sec7.13/Sec32.3/Sec34.5 already model this SAME physical
// field as CharacterState::lifeState in include/sr3luahost/engine_state.h
// (values 5 "dead/kill-finalised", 6 "downed"). This file writes DIFFERENT
// values to that same field (0xa requested, 0 cancelled/exited, 8/9
// seating, 0xb exit-requested) but keeps its OWN, independent occupant-
// keyed map rather than taking a dependency on sr3luahost::CharacterState,
// to avoid coupling two units' owned files mid-round (CharacterState is a
// large, actively-edited shared struct this round). A future integration
// pass should route both through one sink - flagged here, not fixed, per
// this project's no-invented-fixes policy. The same reasoning applies to
// the human state-enum field +0x16d4 (CharacterState::stateEnum already
// models it, for a DIFFERENT purpose, Sec3.4/Sec3.10) - this file keeps its
// own occupant-keyed copy, written only at the Sec7.8/Sec10.2 sites this
// slice actually implements (states 0, 0x14, ride on-entry, release).
//
// This is also NOT the control-state machine of control_state.h (Sec2's
// 8-row `0x012e6bb8` table) or the normal-condition classify of movement.h
// (Sec3.2's `0x005ac620`): those gate the LOCAL PLAYER's per-frame input
// handling by condition-row VALUE; this file is the thing that WRITES that
// value for a vehicle occupant (Sec8.0 item 2: "the hand-off to vehicle
// controls is the condition-row change ... without +0xcc8 = 8/9 the player
// keeps the on-foot controls"). Wiring my ConditionRow() output into
// movement.h's own per-row dispatch (U7's in-vehicle action handler
// `0x005b5c50`) is explicitly out of scope here too.

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "sr3tables_vehicle_world/tables.h"

namespace sr3player {

// ===========================================================================
// U6.0 - state numbering (Sec7.5's full 35-entry table)
// ===========================================================================

// A slot's phase (Sec7.2 +0x08): 0 none, 1 entering, 2 exiting, 3 riding.
enum class VehicleSlotPhase : int32_t { kNone = 0, kEnter = 1, kExit = 2, kRide = 3 };

// The engine's own state numbers (Sec7.5). Only the ones this slice gives a
// real body to get a named, used constant; the rest are referenced by their
// literal hex value directly (matching how the spec itself cites them) so
// the dispatcher's switch statements stay readable against the spec table.
inline constexpr int32_t kEnterSubmitted = 0x00;   // enter, U6.3 (Sec10.2)
inline constexpr int32_t kEnterHub = 0x01;         // enter, U6.3 (Sec10.3) - "never held across a frame"
inline constexpr int32_t kEnterInstantWaitSeat = 0x02;  // enter, U6.3 (Sec10.5) - instant route, seat occupied
inline constexpr int32_t kEnterInstantTakeSeat = 0x04;  // enter, U6.3 (Sec10.5) - instant route, take the seat
inline constexpr int32_t kEnterCommit = 0x09;      // enter, U6.4 (Sec8)
inline constexpr int32_t kEnterPrepare = 0x0a;     // enter, U6.5 (Sec14.2)
inline constexpr int32_t kEnterTraverse = 0x0b;    // enter, U6.5 (Sec14.3)
inline constexpr int32_t kEnterOpen = 0x0c;        // enter, U6.5 (Sec14.4)
inline constexpr int32_t kEnterExtract = 0x0d;     // enter, U6.5 (Sec14.5) - the hijack/extract body
inline constexpr int32_t kEnterExtractFailPick = 0x0e;  // enter, U6.5 (Sec14.6)
inline constexpr int32_t kEnterForcedInPlace = 0x0f;  // enter, U6.3's own body, cited verbatim by U6.5 (Sec10.4.4/Sec14.7) - NOT the instant/snap route
inline constexpr int32_t kEnterChangeSeat = 0x11;  // enter, U6.5 (Sec14.9)
inline constexpr int32_t kEnterFast = 0x13;        // enter, U6.5 (Sec14.10)
inline constexpr int32_t kEnterCancel = 0x08;      // enter, U6.3 (Sec10.9) - the one-shot cancel
inline constexpr int32_t kExitRequested = 0x14;    // exit, U6.6 (Sec6.6)
inline constexpr int32_t kExitLeaveSeat = 0x15;    // exit, U6.6 (Sec6.7)
inline constexpr int32_t kExitWaitSeat = 0x16;     // exit, U6.6 (Sec6.8)
inline constexpr int32_t kExitInstantPlacement = 0x17;  // exit, U6.6 (Sec6.9)
inline constexpr int32_t kExitParked = 0x18;       // exit, U6.6 (Sec6.10)
inline constexpr int32_t kExitFinalise = 0x19;     // exit, U6.6 (Sec6.11)
inline constexpr int32_t kExitChangeSeat = 0x1b;   // exit, U6.7 (Sec11.3)
inline constexpr int32_t kExitExtract = 0x1d;      // exit, U6.7 (Sec11.4)
inline constexpr int32_t kExitExit = 0x1f;         // exit, U6.7 (Sec11.5)
inline constexpr int32_t kExitTraverse = 0x21;     // exit, U6.7 (Sec11.6)
inline constexpr int32_t kRideActive = 0x22;        // ride, U6.4 (Sec8.5)

// Exit kinds (slot +0x10 while in state 0x1f/0x21, Sec9.3/Sec11.5.2's table).
inline constexpr int32_t kExitKindStandard = 0x14;
inline constexpr int32_t kExitKindFast = 0x15;
inline constexpr int32_t kExitKindAtSpeed = 0x16;
inline constexpr int32_t kExitKindDive = 0x17;
inline constexpr int32_t kExitKindAtAltitude = 0x18;
// 0x19 ("exit crawl") is almost certainly NOT a real kind in the shipped
// game: Sec11.5.2's own trace found no producer for it, and notes that
// 0x19 appearing register-written is "almost certainly" the finaliser's
// own STATE number (kExitFinalise), not a kind, mis-attributed by OPEN
// 69. Named here only because U6.1's key builder has a real case for it
// (Sec9.3's table); ChooseExitKind() never produces it.
inline constexpr int32_t kExitKindCrawl = 0x19;
// 0x1a ("exit climb"): read and handled by 0x1f's own per-frame
// (Sec11.5.3's sub-timer), but its PRODUCER (who writes slot +0x10 = 0x1a)
// was not found by U6.7 either - remains OPEN. ChooseExitKind() never
// produces it either; a caller that wants to exercise the climb branch
// must set slot.variant10 = kExitKindClimb directly before ticking 0x1f.
inline constexpr int32_t kExitKindClimb = 0x1a;
inline constexpr int32_t kExitKindEject = 0x1b;
inline constexpr int32_t kExitKindGuardianAngel = 0x1c;

// ===========================================================================
// U6.6 - the exit request record (0x33 bytes, Sec6.2)
// ===========================================================================
struct ExitRequestRecord {
    int32_t exitSeat = -1;          // +0x00: the seat the occupant leaves through
    int32_t animationOverride = -1; // +0x2c: an animation-state id override; -1 none (U6.7 territory, not read here)

    // +0x28, the request's "start time": this project has no shared clock
    // service for this module, so the real "now + ms" timer is modelled as a
    // plain countdown a caller/Tick() decrements - 0 (the default) means
    // "already passed", matching the real constructor's own "now" default
    // (Sec6.2: "'Now' by default, so there is no wait").
    int32_t startDelayMs = 0;

    // +0x30 bits (Sec6.2's field table)
    bool instant = false;              // bit 0
    bool keepInPlace = false;          // bit 1
    bool beingExtracted = false;       // bit 3: chooses 0x1d over 0x1f on the animated path
    bool vehicleBandBit5 = false;      // bit 5 (vehicle-band only; part of the 0xa8 skip-handoff mask, not acted on here)
    bool vehicleBandBit6 = false;      // bit 6 (same 0xa8 mask)
    bool exitPromptBit7 = false;       // bit 7, the player's own exit prompt (same 0xa8 mask)

    // +0x31 bits
    bool instantPathSkipTo19 = false;  // bit 3: on 0x15's instant path, go to 0x19 instead of 0x17
    bool landingRunNotWalk = false;    // bit 1: the finaliser's landing branch locomotion choice (observed only, not acted on - see OnExit19Finalise's doc comment)
    bool waitAtOwnSeat = false;        // bit 7: 0x16's "wait at the own seat" flag

    // +0x32 bits
    bool replicated = true;            // bit 1, default true (network - no-op in this project, kept for fidelity)
    bool vehicleMayBeGone = false;     // bit 2

    // Sec9.0 item 3 / Sec9.8 OPEN 69: the exit side's "convertible" variant
    // selector, read by the key builder at exit request +0x31 bit 0. No
    // writer was found by the spec's own pass (OPEN 69) - exposed here as a
    // plain settable field for a caller/test, not computed.
    bool exitConvertible = false;

    // Sec6.7.3: the seat's "Equip Rifle on Exit" flag
    // (vehicle_interaction_info.xtbl seat byte +0x0B bit 0), ALREADY
    // resolved by the caller through sr3tables_vehicle_world's own 8-seat
    // resolver (this module does not re-implement that resolver). When
    // true, OnExit15LeaveSeat only RECORDS the request on the slot
    // (equipRifleOnExitRequested) rather than performing a real inventory
    // switch - no inventory system is in this project's current scope.
    bool equipRifleOnExitResolved = false;

    // --- U6.7 (Sec11) fields ---
    // +0x20/+0x24: the victim's extract kind/side, written by the hijack
    // chooser (Sec9.5's citation of 0x00afa540: "sends the victim an exit
    // request with +0x20 = kind+9 ... and +0x24 = the side") and copied to
    // slot +0x10/+0x20 by 0x1d's on-entry (Sec9.3/Sec9.5/Sec9.8, Sec11.4.1
    // step2). -1 default (Sec6.2: the plain constructor leaves +0x20 = -1,
    // confirmed by Sec9.4.3's own short-key-miss table entry for 0x1d).
    int32_t exitKind = -1;
    int32_t approachSide = -1;

    // +0x08/+0x0c: a second human's handle the eject/guardian-angel
    // placement is anchored to (Sec11.0 item4/Sec11.8) - HIGH, read by
    // 0x1d's per-frame only to fire a (not modelled) change notification
    // and by the exit-kind chooser's own eject branch (not reproduced
    // further - this project has no "companion anchor" placement system).
    uint64_t companionHandle = 0;

    // +0x31 bit 2: the "point-set" flag (0x15's own 0x00b0b9a0 query,
    // Sec6.2/Sec11.5.1 step3) - read by 0x1f's on-entry/per-frame to decide
    // a teleport-instead-of-clip path. No real point-query system exists
    // here, so this always reads as "no point" in this slice's own
    // per-frame unless a caller sets it AND also supplies a resolved point
    // (there is none to supply - this slice never teleports; see
    // OnExit1f's own doc comment).
    bool pointSet = false;
    // +0x31 bit 4: forces exit kind 0x1c ("exit guardian angel"),
    // Sec11.5.2 step1.
    bool guardianAngelForced = false;

    // +0x30 bits 4-7 (Sec11.0 item5/Sec11.8, corrections to the table
    // above): bit4 forces "exit dive" past the dive-room check; bit5
    // selects "exit eject"; bit6 is the eject companion-resolved flag
    // (ALSO written by the exit-kind chooser, not only read); bit7 (already
    // modelled above as exitPromptBit7) is ALSO written by the exit-kind
    // chooser's own side-clearance cascade, not only the player's prompt.
    bool forceDive = false;
    bool forceEject = false;
    bool ejectCompanionResolved = false;

    // +0x32 bit 0: 0x1f's own per-frame "already started" latch
    // (Sec11.5.1 step6/Sec11.8), distinct from bit1 (replicated) and bit2
    // (vehicleMayBeGone).
    bool exitStarted = false;
};

// ===========================================================================
// U6.0 - the enter-request copy a submission carries (Sec7.2's "+0x5c..+0x9f"
// region). Minimal: only the fields CommitSeating() (U6.4) needs.
// ===========================================================================
struct EnterRequestRecord {
    int32_t requestedSeat = -1;  // +0x10 (request-relative): candidate seat; becomes slot +0x2c at the commit (Sec7.2/Sec8.2 step 3)
    bool forced = false;          // +0x23 bit 3: camera snap at the commit (Sec7.2; Sec8.2 step 12d); U6.3: also routes the hub to 0x0f (Sec10.3 step15)
    bool skipTeamCheck = false;   // +0x22 bit 6: skips the commit's team check (Sec7.2; Sec8.2 step 33 - not reproduced here, see CommitSeating's doc comment)

    // --- U6.3 (Sec10) fields ---
    bool instant = false;         // +0x20 bit 0 (Sec10.4.1): the instant-route trigger
    bool fastEntry = false;       // +0x23 bit 0 (Sec10.0 item1/Sec10.10): routes the hub to 0x13
    bool hasAnimationOverride = false;  // +0x30 != -1 (Sec10.10): routes the hub to 0x0f, same as `forced`
    // request +0x08 (Sec10.0 item7/Sec10.10): -1 default, 0 "approach only",
    // 1 "remove the occupant, then stop", 2 "normal entry". Only 0 (skips
    // H1's eviction) and 1 (H4's unconditional cancel) change this slice's
    // own routing; 2 and -1 behave identically here.
    int32_t requestKind = -1;
    // Sec10.4.2 H1's tank branch: a player-class victim in a tank's own
    // entry seat cancels the entry outright instead of being evicted. Any
    // OTHER victim (a non-player in a tank, or any occupant of a non-tank
    // vehicle) is evicted via an ordinary instant exit request - this
    // slice's own simplification for the tank's "kill and destroy the
    // victim" sub-case (Sec10.4.2 H1), which needs a damage/AI system this
    // project does not model; flagged, not invented.
    bool isTankEntry = false;
    bool evictedVictimIsPlayerClass = false;

    // Sec10.3 step2/Sec10.9 step3, request +0x34: the "begun" callback,
    // called once with true from the hub (Sec10.3 step2) or with false from
    // a cancel if the hub never ran (Sec10.9 step3) - never called twice,
    // and cleared after firing. The real completion callback (request
    // +0x3c, Sec8.2 step32) is a SEPARATE field this slice does not model
    // (U6.4's own CommitSeating has no request-level completion callback
    // either) - Sec10.0 item4's "it never calls the completion callback" is
    // therefore reproduced by omission: there is nothing here to call.
    std::function<void(bool /*ok*/)> onBegunCallback;

    // --- U6.5 (Sec14) fields ---
    // +0x7d bit 7 (slot-relative +0x21 bit7): 0x00b01460's own latch
    // (U6.7 Sec11.7.2, cited unchanged by U6.5 Sec14.5 step5/Sec14.10
    // step3-4) - "already started the fast-entry animation".
    bool fastEntryLatched = false;
    // +0x7f bit 1 (slot-relative +0x23 bit1): "an occupant is being
    // hijacked via the chooser" - set by 0x13's own on-entry after a
    // successful hijack-chooser call (Sec14.10 step3/Sec14.11); NOT set by
    // 0x0d's on-entry, which calls the same chooser but writes no bit
    // around it (Sec14.5). New field; not read anywhere within this
    // slice's own scope (exposed for fidelity/observability only).
    bool hijackedViaChooser = false;
    // request +0x21 bit 6 (Sec9.5): picks hijack kind 6 ("extract
    // convertible") directly, without a lookup.
    bool hijackForceKind6 = false;
};

// ===========================================================================
// U6.5/U6.7 - external-system stand-ins (judgement call - see the file
// banner's own ARCHITECTURE NOTE). Every field below is read by at least
// one new on-entry/per-frame handler added for U6.5/U6.7; none is invented
// data - each one stands in for a real per-clip animation/table/physics
// fact this registry has no way to compute, and each defaults to the
// ordinary/CONFIRMED-common case so a test exercising only the transition
// skeleton never has to touch it.
// ===========================================================================
struct VehicleOccupancyExternalSignals {
    // Stands in for whichever U6.1 lookup (0x00b0ad10) the current call
    // makes - this registry owns no real vi_enter.xtbl/vi_exit.xtbl data
    // (FindAnimation is a free function the CALLER drives, see the file
    // banner), so a real hit/miss can't be computed here. Defaults to
    // "hit" (every live Element set in the shipped game has normal
    // entries per Sec9.4.3's own reachability table) - set false to
    // exercise a state's own documented miss path.
    bool animationLookupHit = true;

    // Stands in for 0x00b0c170 (U6.7 Sec11.7.1, "is the held
    // vehicle-interaction animation finished") - advanced by the caller
    // across successive Tick calls, since no real clip duration exists
    // here. Defaults false (still playing) so a bare Tick() never
    // spuriously finishes an animation a test didn't ask to finish.
    bool animationFinished = false;
    // Stands in for the progress query (0x004b4600, 0..1) a few states
    // compare against an explicit, numerically-given threshold (Sec14.4
    // step2's ~0.5, Sec11.6's 1.0) rather than a plain finished/not test.
    float animationProgress = 0.0f;

    bool anyWalkAxisNonZero = false;  // CAA_WALK_FORWARD_BACKWARD/TURN_LEFT_RIGHT (Sec1.9) - every stick-abort path in this unit
    bool isLocalPlayer = true;

    // Sec14.2's tilt-angle branch (0x00da48a0 on vehicle +0x4c, doubled,
    // vs 0.5) - vehicle orientation data this project does not model (same
    // gap as the un-reproduced U6.3 Sec10.5 step3 "tilt fix"). False
    // (small tilt, the ordinary chooser-transition path) is the safer
    // default: it does not invent an exceptional stick-abort-style path.
    bool vehicleTiltSevere = false;

    // 0x00afa540's own random pick among the candidates that have a
    // matching Element (Sec9.5) - no real candidate-availability data
    // exists here, so the caller names the winner directly. 1 ("extract
    // standard") is always a safe candidate (Sec14.5's own pre-seeded
    // default for the same reason).
    int32_t hijackChosenKind = 1;

    // 0x00afaf90's own count-then-roll pick (Sec9.3/Sec14.6 step2): which
    // "extract fail N" variant (Sec9.3's +0x14 mapping: 5/6/7 -> 1/2/3).
    // 5 ("extract fail 1") is always a valid pick per Sec9.4.3's own
    // reachability note ("all 12 shipped groups have exactly three").
    int32_t extractFailPick = 5;

    // 0x1f's +0x18 6->7 sub-timer (Sec11.5.1 item6/Sec11.5.3): "wait out
    // half of the animated-camera test window" - no window duration is
    // given numerically anywhere in the read spec text, so unlike every
    // other signal above there is no safe "ordinary case" default to
    // assume. Left unset (nullopt), TickExit1f throws OpenStateError when
    // it reaches this specific branch - the OpenValue/OpenStateError
    // shape this project uses for a genuinely missing constant (see the
    // file banner), adapted to a per-call optional since the underlying
    // fact is dynamic (a progress test), not a persistent global.
    std::optional<bool> halfWindowElapsed;

    // 0x13's per-frame knockdown loop (Sec14.10 step4): whether the
    // destination seat's entry-point QUEUE (not the seat itself, which
    // this registry resolves for real through VehicleSeatRecord) has
    // someone in it - approximated here since no real distance-ordered
    // queue exists (Sec10.1's own defect/HYPOTHESIS, already not
    // reproduced by RequestEnterVehicle).
    bool entryPointQueueClearNow = true;

    // --- U6.7's exit-kind chooser (0x00b04ae0, Sec11.5.2) - every
    // geometry/speed/room predicate the real cascade tests, none of
    // which this project has data for (same vehicle-physics gap as the
    // un-reproduced tip-over test, Sec6.7.1). All default false, which
    // falls all the way through to the cascade's own named "implicit
    // default" (kExitKindStandard, Sec11.5.2's own HIGH-confidence
    // reading) - see ChooseExitKind(). ---
    bool exitTippedAndAtAltitude = false;  // tip-over + speed -> "exit at altitude"
    bool exitDiveRoomClear = false;        // dive-room predicate -> "exit dive" (exitRequest.forceDive skips this check)
    bool exitSideClearOutward = false;     // side-clearance cascade -> "exit fast outward"
    bool exitSideClearForward = false;     //   ... "exit fast forward"
    bool exitSideClearBackward = false;    //   ... "exit fast backward"
    bool exitSpeedAtCruise = false;        // speed-threshold cascade -> "exit at speed cruise"
    bool exitSpeedAtFast = false;          //   ... "exit at speed fast"

    // Sec11.5.2's own "implicit default" reading is HIGH confidence, not
    // CONFIRMED - and U6.0's own census independently confirms state
    // 0x21 (Sec11.6) IS live, which per Sec11.6 step2's own text can only
    // happen when the chooser genuinely leaves the kind unresolved. This
    // flag exercises exactly that documented, rarer path instead of the
    // default cascade's fallback.
    bool exitKindChooserCannotResolve = false;
};

// ===========================================================================
// U6.0 - the slot (0x100 bytes, Sec7.2) and the vehicle-keyed record
// (Sec7.1). Field-equivalent, not byte-equivalent: this is a clean-room
// reimplementation, so there is no engine memory layout to match bit for
// bit - only the OBSERVABLE fields and their confirmed semantics.
// ===========================================================================
struct VehicleSlot {
    uint64_t occupantHandle = 0;   // +0x00, 0 = free
    VehicleSlotPhase phase = VehicleSlotPhase::kNone;  // +0x08
    int32_t state = -1;             // +0x0c

    // +0x10/+0x14/+0x18: per-state variant fields the U6.1 key builder reads
    // (Sec7.2; reset to -1 at states 0, 9 and every U6.6 on-entry, Sec7.11
    // OPEN 48 - this slice performs that reset only at the on-entry handlers
    // it actually implements).
    int32_t variant10 = -1;
    int32_t variant14 = -1;
    int32_t variant18 = -1;

    int32_t landingKind = -1;  // +0x1c: enter-side "surface" token source AND exit-side "landing kind" (Sec9.0 item3/Sec6.1)
    int32_t sideIndex = -1;    // +0x20: enter-side "side" token source (0..5, Sec9.1); also the exit per-frame's exit-seat choice in the real engine (not reproduced)
    int32_t seatIndex = -1;    // +0x2c: seat index, 0 = driver (Sec7.2; set by the commit from the enter request, Sec8.2 step 3)

    int32_t exitWaitSeatTimerMs = -1;  // +0x38: 0x16's exit-seat wait timer (Sec6.1)

    // +0x84 (request-relative; Sec10.2 step8/Sec10.10): the 800ms stick-
    // abort grace armed on entering state 0. -1 = unarmed.
    int32_t stickAbortGraceMs = -1;

    // +0x24: several different roles by unit/state, all sharing this one
    // slot-relative offset - Sec9.3's "prepare lift"/"prepare pull" source
    // for state 0x0a, Sec14.3 step2's traverse point-type code for 0x0b,
    // and Sec11.5.2/Sec11.6's exit-side timing-parameter selector for
    // 0x1f/0x21 (consumer of the latter not found by U6.7 either).
    int32_t interactionPointType = -1;

    // +0x28 (ENTER side - distinct from the exit request's own +0x28,
    // "start time", Sec6.2): a new timer, read through "expired" by both
    // 0x0d's and 0x13's per-frame handlers (Sec14.0 item9/Sec14.11). No
    // writer was found by U6.5's own pass (OPEN item, Sec14.13 item5/
    // Sec14.0 item9) - -1 (never armed) is the safe default: it is always
    // read as "not yet expired" until a caller explicitly arms it.
    int32_t enterTimerMs = -1;

    ExitRequestRecord exitRequest;      // +0xa0..+0xd2 (Sec6.2)
    EnterRequestRecord enterRequest;    // +0x5c.. (minimal, Sec7.2)

    // See VehicleOccupancyExternalSignals's own doc comment (U6.5/U6.7).
    VehicleOccupancyExternalSignals external;

    // Observable-only (not a real engine field): records that OnExit15 saw
    // the seat's "Equip Rifle on Exit" flag set (Sec6.7.3) - see
    // ExitRequestRecord::equipRifleOnExitResolved's own doc comment.
    bool equipRifleOnExitRequested = false;

    // Observable-only (not a real engine field, and NOT a clip name - this
    // registry has no real per-clip data, see VehicleOccupancyExternalSignals):
    // true when the most recent on-entry/per-frame lookup in this slot hit
    // and (notionally) started playing something, i.e. 0x00af45a0 would
    // have been called. Surfaced for a test/caller; not consumed by any
    // transition logic in this slice.
    bool hasActiveAnimation = false;
};

// A vehicle seat record (Sec7.3: "vehicle +0x1340 + 0x30*seat"). Minimal:
// only the three sub-fields the commit/release touch in this slice.
struct VehicleSeatRecord {
    uint64_t claim = 0;          // +0x00: who is entering or seated
    uint64_t nextClaim = 0;      // +0x10: the next claim
    uint64_t seatedOccupant = 0; // +0x18: the seated occupant
};

inline constexpr int kSlotsPerRecord = 8;  // Sec7.1

struct VehicleInteractionRecord {
    uint64_t vehicleHandle = 0;  // +0x00, 0 = free record (Sec7.1)
    std::array<VehicleSlot, kSlotsPerRecord> slots{};
    std::array<VehicleSeatRecord, kSlotsPerRecord> seats{};
    uint64_t driverHandle = 0;   // +0x16d8/+0x16dc, written only in seat 0 (Sec8.1)

    // --- U6.5/U6.7 vehicle-level external signals (see
    // VehicleOccupancyExternalSignals's own doc comment for the general
    // judgement call) - these are properties of the VEHICLE, not of any
    // one occupant's slot, so they live here instead. ---
    bool isTankVehicle = false;          // Sec14.5 step2 (0x00ad3140) - vehicle-class data not modelled; caller-settable per record.
    bool vehicleInstantiatedNow = true;  // the per-frame object-flag test (+0x33 bit3 0x08) every state in this unit's "not instantiated -> cancel-and-resubmit" idiom uses - distinct from RequestEnterVehicle's own submit-time hint.
};

// ===========================================================================
// U6.1 - the element-selection rule (Sec9), as free functions over a
// caller-supplied temporary - see the file banner for why this is the right
// shape, not a scope reduction.
// ===========================================================================

// A minimal, slot-SHAPED input for the key builder (Sec9.3): deliberately
// not VehicleSlot itself, so the rule can be run on a throwaway record a
// caller builds for a single probe (seat scoring, the hijack chooser, the
// failed-hijack pick, the next-state chooser - Sec9.0 item 7), without
// needing a real registry slot at all.
struct ElementKeyInputs {
    int32_t state = -1;        // +0x0c: selects the case (Sec9.3 step 1)
    int32_t variant10 = -1;    // +0x10
    int32_t variant14 = -1;    // +0x14
    int32_t variant24 = -1;    // +0x24: "prepare lift"(2)/"prepare pull"(else), state 0x0a only
    int32_t surfaceRaw = -1;   // +0x1c: -1 none, 0 air, 1 ground, 2 vehicle, 3 water (Sec9.0 item3/Sec9.3 step2)
    int32_t sideRaw = -1;      // +0x20: 0 left,1 right,2 front,3 front left,4 front right,5 back, else none (Sec9.1)
    bool traverseNoDoor = false;    // request +0x7d (+0x21) bit2 set AND bit3 clear (Sec9.3, state 0x0b)
    bool enterConvertible = false;  // request +0x7d bit6 (Sec9.3, state 0x0f)
    bool exitConvertible = false;   // exit request +0xd1 (+0x31) bit0 (Sec9.3, state 0x1f; Sec9.8 OPEN69)
};

// The key builder (0x00b0a6f0, Sec9.3, 326 instructions): always returns
// exactly 5 tokens, zero-filling ("none") any position the real builder
// would leave uninitialised on a "short key" (Sec9.7 item 1's own
// instruction: "Zero-fill the unwritten positions of a short key").
std::array<std::string, 5> BuildElementKey(const ElementKeyInputs& in);

// The wildcard matcher (0x00b0a650, Sec9.1/Sec9.4): the first Element of
// `set` whose first 5 Parameters match `key` position by position, where a
// KEY token of "any" (case-insensitive) matches any Element token at that
// position (an Element that itself says "any" is NOT a wildcard, Sec9.7
// item 2). Position 5 is never compared here (ride-only, Sec9.1/Sec9.4.1
// item4: this rule never reads vi_ride.xtbl at all). A silent miss returns
// nullopt (Sec9.4.2/Sec9.7 item5: no diagnostic is reproduced).
std::optional<std::string> MatchElementKey(
    const std::vector<sr3tables_vehicle_world::VehicleInteractionAnimationElement>& set,
    const std::array<std::string, 5>& key);

// Sec9.4.1's set choice: the enter set in phase Enter, the exit set
// otherwise. The change-seat seat exception (Sec9.4.1: "seat = slot +0x2c
// in state 0x1b, otherwise *(the caller's exit-seat pointer)") is the
// CALLER's job - `in` must already carry the right state/variant fields for
// whichever seat's set is passed in.
std::optional<std::string> FindAnimation(
    VehicleSlotPhase phase, const ElementKeyInputs& in,
    const std::vector<sr3tables_vehicle_world::VehicleInteractionAnimationElement>& enterSet,
    const std::vector<sr3tables_vehicle_world::VehicleInteractionAnimationElement>& exitSet);

// ===========================================================================
// U6.6 - the finaliser's outcome (Sec6.11), surfaced for a caller/test
// instead of acting on it (see OnExit19Finalise's own doc comment).
// ===========================================================================
struct ExitFinaliseResult {
    int32_t oldVariant10 = -1;  // the slot's +0x10 just before this slice resets it (Sec6.11 step1; meaning OPEN, see doc comment)
    enum class Landing { kAirborne, kWater, kGround } landing = Landing::kGround;  // Sec6.11 step 11's branch (ragdoll not modelled - always the "not ragdolled" path)
};

// ===========================================================================
// U6.0/U6.1/U6.4/U6.6 - the registry.
// ===========================================================================
class VehicleInteractionRegistry {
public:
    static constexpr size_t kCapacity = 48;  // Sec7.1

    // --- U6.0: find/allocate/claim/release (Sec7.3, all CONFIRMED there) ---
    VehicleInteractionRecord* FindByVehicle(uint64_t vehicleHandle);
    // Find-or-allocate a free record for this vehicle handle (0x00b08400 +
    // 0x00b08560's role together); nullptr when the registry is full
    // (Sec7.4 step 7's "no free record -> false").
    VehicleInteractionRecord* ClaimRecord(uint64_t vehicleHandle);
    // 0x00b085d0: the first free slot in `rec`, reset and claimed for
    // `occupantHandle`; -1 if the record's 8 slots are all occupied.
    int AllocateSlot(VehicleInteractionRecord& rec, uint64_t occupantHandle);
    // 0x00b086d0: clears the seat record's claim (if held), writes +0x16d4
    // = 0 (Sec7.8), clears this module's own seat/link maps, and resets the
    // slot to free.
    void ReleaseSlot(VehicleInteractionRecord& rec, int slotIndex);

    // 0x00b08380/0x00b08390's combined role: the record and slot index
    // currently holding this occupant, or {nullptr, -1}.
    std::pair<VehicleInteractionRecord*, int> FindSlotByOccupant(uint64_t occupantHandle);

    // --- U6.0: the three setters (Sec7.5) ---
    // Each dispatches to this slice's on-entry handler table; `send` is
    // accepted for signature fidelity but is a no-op (network is parked
    // project-wide, Sec7.7/Sec7.10 point 6: "the network send ... can be
    // stubbed"). A state with no on-entry handler (the 5 never-held
    // numbers) leaves the slot's state field UNCHANGED - this is how a test
    // derives the 30-live/5-never-held split straight from this dispatcher,
    // rather than from a second, separately-maintained list.
    void SetEnter(VehicleInteractionRecord& rec, int slotIndex, int32_t state, bool send = true);
    void SetExit(VehicleInteractionRecord& rec, int slotIndex, int32_t state, bool send = true,
                 bool fromNetwork = false);
    void SetRide(VehicleInteractionRecord& rec, int slotIndex, int32_t state);

    // --- U6.0: a scoped-down submitter (Sec7.4 steps 7/8/11 ONLY) ---
    // Steps 1-6/9/10 (eligibility 0x00b0ca60, vehicle/seat resolution
    // 0x00b0fdf0/0x00b105c0, the forced-instant rules) are U6.2's own body
    // and are not reproduced - this just claims a record, allocates a slot,
    // copies the request and calls SetEnter(..., 0, true) ("every
    // submission starts at state 0", Sec7.4's own closing line). This does
    // NOT drive the route any further - use RequestEnterVehicle() (U6.3)
    // below for the real routed entry, or CommitSeating() (U6.4) to skip
    // straight to the commit for a focused test.
    std::pair<VehicleInteractionRecord*, int> SubmitEnterRequest(uint64_t vehicleHandle,
                                                                  uint64_t occupantHandle,
                                                                  EnterRequestRecord request);

    // --- U6.3: the enter route (Sec10) - submit (state 0) through the hub
    // (state 1) and, for an instant request, all the way to the commit
    // (0 -> 1 -> (2 ->) 4 -> 9), exactly as Sec10.4.2 describes it as "a
    // synchronous call chain". The real 250ms seat-resolution upkeep that
    // carries state 0 to 1 (Sec7.6) is U6.2 territory and is not modelled
    // separately - this slice treats that hand-off as immediate, folding it
    // into one call (the same simplification RequestExit already makes on
    // the exit side). `vehicleInstantiated` and `hasEnterAnimationsForSeat`
    // reproduce the hub's force-instant pre-rules (Sec10.3 step11).
    //
    // NOT reproduced (each needs a system this slice does not touch): the
    // local-player-only side effects of state 0's on-entry (Sec10.2 steps
    // 3-5: leaving fine aim/scope view, cancelling a reload/skydive, ending
    // animation states), the vehicle-AI reaction of the hub's step16, the
    // "Should Hide Big Hats" customisation of step13, the convertible-entry
    // dice-roll and the real navigation of state 5 (both U6.3's own walk
    // machinery, stubbed here), the arrival router's snap-to-point (state
    // 6), and the animated/forced/fast-entry bodies themselves (U6.3 routes
    // to 0x0a-0x0f/0x11/0x13 correctly; their interior is U6.5's).
    struct EnterRouteResult {
        VehicleInteractionRecord* record = nullptr;
        int slotIndex = -1;
    };
    EnterRouteResult RequestEnterVehicle(uint64_t vehicleHandle, uint64_t occupantHandle,
                                          EnterRequestRecord request, bool vehicleInstantiated = true,
                                          bool hasEnterAnimationsForSeat = true);

    // Sec10.0 item8/Sec10.2 per-frame/Sec10.6 step1: the stick-abort, local-
    // player only, with an 800ms grace, never for an instant request. Only
    // reproduced for state 0 (Sec10.2) - state 5's own copy (Sec10.6) needs
    // the real navigator this slice does not model. Call once per frame
    // with whatever the input layer reports; a non-zero walk axis after the
    // grace has expired cancels the entry (set-enter 8).
    void TickStickAbort(VehicleInteractionRecord& rec, int slotIndex, int32_t dtMs, bool isLocalPlayer,
                         bool anyWalkAxisNonZero);

    // --- U6.4: the seating commit, scoped to its CONFIRMED observable core
    // (Sec8.1's own "minimal stub" list) ---
    //
    // NOT reproduced here (each needs a system this slice does not touch -
    // see the file banner): weapon holstering (Sec8.4), the ~39 vehicle-band
    // callees (Sec8.3: door/engine/AI-passenger bookkeeping), the vehicle-AI
    // drunk-driver effect, player-only HUD/tutorial/cheat effects, the
    // team-check immediate re-exit (Sec8.2 step 33 - no "team" field exists
    // anywhere in this project to compare), and the camera hand-off
    // (Sec8.6). What IS reproduced: slot phase/state/seat (Sec8.2 steps 1,3),
    // the seat record and +0x16c8 (Sec8.2 step5/Sec8.1), **condition-row
    // +0xcc8 = 8 (seat 0) / 9 (elsewhere)** (Sec8.2 step21/Sec8.1 - the
    // task's own central ask), the driver-handle write for seat 0 (Sec8.2
    // step24/Sec8.1), and the trailing set-ride (Sec8.2 step29).
    struct CommitSeatingResult {
        VehicleInteractionRecord* record = nullptr;
        int slotIndex = -1;
    };
    CommitSeatingResult CommitSeating(uint64_t vehicleHandle, uint64_t occupantHandle,
                                       int32_t seatIndex, bool forced = false,
                                       bool skipTeamCheck = false);

    // --- U6.6: the exit request (0x00af3cd0, Sec6.3) ---
    struct ExitPermissionInputs {
        bool cannotExitVehicles = false;   // +0x1c9c bit 8 (Sec6.3.2 rule 3, first bullet)
        bool cannotExitRcVehicle = false;  // +0x1c9c bit 9 (second bullet)
        // vehicle handle == the occupant's own active-vehicle pair
        // +0x16d8/+0x16dc (Sec6.3.2's "same"): default true, since in this
        // slice the occupant's "active vehicle" IS the one holding its slot.
        bool sameVehicleAsActive = true;
        bool isDead = false;       // 0x0096f4f0 (Sec6.3.2 rule3's escape hatch)
        bool isRagdolled = false;  // 0x009a1dc0 (same)
    };
    // Sec6.3.1's procedure, scoped down: step 1 (no human/no vehicle handle)
    // is folded into "no slot found"; step 3 (clear being_vacuumed) is
    // skipped (no such flag is modelled); step 5's local-player/UI gate
    // (0x00818570) is skipped (not applicable outside that UI, OPEN here).
    // `hasVehicleInteractionInfo = false` reproduces step 4's "a vehicle
    // with no Vehicle_Interaction_Info record always exits without
    // animation" rule by forcing `request.instant`.
    bool RequestExit(uint64_t occupantHandle, ExitRequestRecord request,
                      bool hasVehicleInteractionInfo = false, bool humanForcedInstantHint = false,
                      ExitPermissionInputs permission = {});

    // --- Per-frame tick across the whole registry (Sec7.6 step 5's "phase
    // dispatch" only - steps 1-4's record reset/dead-occupant release/
    // seat-resolution upkeep/ownership upkeep are U6.2 territory and are
    // not reproduced). Drives: exit states 0x14 and 0x16 (U6.6, Sec6.1's
    // own table) and enter states 0x02/0x04 (U6.3's instant route, Sec10.5
    // "the per-frame handler retries every frame"). Every other state is a
    // no-op per tick, matching the real dispatcher exactly. Does NOT drive
    // the stick-abort (TickStickAbort, above) - that needs device input
    // this generic tick has no parameter for.
    // Sec14/Sec11's own live states (0x0a-0x0f/0x11/0x13, 0x1b/0x1d/0x1f/
    // 0x21) are ALSO dispatched here now - each one's own per-frame wait
    // is driven by the caller-supplied signals on VehicleSlot::external
    // (and the two vehicle-level booleans on VehicleInteractionRecord),
    // not by this generic tick's own parameters, so no signature change
    // was needed to wire them in (see the file banner's ARCHITECTURE
    // NOTE). Every one of these new signals defaults to "nothing has
    // happened yet" (not finished, not local player's axis pushed, ...),
    // so an existing test's bare Tick() call cannot newly reach any of
    // them unless it explicitly sets one.
    void Tick(int32_t dtMs);

    // Sec7.5/Sec9.5's "next-state chooser" `0x00afb220` (called by the
    // per-frame handlers of 0x0a/0x0b/0x0c/0x0d): only two of its seven
    // CONFIRMED outcomes have a known mechanism (see the file banner).
    // A test/caller can set this to supply the full result directly -
    // ProbeOpenVsTraverse/ProbeExtractFailVsExtract below are the two
    // CONFIRMED building blocks to build it from. Left unset, reaching
    // this chooser throws sr3luahost::OpenStateError (the open-dependency
    // shape this project's OpenValue/OpenStateError exist for).
    std::function<int32_t(VehicleInteractionRecord&, int, int32_t /*flag*/)> nextStateChooserOverride;

    // The chooser's two CONFIRMED internal probes (Sec9.5's own citation
    // of 0x00afb220), reproduced for real as far as the spec goes - both
    // stand in for an actual table hit via
    // VehicleSlot::external.animationLookupHit, since this registry owns
    // no real vi_enter.xtbl/vi_exit.xtbl data (see the file banner).
    int32_t ProbeOpenVsTraverse(VehicleInteractionRecord& rec, int i);         // -> kEnterOpen (match) or kEnterTraverse (Sec9.5 row a)
    int32_t ProbeExtractFailVsExtract(VehicleInteractionRecord& rec, int i);   // -> kEnterExtractFailPick (match) or kEnterExtract (Sec9.5 row b)

    // --- Observability: this module's OWN occupant-keyed stand-ins for the
    // shared engine fields it writes (see the file banner's integration
    // note) ---
    int32_t ConditionRow(uint64_t occupantHandle) const;      // +0xcc8; 0 if never written
    int32_t StateEnum(uint64_t occupantHandle) const;         // +0x16d4; 0 if never written
    std::optional<int> SeatOf(uint64_t occupantHandle) const;  // +0x16c8

    // Test observability for Sec7.0 item2's "30 live / 5 never-held" claim:
    // every on-entry handler this file dispatches to (including the generic
    // StubOnEntry, Sec7.0 item2's universal "stores its own state number"
    // fact) appends its OWN state number here when enabled, in call order -
    // including handlers reached only through an internal cascade (e.g. the
    // commit's trailing set-ride, Sec8.2 step29). This lets a test DERIVE
    // which of the 35 numbers can ever be stored by exercising the real
    // dispatcher, rather than comparing two independently hand-written
    // lists. Off by default (zero cost in normal use).
    bool traceEnabled = false;
    std::vector<int32_t> trace;

private:
    void RecordTrace(int32_t state) { if (traceEnabled) trace.push_back(state); }

    void DispatchEnterOnEntry(VehicleInteractionRecord& rec, int i, int32_t state);
    void DispatchExitOnEntry(VehicleInteractionRecord& rec, int i, int32_t state);
    void DispatchRideOnEntry(VehicleInteractionRecord& rec, int i, int32_t state);

    // The ONE universal, U6.0-owned fact shared by every on-entry handler
    // this slice does not otherwise implement (Sec7.0 item 2): it stores
    // its own state number and nothing else.
    void StubOnEntry(VehicleInteractionRecord& rec, int i, int32_t state);

    void OnEnterRequested(VehicleInteractionRecord& rec, int i);     // state 0 (Sec10.2)
    void OnEnterHub(VehicleInteractionRecord& rec, int i);            // state 1 (Sec10.3)
    void OnEnterInstantWaitSeat(VehicleInteractionRecord& rec, int i);  // state 2 (Sec10.5)
    void TickEnterInstantWaitSeat(VehicleInteractionRecord& rec, int i, int32_t dtMs);
    void OnEnterInstantTakeSeat(VehicleInteractionRecord& rec, int i);  // state 4 (Sec10.5)
    void TickEnterInstantTakeSeat(VehicleInteractionRecord& rec, int i, int32_t dtMs);
    void OnEnterCancel(VehicleInteractionRecord& rec, int i);          // state 8 (Sec10.9)
    void OnEnterCommit(VehicleInteractionRecord& rec, int i);          // state 9 (Sec8.2)
    void OnRideActive(VehicleInteractionRecord& rec, int i);           // state 0x22 (Sec8.5)

    void OnExit14Requested(VehicleInteractionRecord& rec, int i);
    void TickExit14(VehicleInteractionRecord& rec, int i, int32_t dtMs);
    void OnExit15LeaveSeat(VehicleInteractionRecord& rec, int i);
    void OnExit16WaitSeat(VehicleInteractionRecord& rec, int i);
    void TickExit16(VehicleInteractionRecord& rec, int i, int32_t dtMs);
    void OnExit17InstantPlacement(VehicleInteractionRecord& rec, int i);
    void OnExit18Parked(VehicleInteractionRecord& rec, int i);
    ExitFinaliseResult OnExit19Finalise(VehicleInteractionRecord& rec, int i);

    // --- U6.3 Sec10.4.3: cancel-and-resubmit, reused unchanged by U6.5/
    // U6.7 (every failure path in this range falls back to it). Mirrors
    // RequestEnterVehicle's own judgement call of folding the 250ms
    // upkeep hand-off into one synchronous call, for the same reason
    // (Sec10.4.3's own callers describe it as "snap in instead of
    // cancelling" - in practice the resubmission reaches the instant
    // route almost immediately). ---
    EnterRouteResult CancelAndResubmit(VehicleInteractionRecord& rec, int i);

    // --- U6.5 (Sec14): the eight animated enter states' own bodies ---
    void OnEnterPrepare(VehicleInteractionRecord& rec, int i);              // 0x0a (Sec14.2)
    void TickEnterPrepare(VehicleInteractionRecord& rec, int i, int32_t dtMs);
    void OnEnterTraverse(VehicleInteractionRecord& rec, int i);             // 0x0b (Sec14.3)
    void TickEnterTraverse(VehicleInteractionRecord& rec, int i, int32_t dtMs);
    void OnEnterOpen(VehicleInteractionRecord& rec, int i);                 // 0x0c (Sec14.4)
    void TickEnterOpen(VehicleInteractionRecord& rec, int i, int32_t dtMs);
    void OnEnterExtract(VehicleInteractionRecord& rec, int i);              // 0x0d (Sec14.5)
    void TickEnterExtract(VehicleInteractionRecord& rec, int i, int32_t dtMs);
    void OnEnterExtractFailPick(VehicleInteractionRecord& rec, int i);      // 0x0e (Sec14.6)
    void TickEnterExtractFailPick(VehicleInteractionRecord& rec, int i, int32_t dtMs);
    void OnEnterForcedInPlace(VehicleInteractionRecord& rec, int i);        // 0x0f (Sec14.7, cited from Sec10.4.4)
    void TickEnterForcedInPlace(VehicleInteractionRecord& rec, int i, int32_t dtMs);
    void OnEnterChangeSeat(VehicleInteractionRecord& rec, int i);           // 0x11 (Sec14.9)
    void TickEnterChangeSeat(VehicleInteractionRecord& rec, int i, int32_t dtMs);
    void OnEnterFast(VehicleInteractionRecord& rec, int i);                 // 0x13 (Sec14.10)
    void TickEnterFast(VehicleInteractionRecord& rec, int i, int32_t dtMs);

    // Sec9.5's hijack chooser (0x00afa540), reused unchanged by U6.5
    // (Sec14.5/Sec14.10). Writes the caller's own slot +0x10 (the
    // hijacker's chosen kind) and sends the victim (the entry seat's
    // current occupant, if any) an animated exit request. Real candidate
    // enumeration against vi_exit.xtbl is not reproduced (no such table
    // is wired into this registry) - see VehicleOccupancyExternalSignals.
    int32_t HijackChooserPickKind(VehicleInteractionRecord& rec, int i);

    // Sec11.5.2's exit-kind chooser (0x00b04ae0), U6.7's own. `flagForced`
    // selects the forced-kinds branch (guardian angel / eject); the
    // cascade's own vehicle-geometry predicates come from the slot's own
    // VehicleOccupancyExternalSignals (see the file banner) and default
    // to the spec's own named "implicit default" (kExitKindStandard)
    // when none apply. Writes slot +0x10 (and +0x14/+0x24 where the
    // spec gives a sub-variant) for real, matching the real chooser.
    int32_t ChooseExitKind(VehicleInteractionRecord& rec, int i, bool flagForced);

    // Sec11.7.2's fast-entry starter (0x00b01460), cited unchanged by
    // U6.5 (0x13's own on-entry/per-frame) and called by U6.7's own 0x1d
    // on-entry ON THE HIJACKER'S SLOT (Sec11.4.1 step5/Sec11.7.2's "three
    // contexts" finding) - hence it takes the record/slot to act on
    // explicitly rather than assuming "this slot".
    void StartFastEntry(VehicleInteractionRecord& rec, int i);

    // Sec11.4.1 step3: the hijacker-resync scan - the other slot (phase
    // Enter, state 0x0d or 0x13) whose own enter-request target seat
    // equals `seat`, or -1.
    int FindHijackerSlotTargetingSeat(VehicleInteractionRecord& rec, int seat);

    // --- U6.7 (Sec11): the four animated exit states' own bodies ---
    void OnExitChangeSeat(VehicleInteractionRecord& rec, int i);            // 0x1b (Sec11.3)
    void TickExitChangeSeat(VehicleInteractionRecord& rec, int i, int32_t dtMs);
    void OnExitExtract(VehicleInteractionRecord& rec, int i);               // 0x1d (Sec11.4)
    void TickExitExtract(VehicleInteractionRecord& rec, int i, int32_t dtMs);
    void OnExitExit(VehicleInteractionRecord& rec, int i);                  // 0x1f (Sec11.5)
    void TickExitExit(VehicleInteractionRecord& rec, int i, int32_t dtMs);
    void OnExitTraverse(VehicleInteractionRecord& rec, int i);              // 0x21 (Sec11.6)
    void TickExitTraverse(VehicleInteractionRecord& rec, int i, int32_t dtMs);

    int32_t NextStateChooser(VehicleInteractionRecord& rec, int i, int32_t flag);

    void ReleaseSlotInternal(VehicleInteractionRecord& rec, int i);

    std::vector<VehicleInteractionRecord> records_;
    std::unordered_map<uint64_t, int32_t> conditionRows_;  // +0xcc8
    std::unordered_map<uint64_t, int32_t> stateEnums_;     // +0x16d4
    std::unordered_map<uint64_t, int> seatOf_;             // +0x16c8
};

}  // namespace sr3player
