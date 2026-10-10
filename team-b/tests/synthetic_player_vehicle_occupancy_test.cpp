// Synthetic tests for the player runtime "U6 vehicle minimum slice": U6.0
// (slot registry/state skeleton), U6.1 (element-selection rule), U6.4 (the
// seating commit) and U6.6 (exit request + non-animated exit states 0x14-
// 0x19), spec-player-runtime.md Sec6/Sec7/Sec8/Sec9. Every expected value
// below is a rule quoted or derived from that text (cited per block); no
// real vi_enter.xtbl/vi_exit.xtbl data is read here (none is checked into
// this clean-room repo) - the element-selection fixtures are hand-built,
// matching this project's synthetic_tables_vehicle_world_test.cpp style.
#include <algorithm>
#include <iostream>
#include <set>
#include <stdexcept>
#include <vector>

#include "sr3luahost/open_state.h"
#include "sr3player/vehicle_occupancy.h"

using namespace sr3player;
using sr3tables_vehicle_world::VehicleInteractionAnimationElement;

namespace {

int g_failures = 0;
#define CHECK(cond)                                                                           \
    do {                                                                                      \
        if (!(cond)) {                                                                        \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                     \
        }                                                                                     \
    } while (0)

VehicleInteractionAnimationElement MakeElement(std::vector<std::string> params, std::string anim) {
    VehicleInteractionAnimationElement e;
    e.parameters = std::move(params);
    e.animation = std::move(anim);
    return e;
}

// ===========================================================================
// U6.0 - derive the reachable/unreachable state split from the ACTUAL
// dispatcher (SetEnter/SetExit/SetRide), not from a second hand-written list
// (spec-player-runtime.md Sec7.0 item2/item3, Sec7.5's full table).
// ===========================================================================
void testStateReachability() {
    // Derive reachability from the TRACE of every on-entry handler actually
    // invoked (Sec7.0 item2: "each on-entry handler stores its own state
    // number") - not from the slot's FINAL settled state, since a handler
    // this slice implements (the commit, or the exit sextet) may cascade
    // straight into a further state in the same call (e.g. the commit ends
    // in ride, Sec8.2 step29) without that meaning the intermediate state
    // was never stored.
    VehicleInteractionRegistry reg;
    reg.traceEnabled = true;
    std::vector<int32_t> neverHeld;
    std::vector<int32_t> live;

    // Enter range 0x00-0x13 (Sec7.5): SetEnter is the real domain for these.
    for (int32_t s = 0x00; s <= 0x13; ++s) {
        auto* rec = reg.ClaimRecord(0x1000 + s);
        int slot = reg.AllocateSlot(*rec, 1);
        reg.trace.clear();
        reg.SetEnter(*rec, slot, s, false);
        const bool stored = std::find(reg.trace.begin(), reg.trace.end(), s) != reg.trace.end();
        (stored ? live : neverHeld).push_back(s);
    }
    // Exit range 0x14-0x21 (Sec7.5): SetExit's real domain.
    for (int32_t s = 0x14; s <= 0x21; ++s) {
        auto* rec = reg.ClaimRecord(0x2000 + s);
        int slot = reg.AllocateSlot(*rec, 1);
        // State 0x21 (traverse) always re-runs 0x1f's entire on-entry
        // first (Sec11.0 item8/Sec11.6 step1) and ONLY re-stamps its own
        // state number when the exit-kind chooser genuinely leaves the
        // kind unresolved (Sec11.6 step2) - this slice's own default
        // cascade (ChooseExitKind) always resolves a kind, so this probe
        // needs the documented override to exercise that rarer, but
        // U6.0-census-confirmed-live, path (see ChooseExitKind's own doc
        // comment on VehicleOccupancyExternalSignals::
        // exitKindChooserCannotResolve).
        if (s == 0x21) rec->slots[slot].external.exitKindChooserCannotResolve = true;
        reg.trace.clear();
        reg.SetExit(*rec, slot, s, false, true /*fromNetwork: skip the tip-over test's own effects*/);
        const bool stored = std::find(reg.trace.begin(), reg.trace.end(), s) != reg.trace.end();
        (stored ? live : neverHeld).push_back(s);
    }
    // Ride: only 0x22 (Sec7.5).
    {
        auto* rec = reg.ClaimRecord(0x3000);
        int slot = reg.AllocateSlot(*rec, 1);
        reg.trace.clear();
        reg.SetRide(*rec, slot, 0x22);
        CHECK(rec->slots[slot].state == 0x22);
        live.push_back(0x22);
    }

    // Sec7.0 item2: "Only 30 of the 35 numbers are live."
    CHECK(live.size() == 30);
    CHECK(neverHeld.size() == 5);
    std::set<int32_t> neverHeldSet(neverHeld.begin(), neverHeld.end());
    CHECK((neverHeldSet == std::set<int32_t>{0x10, 0x12, 0x1a, 0x1e, 0x20}));
}

// A state outside the 5 never-held numbers must actually store its own
// number via the generic skeleton stub (Sec7.0 item2: "each on-entry
// handler stores its own state number") even when this slice does not
// implement that state's real per-unit behaviour (e.g. enter state 5, the
// walk to the vehicle - U6.3's own walk machinery, not reproduced here).
void testStubStatesStillStoreTheirNumber() {
    VehicleInteractionRegistry reg;
    auto* rec = reg.ClaimRecord(42);
    int slot = reg.AllocateSlot(*rec, 7);
    reg.SetEnter(*rec, slot, 5, false);  // enter state 5 (walk) - U6.3's own walk machinery, not this slice's
    CHECK(rec->slots[slot].state == 5);
    reg.SetEnter(*rec, slot, 6, false);  // enter state 6 (arrival router) - U6.3's own body, not this slice's
    CHECK(rec->slots[slot].state == 6);
}

// ===========================================================================
// U6.1 - the element-selection rule (Sec9.3's key builder, Sec9.1/Sec9.4's
// matcher), against hand-built fixtures.
// ===========================================================================
void testKeyBuilderShortKeyZeroFill() {
    // Sec9.4.3: state 0x1d (extract, exit side) with +0x10 == -1 is a short
    // key (only "extract" is written) - the remaining positions must be
    // zero-filled to "none" per Sec9.7 item1's own clean-room instruction.
    ElementKeyInputs in;
    in.state = 0x1d;
    in.variant10 = -1;
    in.surfaceRaw = 1;  // ground
    in.sideRaw = 0;      // left
    const auto key = BuildElementKey(in);
    // Short key: surface/side land at positions 1/2 (not 3/4), the rest "none".
    CHECK(key[0] == "extract");
    CHECK(key[1] == "ground");
    CHECK(key[2] == "left");
    CHECK(key[3] == "none");
    CHECK(key[4] == "none");
}

void testKeyBuilderExtractVariantOffsetBy9() {
    // Sec9.3: state 0x1d's extract-kind list is "the 0x0d list, offset by 9"
    // - 0x0d's variant10=3 ("extract bash") must equal 0x1d's variant10=0x0c
    // (3+9).
    ElementKeyInputs a;
    a.state = 0x0d; a.variant10 = 3; a.surfaceRaw = 1; a.sideRaw = 1;
    ElementKeyInputs b;
    b.state = 0x1d; b.variant10 = 0x0c; b.surfaceRaw = 1; b.sideRaw = 1;
    const auto keyA = BuildElementKey(a);
    const auto keyB = BuildElementKey(b);
    CHECK(keyA[1] == "extract bash");
    CHECK(keyB[1] == "extract bash");
    CHECK(keyA[0] == keyB[0] && keyA[2] == keyB[2] && keyA[3] == keyB[3] && keyA[4] == keyB[4]);
}

void testKeyBuilderChangeSeatAnySurface() {
    // Sec9.3 step2: states 0x11/0x1b always get surface = "any" ("change
    // seat matches every surface"), regardless of the raw surface field.
    ElementKeyInputs in;
    in.state = 0x1b;
    in.surfaceRaw = 3;  // water - must be overridden to "any" anyway
    in.sideRaw = 2;      // front
    const auto key = BuildElementKey(in);
    CHECK(key[0] == "change seat");
    CHECK(key[1] == "none");
    CHECK(key[2] == "none");
    CHECK(key[3] == "any");
    CHECK(key[4] == "front");
}

void testKeyBuilderExitConvertibleSubstitution() {
    // Sec9.3: state 0x1f, +0x10 = 0x14 ("exit standard"), substituted to
    // "exit convertible" when exit-request +0x31 bit0 is set; token2 stays
    // "none" either way (Sec9.3's "Token 2" table: "every other kind: none").
    ElementKeyInputs plain;
    plain.state = 0x1f; plain.variant10 = 0x14; plain.exitConvertible = false;
    ElementKeyInputs conv = plain;
    conv.exitConvertible = true;
    CHECK(BuildElementKey(plain)[1] == "exit standard");
    CHECK(BuildElementKey(conv)[1] == "exit convertible");
    CHECK(BuildElementKey(plain)[2] == "none");
    CHECK(BuildElementKey(conv)[2] == "none");
}

void testKeyBuilderExtractFailAnyWildcard() {
    // Sec9.3: states 0x0e/0x1e with +0x14 == 0 write "any" as the third
    // token (the hijacker's own key, matched against the VICTIM's set,
    // Sec9.5) - this is a real, written wildcard token, not a short key.
    ElementKeyInputs in;
    in.state = 0x0e;
    in.variant14 = 0;
    in.surfaceRaw = 1;
    in.sideRaw = 1;
    const auto key = BuildElementKey(in);
    CHECK(key[0] == "extract");
    CHECK(key[1] == "extract fail");
    CHECK(key[2] == "any");
    CHECK(key[3] == "ground");
    CHECK(key[4] == "right");
}

void testMatcherWildcardAndFirstMatchWins() {
    // Sec9.1: a KEY token of "any" matches any Element token at that
    // position; an Element that itself says "any" is not special (Sec9.7
    // item2). Sec9.0 item6: first match wins, even when a later duplicate
    // carries a different animation.
    std::vector<VehicleInteractionAnimationElement> set = {
        MakeElement({"change seat", "none", "none", "any", "left"}, "ChangeSeatLeft"),
        MakeElement({"change seat", "none", "none", "any", "right"}, "ChangeSeatRight"),
        MakeElement({"change seat", "none", "none", "any", "left"}, "ShadowedNeverReturned"),
    };
    ElementKeyInputs in;
    in.state = 0x1b;
    in.surfaceRaw = 3;  // water -> overridden to "any" by the builder; the set's own "any" at position3 matches literally too
    in.sideRaw = 0;      // left
    const auto key = BuildElementKey(in);
    const auto result = MatchElementKey(set, key);
    CHECK(result.has_value() && *result == "ChangeSeatLeft");  // first match, not the later shadowed duplicate
}

void testMatcherShortKeyMiss() {
    // Sec9.4.3: a 0x1d short key (variant10 == -1) always misses real data,
    // because no shipped "extract" Element has "none" as its own second
    // token - reproduced here with a representative fixture.
    std::vector<VehicleInteractionAnimationElement> set = {
        MakeElement({"extract", "extract standard", "none", "ground", "left"}, "ExtractStandardGroundLeft"),
    };
    ElementKeyInputs in;
    in.state = 0x1d;
    in.variant10 = -1;  // short key: "extract", "ground", "left", "none", "none"
    in.surfaceRaw = 1;
    in.sideRaw = 0;
    CHECK(!MatchElementKey(set, BuildElementKey(in)).has_value());
}

void testFindAnimationSetChoiceByPhase() {
    // Sec9.4.1: the enter set is consulted in phase Enter, the exit set
    // otherwise.
    std::vector<VehicleInteractionAnimationElement> enterSet = {
        MakeElement({"enter", "enter standard", "none", "ground", "left"}, "EnterStdGroundLeft"),
    };
    std::vector<VehicleInteractionAnimationElement> exitSet = {
        MakeElement({"exit", "exit standard", "none", "ground", "left"}, "ExitStdGroundLeft"),
    };
    ElementKeyInputs enterIn;
    enterIn.state = 0x0f;
    enterIn.variant10 = 1;  // not 0, so not guardian angel
    enterIn.variant14 = 0;  // not 4, so not shopper
    enterIn.enterConvertible = false;
    enterIn.surfaceRaw = 1;
    enterIn.sideRaw = 0;
    const auto gotEnter = FindAnimation(VehicleSlotPhase::kEnter, enterIn, enterSet, exitSet);
    CHECK(gotEnter.has_value() && *gotEnter == "EnterStdGroundLeft");

    ElementKeyInputs exitIn;
    exitIn.state = 0x1f;
    exitIn.variant10 = 0x14;  // exit standard
    exitIn.surfaceRaw = 1;
    exitIn.sideRaw = 0;
    const auto gotExit = FindAnimation(VehicleSlotPhase::kExit, exitIn, enterSet, exitSet);
    CHECK(gotExit.has_value() && *gotExit == "ExitStdGroundLeft");
}

// ===========================================================================
// U6.4 - the seating commit: condition-row +0xcc8 = 8/9 (the task's own
// central ask), seat record, driver handle (Sec8.1/Sec8.2).
// ===========================================================================
void testSeatingCommitConditionRow() {
    VehicleInteractionRegistry reg;
    const uint64_t vehicle = 500;

    auto driver = reg.CommitSeating(vehicle, /*occupant=*/1, /*seat=*/0);
    CHECK(driver.record != nullptr && driver.slotIndex >= 0);
    CHECK(reg.ConditionRow(1) == 8);   // Sec8.1: "8 in seat 0"
    CHECK(reg.StateEnum(1) == 3);      // Sec8.1: "+0x16d4 = 3"
    CHECK(reg.SeatOf(1).has_value() && *reg.SeatOf(1) == 0);
    CHECK(driver.record->slots[driver.slotIndex].state == 0x22);  // Sec8.2 step29: commit ends in ride
    CHECK(driver.record->driverHandle == 1);  // Sec8.2 step24 (seat 0 only)

    auto passenger = reg.CommitSeating(vehicle, /*occupant=*/2, /*seat=*/3);
    CHECK(reg.ConditionRow(2) == 9);   // Sec8.1: "9 ... elsewhere"
    CHECK(reg.SeatOf(2).has_value() && *reg.SeatOf(2) == 3);
    CHECK(driver.record->driverHandle == 1);  // unchanged by the passenger's own commit
}

// ===========================================================================
// U6.6 - the exit request and the six non-animated exit states (0x14-0x19).
// ===========================================================================

// The full instant-exit chain in one Tick: 0x14 -> 0x15 -> 0x17 -> 0x19 ->
// released, exactly as Sec6.13 point5 says a U6.7-stubbing port may do
// ("send every exit through the instant path ... still reproduce the
// observable end state").
void testInstantExitChainAndRelease() {
    VehicleInteractionRegistry reg;
    const uint64_t vehicle = 600;
    auto seated = reg.CommitSeating(vehicle, /*occupant=*/9, /*seat=*/0);
    CHECK(reg.ConditionRow(9) == 8);

    ExitRequestRecord req;  // defaults: not instant - forced instant below
    const bool accepted = reg.RequestExit(/*occupant=*/9, req, /*hasVehicleInteractionInfo=*/false);
    CHECK(accepted);
    // OnExit14Requested already ran inside RequestExit: condition row = 0xb,
    // state-enum = 2 (Sec6.6.1), but no auto-advance yet (vehicleMayBeGone
    // is false by default).
    CHECK(reg.ConditionRow(9) == 0x0b);
    CHECK(reg.StateEnum(9) == 2);
    CHECK(seated.record->slots[seated.slotIndex].state == kExitRequested);

    reg.Tick(0);  // drains the whole 0x14->0x15->0x17->0x19 chain at once (startDelayMs defaults to 0 = "already passed")

    CHECK(reg.ConditionRow(9) == 0);    // Sec6.11 step9: finaliser sets condition row back to 0
    CHECK(reg.StateEnum(9) == 0);       // Sec7.3 release: +0x16d4 = 0
    CHECK(!reg.SeatOf(9).has_value());  // seat link cleared on release
    CHECK(seated.record->slots[seated.slotIndex].occupantHandle == 0);  // slot freed
    auto [rec2, slot2] = reg.FindSlotByOccupant(9);
    CHECK(rec2 == nullptr && slot2 == -1);
}

// RequestExit refuses a slot that is entering or already exiting (Sec6.3.1
// step7: "in practice this means only riding (phase 3) slots are accepted").
void testRequestExitRefusesEnteringOrExitingSlot() {
    VehicleInteractionRegistry reg;
    auto [rec, slot] = reg.SubmitEnterRequest(700, 11, EnterRequestRecord{});
    CHECK(rec != nullptr && slot >= 0);
    CHECK(rec->slots[slot].phase == VehicleSlotPhase::kEnter);
    const bool accepted = reg.RequestExit(11, ExitRequestRecord{});
    CHECK(!accepted);  // phase is Enter, not Ride/None
}

// The permission-lock rule (Sec6.3.2 rule3): locked unless instant, dead or
// ragdolled.
void testRequestExitPermissionLock() {
    VehicleInteractionRegistry reg;
    auto seated = reg.CommitSeating(800, 12, 0);

    VehicleInteractionRegistry::ExitPermissionInputs locked;
    locked.cannotExitVehicles = true;
    locked.sameVehicleAsActive = false;  // "not same" + cannotExitVehicles -> locked
    ExitRequestRecord req;  // not instant
    CHECK(!reg.RequestExit(12, req, /*hasVehicleInteractionInfo=*/true, /*humanForcedInstantHint=*/false, locked));

    // The escape hatch: dead or ragdolled still gets out even when locked.
    VehicleInteractionRegistry::ExitPermissionInputs lockedButDead = locked;
    lockedButDead.isDead = true;
    CHECK(reg.RequestExit(12, req, true, false, lockedButDead));
}

// 0x16 (wait for the exit seat): the timeout sub-path is reproduced in
// full (Sec6.8's per-frame handler, "wait at own seat" branch) - the timer
// expiring forces an instant exit through 0x17/0x19.
void testExit16WaitSeatTimeout() {
    VehicleInteractionRegistry reg;
    auto seated = reg.CommitSeating(900, 13, 0);
    VehicleSlot& slot = seated.record->slots[seated.slotIndex];
    slot.phase = VehicleSlotPhase::kExit;  // drive it directly into the exit phase for this focused test
    slot.exitRequest = ExitRequestRecord{};
    slot.exitRequest.waitAtOwnSeat = true;
    slot.exitWaitSeatTimerMs = 500;
    reg.SetExit(*seated.record, seated.slotIndex, kExitWaitSeat, false, true);
    CHECK(slot.state == kExitWaitSeat);

    reg.Tick(300);  // 500 -> 200, still waiting
    CHECK(slot.state == kExitWaitSeat);

    reg.Tick(300);  // 200 -> -100, expired -> instant -> 0x17 -> 0x19 -> released
    auto [rec2, slot2] = reg.FindSlotByOccupant(13);
    CHECK(rec2 == nullptr && slot2 == -1);
}

// 0x18 (parked): "no transition and no per-frame handler" (Sec6.10) -
// reachable only by a direct call in this slice, exactly as in the real
// game it is reachable only through the network applier.
void testExit18ParkedHasNoTransition() {
    VehicleInteractionRegistry reg;
    auto seated = reg.CommitSeating(1000, 14, 0);
    VehicleSlot& slot = seated.record->slots[seated.slotIndex];
    slot.phase = VehicleSlotPhase::kExit;
    reg.SetExit(*seated.record, seated.slotIndex, kExitParked, false, true);
    CHECK(slot.state == kExitParked);
    reg.Tick(1000);
    CHECK(slot.state == kExitParked);  // unchanged: no per-frame handler for 0x18
}

// ===========================================================================
// U6.3 - the enter route (Sec10): the instant route 0 -> 1 -> (2 ->) 4 -> 9,
// the routing hub's decision table, and the one-shot cancel (state 8).
// ===========================================================================

// Sec10.0 item2/Sec10.4.2: with a free seat and an empty queue, an instant
// request commits inside the same call ("the first phase tick"), landing in
// ride with condition row 8 (driver) - the same commit this slice already
// built for U6.4, now reached through the real route instead of the
// CommitSeating() shortcut.
void testInstantRouteFreeSeatCommitsSynchronously() {
    VehicleInteractionRegistry reg;
    EnterRequestRecord req;
    req.requestedSeat = 0;
    req.instant = true;
    bool begunCalled = false, begunOk = false;
    req.onBegunCallback = [&](bool ok) { begunCalled = true; begunOk = ok; };

    auto result = reg.RequestEnterVehicle(3000, 31, req);
    CHECK(result.record != nullptr && result.slotIndex >= 0);
    CHECK(begunCalled && begunOk);  // Sec10.3 step2: fired once, ok=true
    CHECK(reg.ConditionRow(31) == 8);
    CHECK(reg.StateEnum(31) == 3);
    CHECK(reg.SeatOf(31).has_value() && *reg.SeatOf(31) == 0);
    CHECK(result.record->slots[result.slotIndex].state == kRideActive);
}

// Sec10.0 item2/Sec10.4.2 H1/H4: an instant request against an OCCUPIED seat
// evicts the current occupant (an instant exit request, not a synchronous
// kill) and routes through state 2 (wait) instead of straight to 4.
void testInstantRouteOccupiedSeatEvictsThenSeats() {
    VehicleInteractionRegistry reg;
    auto first = reg.CommitSeating(4000, 41, 0);
    CHECK(reg.ConditionRow(41) == 8);

    EnterRequestRecord req;
    req.requestedSeat = 0;
    req.instant = true;
    auto result = reg.RequestEnterVehicle(4000, 42, req);
    // Seat occupied -> H1 only REQUESTS the eviction (Sec6.6.1 step3: 0x14
    // does not auto-advance), so the new entrant waits in state 2, not yet
    // committed.
    CHECK(result.record->slots[result.slotIndex].state == kEnterInstantWaitSeat);
    CHECK(reg.ConditionRow(41) == 0x0b);  // the evicted occupant's exit was requested

    // Drain: the evicted occupant's exit chain and the waiting entrant's
    // 2->4->9 chain (up to two ticks, depending on slot iteration order
    // within the same vehicle record).
    reg.Tick(0);
    reg.Tick(0);

    CHECK(reg.ConditionRow(41) == 0);    // fully exited
    CHECK(!reg.SeatOf(41).has_value());
    CHECK(reg.ConditionRow(42) == 8);     // the new occupant now seated as driver
    CHECK(reg.StateEnum(42) == 3);
    auto [rec2, slot2] = reg.FindSlotByOccupant(42);
    CHECK(rec2 != nullptr && rec2->slots[slot2].state == kRideActive);
}

// Sec10.0 item1/Sec10.3 step15: the hub's non-instant branches route
// correctly even though their own bodies are U6.3's walk machinery/U6.5's
// animated states (stubbed here) - in particular, state 0x0f is a DISTINCT
// target from the instant route's 4/9, refuting the "0x0f is the snap
// route" HYPOTHESIS the task's own orientation flagged (Sec10.0 item2/
// Sec10.4.4).
void testHubRoutingNonInstantBranches() {
    VehicleInteractionRegistry reg;
    {
        EnterRequestRecord req;
        req.requestedSeat = 1;
        req.fastEntry = true;
        auto result = reg.RequestEnterVehicle(5000, 51, req);
        CHECK(result.record->slots[result.slotIndex].state == kEnterFast);  // 0x13
    }
    {
        EnterRequestRecord req;
        req.requestedSeat = 1;
        req.forced = true;
        auto result = reg.RequestEnterVehicle(5100, 52, req);
        CHECK(result.record->slots[result.slotIndex].state == kEnterForcedInPlace);  // 0x0f, NOT 4/9
    }
    {
        EnterRequestRecord req;
        req.requestedSeat = 1;  // ordinary: not instant, not forced, not fast
        auto result = reg.RequestEnterVehicle(5200, 53, req);
        CHECK(result.record->slots[result.slotIndex].state == 0x05);  // walk to the vehicle
    }
}

// Sec10.4.2 H1's tank branch: a player-class victim in a tank's own entry
// seat cancels the entry outright instead of being evicted.
void testTankEntryWithPlayerVictimCancelsInsteadOfEviction() {
    VehicleInteractionRegistry reg;
    reg.CommitSeating(6000, 61, 0);
    CHECK(reg.ConditionRow(61) == 8);

    EnterRequestRecord req;
    req.requestedSeat = 0;
    req.instant = true;
    req.isTankEntry = true;
    req.evictedVictimIsPlayerClass = true;
    reg.RequestEnterVehicle(6000, 62, req);

    auto [rec2, slot2] = reg.FindSlotByOccupant(62);
    CHECK(rec2 == nullptr && slot2 == -1);  // the entry was cancelled, not seated
    CHECK(reg.ConditionRow(61) == 8);        // the original occupant was never evicted
}

// Sec10.0 item7/Sec10.10/H4: request kind 1 ("remove the occupant, then
// stop") always cancels, even against a free seat.
void testRequestKindRemoveThenStopAlwaysCancels() {
    VehicleInteractionRegistry reg;
    EnterRequestRecord req;
    req.requestedSeat = 2;
    req.instant = true;
    req.requestKind = 1;
    reg.RequestEnterVehicle(7000, 71, req);

    auto [rec2, slot2] = reg.FindSlotByOccupant(71);
    CHECK(rec2 == nullptr && slot2 == -1);  // cancelled per H4, despite the free seat
}

// Sec10.0 item8/Sec10.2 per-frame/Sec10.9 step3: the stick abort is local-
// player only, has an 800ms grace, and a cancel it triggers fires the
// "begun" callback with ok=false - never the completion callback (which
// this slice does not even model, Sec10.0 item4's "it never calls the
// completion callback" reproduced by omission).
void testStickAbortCancelsAndFiresBegunFalse() {
    VehicleInteractionRegistry reg;
    EnterRequestRecord req;
    req.requestedSeat = 0;  // NOT instant - the hub (which would consume the
                             // callback with ok=true) never runs here; only
                             // SubmitEnterRequest (state 0) is called.
    bool begunCalled = false, begunOk = true;
    req.onBegunCallback = [&](bool ok) { begunCalled = true; begunOk = ok; };
    auto [rec, slot] = reg.SubmitEnterRequest(8000, 81, req);
    CHECK(rec->slots[slot].state == kEnterSubmitted);
    CHECK(reg.ConditionRow(81) == 0x0a);

    reg.TickStickAbort(*rec, slot, 900, /*isLocalPlayer=*/true, /*anyWalkAxisNonZero=*/true);

    CHECK(begunCalled && !begunOk);  // Sec10.9 step3: ok=false
    CHECK(reg.ConditionRow(81) == 0);  // Sec10.9 step9: undoes row 0xa
    auto [rec2, slot2] = reg.FindSlotByOccupant(81);
    CHECK(rec2 == nullptr && slot2 == -1);
}

// Sec10.0 item8: "never for instant requests".
void testStickAbortNeverFiresForInstantRequest() {
    VehicleInteractionRegistry reg;
    EnterRequestRecord req;
    req.requestedSeat = 0;
    req.instant = true;
    auto [rec, slot] = reg.SubmitEnterRequest(8100, 82, req);
    reg.TickStickAbort(*rec, slot, 900, true, true);
    CHECK(rec->slots[slot].state == kEnterSubmitted);  // unchanged - no cancel
}

// ===========================================================================
// U6.5 (Sec14) - the eight animated enter states' own interior bodies.
// ===========================================================================

// Sec14.0 item3/Sec14.3: 0x0b's on-entry miss is the one exception in the
// unit whose miss is an explicit transition (-> the commit) rather than a
// silent no-op.
void testEnter0bMissCommitsDirectly() {
    VehicleInteractionRegistry reg;
    auto* rec = reg.ClaimRecord(20000);
    int slot = reg.AllocateSlot(*rec, 200);
    rec->slots[slot].enterRequest.requestedSeat = 0;
    rec->slots[slot].external.animationLookupHit = false;  // miss
    reg.SetEnter(*rec, slot, kEnterTraverse, false);
    CHECK(rec->slots[slot].state == kRideActive);  // the commit cascades into ride (Sec8.2 step29)
    CHECK(reg.ConditionRow(200) == 8);
}

// Sec10.4.4/Sec14.7: 0x0f ("already fully specified by U6.3") - a miss
// with no animation override also commits directly ("without e, set-enter
// 9 directly" - the only other fallback of this kind besides 0x0b's own).
void testEnter0fMissCommitsDirectly() {
    VehicleInteractionRegistry reg;
    auto* rec = reg.ClaimRecord(20100);
    int slot = reg.AllocateSlot(*rec, 201);
    rec->slots[slot].enterRequest.requestedSeat = 0;
    rec->slots[slot].external.animationLookupHit = false;
    reg.SetEnter(*rec, slot, kEnterForcedInPlace, false);
    CHECK(rec->slots[slot].state == kRideActive);
}

// Sec14.9: 0x11's per-frame waits for the animation, then commits
// unconditionally - no chooser, no fallback.
void testEnter11ChangeSeatCommitsOnceAnimationFinished() {
    VehicleInteractionRegistry reg;
    auto* rec = reg.ClaimRecord(20200);
    int slot = reg.AllocateSlot(*rec, 202);
    rec->slots[slot].phase = VehicleSlotPhase::kEnter;  // Tick() dispatches by phase (Sec7.6 step5)
    rec->slots[slot].enterRequest.requestedSeat = 2;
    reg.SetEnter(*rec, slot, kEnterChangeSeat, false);
    CHECK(rec->slots[slot].state == kEnterChangeSeat);

    reg.Tick(0);  // animationFinished defaults false - still waiting
    CHECK(rec->slots[slot].state == kEnterChangeSeat);

    rec->slots[slot].external.animationFinished = true;
    reg.Tick(0);
    CHECK(rec->slots[slot].state == kRideActive);
    CHECK(reg.ConditionRow(202) == 9);  // seat 2, not seat 0 (Sec8.1)
}

// Sec7.5/Sec9.5's next-state chooser 0x00afb220: only two of its seven
// CONFIRMED outcomes have a known mechanism (OPEN item109) - reaching it
// without an override throws rather than guessing the rest of the cascade.
void testNextStateChooserThrowsWithoutOverride() {
    VehicleInteractionRegistry reg;
    auto* rec = reg.ClaimRecord(20300);
    int slot = reg.AllocateSlot(*rec, 203);
    rec->slots[slot].phase = VehicleSlotPhase::kEnter;
    rec->slots[slot].enterRequest.requestedSeat = 0;
    reg.SetEnter(*rec, slot, kEnterTraverse, false);  // hit (default) - stays 0x0b
    CHECK(rec->slots[slot].state == kEnterTraverse);
    rec->slots[slot].external.animationFinished = true;

    bool threw = false;
    try {
        reg.Tick(0);  // waits for 0x00b0c170, then calls the chooser
    } catch (const sr3luahost::OpenStateError&) {
        threw = true;
    }
    CHECK(threw);
}

// A caller/test CAN supply the chooser's full result using the two
// CONFIRMED probes directly instead of guessing the rest (Sec9.5's own
// citation of 0x00afb220, row (a)).
void testNextStateChooserOverrideUsingProbes() {
    VehicleInteractionRegistry reg;
    reg.nextStateChooserOverride = [&](VehicleInteractionRecord& r, int i, int32_t) {
        return reg.ProbeOpenVsTraverse(r, i);
    };
    auto* rec = reg.ClaimRecord(20400);
    int slot = reg.AllocateSlot(*rec, 204);
    rec->slots[slot].phase = VehicleSlotPhase::kEnter;
    rec->slots[slot].enterRequest.requestedSeat = 0;
    reg.SetEnter(*rec, slot, kEnterPrepare, false);
    CHECK(rec->slots[slot].state == kEnterPrepare);
    rec->slots[slot].external.animationFinished = true;

    reg.Tick(0);  // sub-timer stage 0 -> 1
    CHECK(rec->slots[slot].state == kEnterPrepare);
    reg.Tick(0);  // stage 1: small tilt (default) -> the chooser -> probe (a)
    CHECK(rec->slots[slot].state == kEnterOpen);  // animationLookupHit defaults true -> probe matches
}

// Sec14.5 step2/Sec10.4.2 H1: 0x0d's own on-entry independently
// re-implements the hub's tank-entry logic - a player-class victim
// cancels the entry outright instead of being evicted.
void testEnter0dTankPlayerVictimCancelsOutright() {
    VehicleInteractionRegistry reg;
    auto* rec = reg.ClaimRecord(20500);
    rec->isTankVehicle = true;
    int driverSlot = reg.AllocateSlot(*rec, 205);
    rec->slots[driverSlot].seatIndex = 0;
    rec->seats[0].seatedOccupant = 205;

    int entrantSlot = reg.AllocateSlot(*rec, 206);
    rec->slots[entrantSlot].enterRequest.requestedSeat = 0;
    rec->slots[entrantSlot].enterRequest.evictedVictimIsPlayerClass = true;
    reg.SetEnter(*rec, entrantSlot, kEnterExtract, false);

    auto [rec2, slot2] = reg.FindSlotByOccupant(206);
    CHECK(rec2 == nullptr && slot2 == -1);     // cancelled, not seated
    CHECK(rec->seats[0].seatedOccupant == 205);  // the driver was never evicted
}

// Sec14.5 step3/U6.1 Sec9.5: a non-tank 0x0d delegates to the hijack
// chooser, which writes the hijacker's own kind and sends the victim an
// animated exit request (kind+9).
void testEnter0dHijackChooserEvictsVictim() {
    VehicleInteractionRegistry reg;
    auto* rec = reg.ClaimRecord(20600);
    int driverSlot = reg.AllocateSlot(*rec, 207);
    rec->slots[driverSlot].seatIndex = 0;
    rec->seats[0].seatedOccupant = 207;

    int entrantSlot = reg.AllocateSlot(*rec, 208);
    rec->slots[entrantSlot].enterRequest.requestedSeat = 0;
    rec->slots[entrantSlot].external.hijackChosenKind = 4;  // "extract kill"
    reg.SetEnter(*rec, entrantSlot, kEnterExtract, false);

    CHECK(rec->slots[entrantSlot].variant10 == 4);
    auto [rec2, slot2] = reg.FindSlotByOccupant(207);
    CHECK(rec2 == rec && slot2 == driverSlot);
    CHECK(rec2->slots[slot2].exitRequest.exitKind == 0x0d);  // 4+9
    CHECK(rec2->slots[slot2].state == kExitRequested);        // RequestExit only submits 0x14
}

// Sec14.10: with the entry seat free, 0x13's on-entry starts the fast-
// entry animation directly; once finished, with the seat and queue both
// clear, the per-frame commits inline (one of state 9's own CONFIRMED
// producers, U6.0 Sec7.5).
void testEnter13NoOccupantStartsFastEntryThenCommits() {
    VehicleInteractionRegistry reg;
    auto* rec = reg.ClaimRecord(20700);
    int slot = reg.AllocateSlot(*rec, 209);
    rec->slots[slot].phase = VehicleSlotPhase::kEnter;
    rec->slots[slot].enterRequest.requestedSeat = 3;
    reg.SetEnter(*rec, slot, kEnterFast, false);
    CHECK(rec->slots[slot].enterRequest.fastEntryLatched);
    CHECK(rec->slots[slot].state == kEnterFast);

    rec->slots[slot].external.animationFinished = true;
    reg.Tick(0);
    CHECK(rec->slots[slot].state == kRideActive);
    CHECK(reg.ConditionRow(209) == 9);  // seat 3, not seat 0
}

// Sec14.10 step1: the +0x28 timeout-based hard cancel - with the entry
// seat still occupied, the shared inline-cancel idiom fires (full cancel,
// no resubmit).
void testEnter13TimeoutCancelsWhenOccupantPresent() {
    VehicleInteractionRegistry reg;
    auto* rec = reg.ClaimRecord(20800);
    int driverSlot = reg.AllocateSlot(*rec, 210);
    rec->slots[driverSlot].seatIndex = 0;
    rec->seats[0].seatedOccupant = 210;

    int entrantSlot = reg.AllocateSlot(*rec, 211);
    rec->slots[entrantSlot].phase = VehicleSlotPhase::kEnter;
    rec->slots[entrantSlot].enterRequest.requestedSeat = 0;
    reg.SetEnter(*rec, entrantSlot, kEnterFast, false);
    rec->slots[entrantSlot].enterTimerMs = 50;

    reg.Tick(100);  // 50 -> expired; entry seat still occupied by 210
    auto [rec2, slot2] = reg.FindSlotByOccupant(211);
    CHECK(rec2 == nullptr && slot2 == -1);
}

// ===========================================================================
// U6.7 (Sec11) - the four animated exit states' own interior bodies.
// ===========================================================================

// U6.6 Sec6.7.2 step4's exit chooser 0x00b02150, reused unchanged by 0x1b's
// per-frame (Sec11.3.2 step4).
void testExit1bRoutesToExtractOrExit() {
    VehicleInteractionRegistry reg;
    auto seated = reg.CommitSeating(21000, 212, 0);
    VehicleSlot& slot = seated.record->slots[seated.slotIndex];
    slot.phase = VehicleSlotPhase::kExit;
    reg.SetExit(*seated.record, seated.slotIndex, kExitChangeSeat, false, true);
    slot.external.animationFinished = true;
    reg.Tick(0);
    CHECK(slot.state == kExitExit);  // beingExtracted defaults false

    auto seated2 = reg.CommitSeating(21001, 213, 0);
    VehicleSlot& slot2 = seated2.record->slots[seated2.slotIndex];
    slot2.phase = VehicleSlotPhase::kExit;
    slot2.exitRequest.beingExtracted = true;
    reg.SetExit(*seated2.record, seated2.slotIndex, kExitChangeSeat, false, true);
    slot2.external.animationFinished = true;
    reg.Tick(0);
    CHECK(slot2.state == kExitExtract);
}

// Sec11.4.1 step4/U6.1 Sec9.4.3: with no hijacker found, 0x1d's lookup
// always misses real data - the instant fallback (0x17), inline all the
// way to release within this one SetExit call.
void testExit1dMissFallsBackToInstant() {
    VehicleInteractionRegistry reg;
    auto seated = reg.CommitSeating(21100, 214, 0);
    VehicleSlot& slot = seated.record->slots[seated.slotIndex];
    slot.phase = VehicleSlotPhase::kExit;
    slot.external.animationLookupHit = false;
    reg.SetExit(*seated.record, seated.slotIndex, kExitExtract, false, true);

    auto [rec2, slot2] = reg.FindSlotByOccupant(214);
    CHECK(rec2 == nullptr && slot2 == -1);  // 0x1d -> 0x17 -> 0x19 -> released, all inline
}

// Sec11.4.2: a kind == kill (0xd on the exit-side list) extract finalises
// once the animation finishes - the one CONFIRMED per-frame transition.
void testExit1dKillKindFinalisesOnAnimationFinished() {
    VehicleInteractionRegistry reg;
    auto seated = reg.CommitSeating(21200, 215, 0);
    VehicleSlot& slot = seated.record->slots[seated.slotIndex];
    slot.phase = VehicleSlotPhase::kExit;
    slot.exitRequest.exitKind = 0x0d;  // "kill"
    reg.SetExit(*seated.record, seated.slotIndex, kExitExtract, false, true);
    CHECK(slot.variant10 == 0x0d);
    CHECK(slot.state == kExitExtract);  // hit (default) - waits

    slot.external.animationFinished = true;
    reg.Tick(0);
    auto [rec2, slot2] = reg.FindSlotByOccupant(215);
    CHECK(rec2 == nullptr && slot2 == -1);
}

// Sec11.4.2's own new OPEN item: "how an instantiated, non-kill extract
// eventually reaches 0x19 is OPEN" - this slice does not invent a
// transition for it; it must stay in 0x1d even once the animation
// finishes (no-invented-fixes policy, not a bug).
void testExit1dNonKillExtractStaysOpenPerSpec() {
    VehicleInteractionRegistry reg;
    auto seated = reg.CommitSeating(21300, 216, 0);
    VehicleSlot& slot = seated.record->slots[seated.slotIndex];
    slot.phase = VehicleSlotPhase::kExit;
    slot.exitRequest.exitKind = 0x0a;  // "extract standard" - NOT kill
    reg.SetExit(*seated.record, seated.slotIndex, kExitExtract, false, true);
    slot.external.animationFinished = true;
    reg.Tick(0);
    CHECK(slot.state == kExitExtract);  // still 0x1d - no invented transition
}

// Sec11.5.4 step4: "eject" is not an animated state in the usual sense -
// it fast-forwards to the finaliser in the same frame it is chosen, once
// there is no clip to play.
void testExit1fEjectFastForwardsToFinaliser() {
    VehicleInteractionRegistry reg;
    auto seated = reg.CommitSeating(21400, 217, 0);
    VehicleSlot& slot = seated.record->slots[seated.slotIndex];
    slot.phase = VehicleSlotPhase::kExit;
    slot.exitRequest.forceEject = true;
    slot.external.animationLookupHit = false;  // no clip to play
    reg.SetExit(*seated.record, seated.slotIndex, kExitExit, false, true);
    CHECK(slot.variant10 == kExitKindEject);

    reg.Tick(0);
    auto [rec2, slot2] = reg.FindSlotByOccupant(217);
    CHECK(rec2 == nullptr && slot2 == -1);
}

// Sec11.5.2's cascade: the tip-over+speed predicate picks "exit at
// altitude" ahead of the implicit default.
void testExit1fCascadePicksAltitudeKind() {
    VehicleInteractionRegistry reg;
    auto seated = reg.CommitSeating(21500, 218, 0);
    VehicleSlot& slot = seated.record->slots[seated.slotIndex];
    slot.phase = VehicleSlotPhase::kExit;
    slot.external.exitTippedAndAtAltitude = true;
    reg.SetExit(*seated.record, seated.slotIndex, kExitExit, false, true);
    CHECK(slot.variant10 == kExitKindAtAltitude);
    CHECK(slot.interactionPointType == 0x0f);
}

// Sec11.5.2 step1: the forced-kinds branch picks "exit guardian angel"
// and resets the side token.
void testExit1fForcedGuardianAngel() {
    VehicleInteractionRegistry reg;
    auto seated = reg.CommitSeating(21600, 219, 0);
    VehicleSlot& slot = seated.record->slots[seated.slotIndex];
    slot.phase = VehicleSlotPhase::kExit;
    slot.exitRequest.guardianAngelForced = true;
    reg.SetExit(*seated.record, seated.slotIndex, kExitExit, false, true);
    CHECK(slot.variant10 == kExitKindGuardianAngel);
    CHECK(slot.sideIndex == -1);
}

// Sec11.5.1 item6/Sec11.5.3: 0x1f's climb sub-timer (+0x18 6->7) has no
// numerically-given window duration anywhere in the read spec text - this
// throws unless a caller supplies the one signal the spec itself never
// resolves (the OpenValue/OpenStateError shape, see the file banner).
void testExit1fClimbSubTimerThrowsWithoutOverride() {
    VehicleInteractionRegistry reg;
    auto seated = reg.CommitSeating(21900, 223, 0);
    VehicleSlot& slot = seated.record->slots[seated.slotIndex];
    slot.phase = VehicleSlotPhase::kExit;
    reg.SetExit(*seated.record, seated.slotIndex, kExitExit, false, true);
    // Force the climb kind directly - no producer for it exists anywhere
    // in this unit's own trace either (kExitKindClimb's own doc comment,
    // Sec11.5.2's OPEN item) - and simulate "already started" (frame 1
    // already ran).
    slot.variant10 = kExitKindClimb;
    slot.exitRequest.exitStarted = true;

    bool threw = false;
    try {
        reg.Tick(0);
    } catch (const sr3luahost::OpenStateError&) {
        threw = true;
    }
    CHECK(threw);

    slot.external.halfWindowElapsed = true;
    reg.Tick(0);
    CHECK(slot.variant18 == 7);  // advances once the override says "yes"
}

// Sec11.0 item8/Sec11.6 step1: 0x21's own on-entry always runs 0x1f's
// entire on-entry first - the common case (a kind resolves) leaves the
// slot in 0x1f, not 0x21; only the documented, U6.0-census-confirmed-live
// "cannot resolve" path actually re-stamps state 0x21.
void testExit21AlwaysRunsExit1fOnEntryFirst() {
    VehicleInteractionRegistry reg;
    auto seated = reg.CommitSeating(21700, 220, 0);
    VehicleSlot& slot = seated.record->slots[seated.slotIndex];
    slot.phase = VehicleSlotPhase::kExit;
    reg.SetExit(*seated.record, seated.slotIndex, kExitTraverse, false, true);
    CHECK(slot.state == kExitExit);  // NOT 0x21

    auto seated2 = reg.CommitSeating(21701, 221, 0);
    VehicleSlot& slot2 = seated2.record->slots[seated2.slotIndex];
    slot2.phase = VehicleSlotPhase::kExit;
    slot2.external.exitKindChooserCannotResolve = true;
    reg.SetExit(*seated2.record, seated2.slotIndex, kExitTraverse, false, true);
    CHECK(slot2.state == kExitTraverse);
}

// Sec11.6 per-frame: once the traverse clip's progress completes, it
// inlines to the finaliser.
void testExit21FinalisesOnFullProgress() {
    VehicleInteractionRegistry reg;
    auto seated = reg.CommitSeating(21800, 222, 0);
    VehicleSlot& slot = seated.record->slots[seated.slotIndex];
    slot.phase = VehicleSlotPhase::kExit;
    slot.external.exitKindChooserCannotResolve = true;
    reg.SetExit(*seated.record, seated.slotIndex, kExitTraverse, false, true);
    CHECK(slot.state == kExitTraverse);

    slot.external.animationFinished = true;
    reg.Tick(0);
    auto [rec2, slot2] = reg.FindSlotByOccupant(222);
    CHECK(rec2 == nullptr && slot2 == -1);
}

}  // namespace

int main() {
    testStateReachability();
    testStubStatesStillStoreTheirNumber();
    testKeyBuilderShortKeyZeroFill();
    testKeyBuilderExtractVariantOffsetBy9();
    testKeyBuilderChangeSeatAnySurface();
    testKeyBuilderExitConvertibleSubstitution();
    testKeyBuilderExtractFailAnyWildcard();
    testMatcherWildcardAndFirstMatchWins();
    testMatcherShortKeyMiss();
    testFindAnimationSetChoiceByPhase();
    testSeatingCommitConditionRow();
    testInstantExitChainAndRelease();
    testRequestExitRefusesEnteringOrExitingSlot();
    testRequestExitPermissionLock();
    testExit16WaitSeatTimeout();
    testExit18ParkedHasNoTransition();
    testInstantRouteFreeSeatCommitsSynchronously();
    testInstantRouteOccupiedSeatEvictsThenSeats();
    testHubRoutingNonInstantBranches();
    testTankEntryWithPlayerVictimCancelsInsteadOfEviction();
    testRequestKindRemoveThenStopAlwaysCancels();
    testStickAbortCancelsAndFiresBegunFalse();
    testStickAbortNeverFiresForInstantRequest();

    testEnter0bMissCommitsDirectly();
    testEnter0fMissCommitsDirectly();
    testEnter11ChangeSeatCommitsOnceAnimationFinished();
    testNextStateChooserThrowsWithoutOverride();
    testNextStateChooserOverrideUsingProbes();
    testEnter0dTankPlayerVictimCancelsOutright();
    testEnter0dHijackChooserEvictsVictim();
    testEnter13NoOccupantStartsFastEntryThenCommits();
    testEnter13TimeoutCancelsWhenOccupantPresent();

    testExit1bRoutesToExtractOrExit();
    testExit1dMissFallsBackToInstant();
    testExit1dKillKindFinalisesOnAnimationFinished();
    testExit1dNonKillExtractStaysOpenPerSpec();
    testExit1fEjectFastForwardsToFinaliser();
    testExit1fCascadePicksAltitudeKind();
    testExit1fForcedGuardianAngel();
    testExit1fClimbSubTimerThrowsWithoutOverride();
    testExit21AlwaysRunsExit1fOnEntryFirst();
    testExit21FinalisesOnFullProgress();

    if (g_failures == 0) {
        std::cout << "All vehicle-occupancy synthetic checks passed.\n";
        return 0;
    }
    std::cerr << g_failures << " check(s) failed.\n";
    return 1;
}
