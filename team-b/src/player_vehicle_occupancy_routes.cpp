// player_vehicle_occupancy_routes.cpp
// The enter route (U6.3, Sec10) and its sub-states, the stick abort, the hijack
// chooser, and the exit-kind sub-states of include/sr3player/vehicle_occupancy.h's
// VehicleInteractionRegistry, moved verbatim out of player_vehicle_occupancy.cpp to keep
// each file under ~1,500 lines (Team C refactor, 2026-10-17). See that file and the
// header's own banner for scope, citations and judgement calls. Section references
// (SecN.M) are to spec-player-runtime.md.

#include "sr3player/vehicle_occupancy.h"

#include <algorithm>
#include <cctype>

#include "sr3luahost/open_state.h"

namespace sr3player {

// --- U6.3: the enter route (Sec10) ---

VehicleInteractionRegistry::EnterRouteResult VehicleInteractionRegistry::RequestEnterVehicle(
    uint64_t vehicleHandle, uint64_t occupantHandle, EnterRequestRecord request, bool vehicleInstantiated,
    bool hasEnterAnimationsForSeat) {
    // Sec10.3 step11's force-instant pre-rules, applied before submission
    // (the real hub applies them itself, but the effect - the slot reaches
    // the hub already instant - is identical).
    if (!request.instant) {
        if (!hasEnterAnimationsForSeat) request.instant = true;  // step11(a)
        if (!vehicleInstantiated) request.instant = true;         // step11(b)
    }

    EnterRouteResult result;
    VehicleInteractionRecord* rec = ClaimRecord(vehicleHandle);
    if (!rec) return result;
    const int slot = AllocateSlot(*rec, occupantHandle);
    if (slot < 0) return {rec, -1};
    rec->slots[slot].enterRequest = request;

    SetEnter(*rec, slot, kEnterSubmitted, true);  // state 0 (Sec10.2)
    // The real 250ms seat-resolution upkeep's "-> 1" hand-off (Sec7.6) is
    // folded into this same call (see this method's own doc comment) -
    // state 1 runs, and its own routing decides where this chain goes next
    // (synchronously all the way to the commit for a free-seat instant
    // request, Sec10.4.2's own "synchronous call chain" framing).
    SetEnter(*rec, slot, kEnterHub, true);  // state 1 (Sec10.3)

    result.record = rec;
    result.slotIndex = slot;
    return result;
}

void VehicleInteractionRegistry::TickStickAbort(VehicleInteractionRecord& rec, int slotIndex, int32_t dtMs,
                                                  bool isLocalPlayer, bool anyWalkAxisNonZero) {
    // Sec10.2 per-frame/Sec10.0 item8: local-player only, never for an
    // instant request, after the 800ms grace. Only reproduced for state 0
    // (state 5's own copy, Sec10.6, needs the real navigator this slice
    // does not model).
    if (slotIndex < 0 || slotIndex >= kSlotsPerRecord) return;
    VehicleSlot& slot = rec.slots[slotIndex];
    if (slot.state != kEnterSubmitted || slot.stickAbortGraceMs < 0) return;
    if (slot.stickAbortGraceMs > 0) slot.stickAbortGraceMs -= dtMs;
    if (slot.stickAbortGraceMs > 0) return;  // grace not yet expired
    if (!isLocalPlayer || slot.enterRequest.instant || !anyWalkAxisNonZero) return;
    SetEnter(rec, slotIndex, kEnterCancel, true);
}

// ===========================================================================
// U6.3 Sec10.4.3 - cancel and resubmit, reused unchanged by U6.5/U6.7.
// ===========================================================================

VehicleInteractionRegistry::EnterRouteResult VehicleInteractionRegistry::CancelAndResubmit(
    VehicleInteractionRecord& rec, int i) {
    // 0x00afe980 (Sec10.4.3, 136 insns, CONFIRMED): build a fresh request
    // (seat/kind/forced kept; instant forced on; fast cleared; callbacks
    // lost), cancel the old slot, then resubmit. Judgement call (see this
    // method's own header doc comment): folds the resubmission all the
    // way through the hub, exactly as RequestEnterVehicle already
    // simplifies the 250ms upkeep hand-off for the identical reason.
    VehicleSlot& slot = rec.slots[i];
    EnterRequestRecord fresh;
    fresh.requestedSeat = slot.enterRequest.requestedSeat;  // +0x00
    fresh.requestKind = slot.enterRequest.requestKind;      // +0x08
    fresh.forced = slot.enterRequest.forced;                // kept (same +0x20 dword)
    fresh.instant = true;                                    // bit0 set
    fresh.fastEntry = false;                                 // +0x23 bit0 cleared
    const uint64_t occupant = slot.occupantHandle;
    const uint64_t vehicleHandle = rec.vehicleHandle;

    SetEnter(rec, i, kEnterCancel, true);  // releases the old slot (Sec10.9)
    return RequestEnterVehicle(vehicleHandle, occupant, fresh);
}

// ===========================================================================
// U6.5 (Sec14) - the eight animated enter states' own bodies.
// ===========================================================================

void VehicleInteractionRegistry::OnEnterPrepare(VehicleInteractionRecord& rec, int i) {
    // 0x00af9720 (Sec14.2 on-entry, 57 insns, CONFIRMED).
    VehicleSlot& slot = rec.slots[i];
    slot.state = kEnterPrepare;
    RecordTrace(kEnterPrepare);
    slot.variant10 = slot.variant14 = -1;
    slot.variant18 = 0;  // exception (Sec14.0 item2): sub-timer stage 0 (-> 1 below)

    // The element lookup (U6.1's rule): hit plays, miss is silent
    // (Sec14.0 item3 - no fallback; 0x00b0c170 reads "nothing playing" as
    // finished on the very next per-frame tick anyway).
    slot.hasActiveAnimation = slot.external.animationLookupHit;
}

void VehicleInteractionRegistry::TickEnterPrepare(VehicleInteractionRecord& rec, int i, int32_t /*dtMs*/) {
    // 0x00affdb0 (Sec14.2 per-frame, 263 insns, CONFIRMED skeleton).
    VehicleSlot& slot = rec.slots[i];
    if (!rec.vehicleInstantiatedNow) {
        // Ownership is always true in this single-player project
        // (Sec6.13 point3's own precedent) - cancel-and-resubmit directly.
        CancelAndResubmit(rec, i);
        return;
    }
    const bool animReady = !slot.hasActiveAnimation || slot.external.animationFinished;
    switch (slot.variant18) {
        case 0:
            if (!animReady) return;
            slot.variant18 = 1;  // a role-only vehicle call (0x00ab48f0) is NOT reproduced
            return;
        case 1: {
            if (!animReady) return;
            if (slot.external.vehicleTiltSevere) {
                // Large tilt: the stick-abort path (same mechanism every
                // other stick-abort check in this unit uses).
                if (slot.external.isLocalPlayer && !slot.enterRequest.instant &&
                    slot.stickAbortGraceMs <= 0 && slot.external.anyWalkAxisNonZero) {
                    SetEnter(rec, i, kEnterCancel, true);
                }
                return;
            }
            // Small tilt: the chooser (clearing the "entry-angle class"
            // bits first - not modelled, no reader within this slice's
            // own scope).
            const int32_t result = NextStateChooser(rec, i, 0);
            if (result == kEnterCancel) {
                CancelAndResubmit(rec, i);
                return;
            }
            if (result == kEnterForcedInPlace) slot.interactionPointType = -1;  // "also clear slot +0x24 first"
            SetEnter(rec, i, result, true);
            return;
        }
        default:
            return;  // wait
    }
}

void VehicleInteractionRegistry::OnEnterTraverse(VehicleInteractionRecord& rec, int i) {
    // 0x00af97e0 (Sec14.3 on-entry, 164 insns, CONFIRMED).
    VehicleSlot& slot = rec.slots[i];
    slot.state = kEnterTraverse;
    RecordTrace(kEnterTraverse);
    slot.variant10 = slot.variant14 = slot.variant18 = -1;  // no exception here

    // step2: +0x24 = "traverse standard"(6) or "traverse no door"(7) from
    // a point-set query against the entry seat's occupant - no real
    // point-set data exists here (same gap U6.3 Sec10.6 step4 already
    // hits), so this defaults to "standard", matching the spec's own
    // "with no occupant, +0x24 = 6 unconditionally."
    slot.interactionPointType = 6;

    // step3's door-state query/drive (sets enter-request +0x21 bits2/3)
    // needs vehicle/door geometry this slice does not model - NOT
    // reproduced.

    slot.hasActiveAnimation = slot.external.animationLookupHit;
    if (!slot.hasActiveAnimation) {
        // The one on-entry miss in this unit that is an explicit
        // transition rather than a silent no-op (Sec14.0 item3).
        SetEnter(rec, i, kEnterCommit, true);
    }
    // hit: play, then the animated-camera gate/forced-snap camera
    // hand-off (step4) - camera system not modelled, NOT reproduced.
}

void VehicleInteractionRegistry::TickEnterTraverse(VehicleInteractionRecord& rec, int i, int32_t /*dtMs*/) {
    // 0x00b000f0 (Sec14.3 per-frame, 184 insns, CONFIRMED).
    VehicleSlot& slot = rec.slots[i];
    if (!rec.vehicleInstantiatedNow) {
        CancelAndResubmit(rec, i);
        return;
    }
    // step2's door-drive block needs vehicle geometry - NOT reproduced.
    if (!slot.external.animationFinished && slot.hasActiveAnimation) return;  // step3: wait for 0x00b0c170

    slot.landingKind = 2;  // +0x1c = 2 ("vehicle" surface token, step3 bullet2)

    // Evict the destination seat's occupant with an ordinary exit
    // request, if not already exiting (step3 bullet3, the pattern
    // Sec10.5 names for states 3/4's per-frame).
    const int32_t seat = slot.enterRequest.requestedSeat;
    if (seat >= 0 && seat < kSlotsPerRecord) {
        const uint64_t occupant = rec.seats[seat].seatedOccupant;
        if (occupant != 0 && occupant != slot.occupantHandle) {
            auto [orec, oslot] = FindSlotByOccupant(occupant);
            const bool alreadyExiting = orec && oslot >= 0 && orec->slots[oslot].phase == VehicleSlotPhase::kExit;
            if (!alreadyExiting) RequestExit(occupant, ExitRequestRecord{});
        }
    }

    const int32_t result = NextStateChooser(rec, i, 0);
    if (result == kEnterCancel) CancelAndResubmit(rec, i);
    else SetEnter(rec, i, result, true);
}

void VehicleInteractionRegistry::OnEnterOpen(VehicleInteractionRecord& rec, int i) {
    // 0x00afa0d0 (Sec14.4 on-entry, 124 insns, CONFIRMED).
    VehicleSlot& slot = rec.slots[i];
    slot.state = kEnterOpen;
    RecordTrace(kEnterOpen);
    slot.variant10 = -1;
    slot.variant18 = 2;  // exception (Sec14.0 item2): sub-timer stage 2 (-> 3 below)
    // +0x14 ("open shady"/"open shopper"/"open standard"): this slice has
    // no dedicated request bit4/5 fields distinct from its own
    // variant14, so it is left at whatever a caller already set (-1 by
    // default, "open standard" per BuildElementKey's own case).
    if (slot.variant14 != 2 && slot.variant14 != 3) slot.variant14 = -1;

    // Hit or miss converge on the same next step (Sec14.4 step2's own
    // "unusual" structure) - the camera gate/hand-off (step3) and the
    // per-seat scratch clear/hijack bookkeeping (step4) are camera/
    // vehicle-band systems this slice does not model, NOT reproduced.
    slot.hasActiveAnimation = slot.external.animationLookupHit;
}

void VehicleInteractionRegistry::TickEnterOpen(VehicleInteractionRecord& rec, int i, int32_t /*dtMs*/) {
    // 0x00b00c60 (Sec14.4 per-frame, 342 insns, CONFIRMED).
    VehicleSlot& slot = rec.slots[i];
    if (!rec.vehicleInstantiatedNow) {
        CancelAndResubmit(rec, i);
        return;
    }
    switch (slot.variant18) {
        case 2:
            // Wait for the tag check (not named, OPEN) or progress
            // beyond ~0.5 (a float constant - numerically given).
            if (slot.external.animationProgress < 0.5f) return;
            slot.variant18 = 3;  // the door drive/1000ms timer-arm (step2) needs vehicle geometry - NOT reproduced beyond the advance itself
            return;
        case 3: {
            // Wait for BOTH the animation finished AND (door fully open
            // [not modelled, never true here] OR the 1000ms timer -
            // folded into animationFinished since neither sub-condition
            // is separately modelable with real data).
            if (!slot.external.animationFinished) return;
            if (slot.external.isLocalPlayer && !slot.enterRequest.instant &&
                slot.stickAbortGraceMs <= 0 && slot.external.anyWalkAxisNonZero) {
                SetEnter(rec, i, kEnterCancel, true);
                return;
            }
            // The chooser's flag here is "not the literal 0" (Sec14.0
            // item8/Sec14.13 item2) - what it selects is OPEN; this
            // slice passes the documented placeholder (0), since
            // NextStateChooser itself already requires an override for
            // its own full cascade regardless of the flag's value.
            const int32_t result = NextStateChooser(rec, i, 0 /* OPEN: real flag not resolved */);
            if (result == kEnterCancel) CancelAndResubmit(rec, i);
            else SetEnter(rec, i, result, true);
            return;
        }
        default:
            return;
    }
}

void VehicleInteractionRegistry::OnEnterExtract(VehicleInteractionRecord& rec, int i) {
    // 0x00afae20 (Sec14.5 on-entry, 129 insns, CONFIRMED).
    VehicleSlot& slot = rec.slots[i];
    slot.state = kEnterExtract;
    RecordTrace(kEnterExtract);
    slot.variant10 = 1;  // exception (Sec14.0 item2): the pre-seeded default extract kind ("extract standard")
    slot.variant14 = slot.variant18 = -1;
    slot.interactionPointType = 0;  // +0x24 = 0 (step1)
    slot.hasActiveAnimation = false;  // "no call to 0x00b0ad10 or 0x00af45a0 anywhere in this function"

    if (rec.isTankVehicle) {
        const int32_t seat = slot.enterRequest.requestedSeat;
        const uint64_t occupant = (seat >= 0 && seat < kSlotsPerRecord) ? rec.seats[seat].seatedOccupant : 0;
        if (occupant != 0) {
            if (slot.enterRequest.evictedVictimIsPlayerClass) {
                // Sec10.4.2 H1 / Sec14.5 step2: a player-class victim
                // cancels the entry outright - returns here, no tail.
                SetEnter(rec, i, kEnterCancel, true);
                return;
            }
            // Non-player: "kill and destroy" in the real game - no
            // damage/AI system exists here. Reuses the SAME simplified
            // stand-in OnEnterHub's own H1 branch already uses for this
            // exact sub-case (ask the occupant to leave instead of being
            // killed) - flagged OPEN there, not invented again here.
            RequestExit(occupant, ExitRequestRecord{});
        }
    } else {
        HijackChooserPickKind(rec, i);  // step3: the hijack chooser, U6.1 Sec9.5
    }

    // step4 (tail): folds an unresolved helper's boolean into human +0xec
    // bit 0x100000 - no reader of that bit exists within this slice's
    // own scope (three OTHER writers, Sec10.2/Sec10.3/Sec10.9) - NOT
    // reproduced.
}

void VehicleInteractionRegistry::TickEnterExtract(VehicleInteractionRecord& rec, int i, int32_t dtMs) {
    // 0x00b00310 (Sec14.5 per-frame, 515 insns, CONFIRMED skeleton).
    VehicleSlot& slot = rec.slots[i];
    if (!rec.vehicleInstantiatedNow) {
        CancelAndResubmit(rec, i);
        return;
    }

    // step2: slot +0x28 expiry (Sec14.0 item9) - no confirmed writer, so
    // this never spuriously expires unless a caller arms it.
    bool expired = false;
    if (slot.enterTimerMs >= 0) {
        if (slot.enterTimerMs > 0) slot.enterTimerMs -= dtMs;
        expired = slot.enterTimerMs <= 0;
    }
    // class-bit 0x22 ("despawn-eligible") on the extract target - no
    // per-occupant class data exists here (same gap this bit hits
    // elsewhere, U6.6 Sec6.7.2/U6.7 Sec11.4.1); defaults to "not
    // eligible", so a would-be stick-abort only sets a FLAG here (fed
    // into the combined check at step4), not an immediate cancel.
    bool stickAbortFlag = false;
    if (expired) {
        stickAbortFlag = slot.external.isLocalPlayer && !slot.enterRequest.instant && slot.external.anyWalkAxisNonZero;
    }

    // step3: wait for the entry seat to actually empty (request +0x21
    // bit 0x80's "force proceed" override is not modelled - this slice
    // always waits for a genuinely empty seat). The held-weapon-drop/
    // AI-driving/door-drive blocks (step3's remaining bullets) are
    // inventory/vehicle-band systems this slice does not model - NOT
    // reproduced.
    const int32_t entrySeat = slot.enterRequest.requestedSeat;
    const uint64_t entryOccupant = (entrySeat >= 0 && entrySeat < kSlotsPerRecord)
                                        ? rec.seats[entrySeat].seatedOccupant : 0;
    if (entryOccupant != 0 && entryOccupant != slot.occupantHandle) return;

    if (!slot.external.animationFinished) return;

    // step4: the combined stick-abort/class-bit cancel, else the chooser.
    if (stickAbortFlag) {
        SetEnter(rec, i, kEnterCancel, true);
        return;
    }
    const int32_t result = NextStateChooser(rec, i, 0);
    if (result == kEnterCancel) CancelAndResubmit(rec, i);
    else SetEnter(rec, i, result, true);
}

void VehicleInteractionRegistry::OnEnterExtractFailPick(VehicleInteractionRecord& rec, int i) {
    // 0x00afaf90 (Sec14.6 on-entry, 145 insns, CONFIRMED).
    VehicleSlot& slot = rec.slots[i];
    slot.state = kEnterExtractFailPick;
    RecordTrace(kEnterExtractFailPick);
    slot.variant10 = slot.variant14 = slot.variant18 = -1;

    // steps2-3: count+roll (no real candidate-count data exists here) -
    // the caller-supplied pick stands in for the roll directly; plays on
    // the real (hijacker's) slot.
    slot.variant14 = slot.external.extractFailPick;
    slot.hasActiveAnimation = slot.external.animationLookupHit;

    // step4: the victim's own "extract fail" animation on a temporary
    // slot - no separate victim-slot animation state is tracked by this
    // registry (the victim's own transition is its per-frame's job, not
    // this on-entry's) - NOT reproduced beyond this slice's own slot.

    // step5: per-seat scratch-table clear (vehicle-band, role only) - NOT
    // reproduced.
}

void VehicleInteractionRegistry::TickEnterExtractFailPick(VehicleInteractionRecord& rec, int i, int32_t /*dtMs*/) {
    // 0x00b00950 (Sec14.6 per-frame, 258 insns, CONFIRMED): "there is no
    // chooser call anywhere in this function" - a failed hijack attempt
    // always ends in a cancel, never a different animated state. A
    // hijacker that fails to resolve (this slice's own lookup missed,
    // hasActiveAnimation false) or whose animation is no longer active
    // (externally reported finished) triggers it.
    VehicleSlot& slot = rec.slots[i];
    if (!slot.hasActiveAnimation || slot.external.animationFinished) {
        SetEnter(rec, i, kEnterCancel, true);
        // step3: re-run the hub's own vehicle-AI-reaction logic (Sec10.3
        // step16) - vehicle-AI system this slice does not model, NOT
        // reproduced.
    }
}

void VehicleInteractionRegistry::OnEnterForcedInPlace(VehicleInteractionRecord& rec, int i) {
    // 0x00afd0a0 (Sec10.4.4, 167 insns, cited verbatim by Sec14.7 - NOT
    // re-derived).
    VehicleSlot& slot = rec.slots[i];
    slot.state = kEnterForcedInPlace;
    RecordTrace(kEnterForcedInPlace);
    slot.variant14 = -1;
    slot.variant18 = 4;
    slot.variant10 = slot.enterRequest.forced ? 0 : -1;  // "enter guardian angel" when forced

    // "the entry door is driven open and the seat's own +0x20 timer
    // disarmed" - vehicle/door geometry this slice does not model, NOT
    // reproduced.

    // e = request +0x30 (the animation override), or the element lookup
    // if -1.
    const bool hasElement = slot.enterRequest.hasAnimationOverride || slot.external.animationLookupHit;
    if (hasElement) {
        // "with e and a controller, play it and, unless the animated-
        // camera gate, the camera hand-off" - camera system not
        // modelled, NOT reproduced beyond the play itself.
        slot.hasActiveAnimation = true;
    } else {
        // "without e, set-enter 9 directly" - the only other fallback of
        // this kind in the 0x0a-0x13 range besides 0x0b's own miss.
        slot.hasActiveAnimation = false;
        SetEnter(rec, i, kEnterCommit, true);
    }
}

void VehicleInteractionRegistry::TickEnterForcedInPlace(VehicleInteractionRecord& rec, int i, int32_t /*dtMs*/) {
    // 0x00b01080 (Sec10.4.4/Sec14.7): "on animation end -> 9 when the
    // entry seat is the destination; -> 0x11 when the destination is
    // free; -> 3 otherwise; 0x00afe980 when the vehicle is gone."
    VehicleSlot& slot = rec.slots[i];
    if (!rec.vehicleInstantiatedNow) {
        CancelAndResubmit(rec, i);
        return;
    }
    if (!slot.external.animationFinished && slot.hasActiveAnimation) return;

    // This registry does not separately track a "destination seat"
    // distinct from the request's own entry seat (slot +0x2c, Sec8.2
    // step3, is only EVER written AT the commit, never before it), so
    // every forced entry this slice can construct takes the "entry seat
    // IS the destination" branch. A future caller that models a
    // genuinely different destination seat should revisit this
    // (documented simplification, not invented).
    SetEnter(rec, i, kEnterCommit, true);
}

void VehicleInteractionRegistry::OnEnterChangeSeat(VehicleInteractionRecord& rec, int i) {
    // 0x00af99b0 (Sec14.9 on-entry, 68 insns, CONFIRMED) - the shortest
    // handler in the unit.
    VehicleSlot& slot = rec.slots[i];
    slot.state = kEnterChangeSeat;
    RecordTrace(kEnterChangeSeat);
    slot.variant10 = slot.variant14 = slot.variant18 = -1;

    // step2: leave the old entry-point queue without releasing the claim
    // (0x00b0b270(vehicle,human,0)) - no real distance-ordered queue
    // exists here (Sec10.1's own defect, already not reproduced
    // elsewhere) - NOT reproduced.

    slot.hasActiveAnimation = slot.external.animationLookupHit;  // step3: hit plays; miss is silent (same convention as 0x0a/0x0c)
}

void VehicleInteractionRegistry::TickEnterChangeSeat(VehicleInteractionRecord& rec, int i, int32_t /*dtMs*/) {
    // 0x00b013c0 (Sec14.9 per-frame, 63 insns, CONFIRMED) - the simplest
    // transition logic in the unit.
    VehicleSlot& slot = rec.slots[i];
    if (!rec.vehicleInstantiatedNow) {
        CancelAndResubmit(rec, i);
        return;
    }
    if (!slot.external.animationFinished && slot.hasActiveAnimation) return;
    SetEnter(rec, i, kEnterCommit, true);  // unconditional, no chooser, no fallback (step2)
}

void VehicleInteractionRegistry::OnEnterFast(VehicleInteractionRecord& rec, int i) {
    // 0x00afd290 (Sec14.10 on-entry, 184 insns, CONFIRMED).
    VehicleSlot& slot = rec.slots[i];
    slot.state = kEnterFast;
    RecordTrace(kEnterFast);
    slot.variant10 = slot.variant14 = -1;
    slot.variant18 = 4;  // exception; consumer not found (Sec14.0 item1/OPEN item111)
    slot.hasActiveAnimation = false;  // 0x13 drives its clip through StartFastEntry below, not a direct lookup here

    // step2: evict a blocking occupant of a DIFFERENT (secondary-access)
    // seat, if eligible - this slice has no Seat_Info Primary/Secondary
    // Access data (role-only, "not independently confirmed" field), so
    // this is NOT reproduced; the entry proceeds as if no such blocking
    // occupant needed evicting here (the entry SEAT's own occupant,
    // below, is a different, CONFIRMED check).

    // step3: resolve the entry seat's own occupant.
    const int32_t seat = slot.enterRequest.requestedSeat;
    const uint64_t occupant = (seat >= 0 && seat < kSlotsPerRecord) ? rec.seats[seat].seatedOccupant : 0;
    if (occupant != 0) {
        HijackChooserPickKind(rec, i);
        slot.enterRequest.hijackedViaChooser = true;  // new bit (Sec14.11) - 0x0d's own on-entry does NOT set it
    } else {
        StartFastEntry(rec, i);
    }

    // step4: UI/achievement/carjacking-report calls - player-only HUD/
    // stat systems this slice does not model, NOT reproduced.
}

void VehicleInteractionRegistry::TickEnterFast(VehicleInteractionRecord& rec, int i, int32_t dtMs) {
    // 0x00b01630 (Sec14.10 per-frame, >=733 insns directly read, CONFIRMED
    // dominant control flow - the "surface kind" door-opening tail,
    // Sec14.13 item1, is an OPEN item in the spec itself and is not
    // reproduced here either).
    VehicleSlot& slot = rec.slots[i];

    // step1: the +0x28 timeout-based hard cancel ("locally owned" stands
    // in as always-true in this single-player project, Sec6.13 point3).
    bool expired = false;
    if (slot.enterTimerMs >= 0) {
        if (slot.enterTimerMs > 0) slot.enterTimerMs -= dtMs;
        expired = slot.enterTimerMs <= 0;
    }
    if (expired) {
        const int32_t entrySeat = slot.enterRequest.requestedSeat;
        const uint64_t entryOccupant = (entrySeat >= 0 && entrySeat < kSlotsPerRecord)
                                            ? rec.seats[entrySeat].seatedOccupant : 0;
        if (entryOccupant == 0) {
            CancelAndResubmit(rec, i);
        } else {
            SetEnter(rec, i, kEnterCancel, true);
        }
        return;
    }

    // step2: resolve the destination seat's occupant.
    const int32_t seat = slot.enterRequest.requestedSeat;
    const uint64_t destOccupant = (seat >= 0 && seat < kSlotsPerRecord) ? rec.seats[seat].seatedOccupant : 0;
    if (destOccupant == 0 && !slot.enterRequest.fastEntryLatched) {
        StartFastEntry(rec, i);
    }

    // step3: never latched (e.g. StartFastEntry hit a lookup miss and
    // cancel-resubmitted instead) -> nothing more this frame.
    if (!slot.enterRequest.fastEntryLatched) return;

    // step4: wait for the held animation.
    if (!slot.external.animationFinished) return;

    if (destOccupant != 0) {
        // Knock-down/impulse retry on the blocking occupant - physics not
        // modelled, NOT reproduced beyond staying put to retry.
        return;
    }
    if (!slot.external.entryPointQueueClearNow) {
        // Same knock-down/impulse pair on the queue head - NOT
        // reproduced.
        return;
    }

    // Both clear: commit directly - "0x13's per-frame never calls
    // set-enter at all" and inlines a copy of state 9's own on-entry
    // (Sec14.10 step4 bullet3); this project's own abstraction still
    // routes it through the SAME setter as every other transition, for
    // the SAME observable effect (U6.0 Sec7.5 already lists this exact
    // call shape as one of state 9's nine CONFIRMED producers).
    SetEnter(rec, i, kEnterCommit, true);
}

// ===========================================================================
// U6.1 Sec9.5's hijack chooser (0x00afa540), reused unchanged by U6.5.
// ===========================================================================

int32_t VehicleInteractionRegistry::HijackChooserPickKind(VehicleInteractionRecord& rec, int i) {
    VehicleSlot& slot = rec.slots[i];
    // "+0x21 bit6 picks kind 6 without a lookup"; otherwise the
    // caller-supplied stand-in for "whichever of the candidates has a
    // matching Element and wins the random pick" (Sec9.5) - no real
    // vi_enter.xtbl data exists here.
    const int32_t kind = slot.enterRequest.hijackForceKind6 ? 6 : slot.external.hijackChosenKind;
    slot.variant10 = kind;  // +0x10 ("written by 0x00afa540", Sec9.3's field table)

    const int32_t seat = slot.enterRequest.requestedSeat;
    const uint64_t victim = (seat >= 0 && seat < kSlotsPerRecord) ? rec.seats[seat].seatedOccupant : 0;
    if (victim != 0) {
        // "sends the victim an exit request with +0x20 = kind+9 (or
        // 0x13 = extract fast when request +0x23 bit0 is set) and
        // +0x24 = the side" - the real candidate-dropping/random-pick
        // logic against the victim's own exit set is not reproduced (no
        // such table is wired into this registry).
        ExitRequestRecord victimReq;
        victimReq.exitKind = slot.enterRequest.fastEntry ? 0x13 : (kind + 9);
        victimReq.approachSide = slot.sideIndex;
        victimReq.beingExtracted = true;  // routes the victim through the animated hijack path (0x1d), not a plain exit
        RequestExit(victim, victimReq, /*hasVehicleInteractionInfo=*/true);
    }
    return kind;
}

// ===========================================================================
// U6.7 Sec11.7.2's fast-entry starter (0x00b01460), cited unchanged by
// U6.5 and called by U6.7's own 0x1d on-entry on the HIJACKER's slot.
// ===========================================================================

void VehicleInteractionRegistry::StartFastEntry(VehicleInteractionRecord& rec, int i) {
    VehicleSlot& slot = rec.slots[i];
    if (slot.enterRequest.fastEntryLatched) return;  // step2: already latched

    // step3: outputs discarded - NOT reproduced (nothing to model).
    if (!slot.external.animationLookupHit) {
        // step4 miss (or a subsequent play failure, not separately
        // modelled): cancel-and-resubmit.
        CancelAndResubmit(rec, i);
        return;
    }
    slot.hasActiveAnimation = true;  // step5: play it

    // The "critical end" tag check (step5), the camera gate/path-clear/
    // hand-off cascade (step6) - camera/physics systems this slice does
    // not model, NOT reproduced.

    slot.enterRequest.fastEntryLatched = true;  // step7: latch
    slot.enterTimerMs = -1;                      // step7: clear +0x28 to -1
}

// ===========================================================================
// U6.7 Sec11.4.1 step3 - the hijacker-resync scan.
// ===========================================================================

int VehicleInteractionRegistry::FindHijackerSlotTargetingSeat(VehicleInteractionRecord& rec, int seat) {
    for (int k = 0; k < kSlotsPerRecord; ++k) {
        VehicleSlot& s = rec.slots[k];
        if (s.occupantHandle == 0) continue;
        if (s.phase != VehicleSlotPhase::kEnter) continue;
        if (s.state != kEnterExtract && s.state != kEnterFast) continue;
        if (s.enterRequest.requestedSeat == seat) return k;
    }
    return -1;
}

// ===========================================================================
// U6.7 Sec11.5.2's exit-kind chooser (0x00b04ae0).
// ===========================================================================

int32_t VehicleInteractionRegistry::ChooseExitKind(VehicleInteractionRecord& rec, int i, bool flagForced) {
    VehicleSlot& slot = rec.slots[i];
    ExitRequestRecord& req = slot.exitRequest;
    const auto& ext = slot.external;
    auto resolve = [&](int32_t kind) { slot.variant10 = kind; return kind; };

    if (flagForced) {
        if (req.guardianAngelForced) {
            slot.sideIndex = -1;  // "+0x20 = -1"
            return resolve(kExitKindGuardianAngel);
        }
        if (req.forceEject) return resolve(kExitKindEject);
        // no other forced branch is CONFIRMED - falls through.
    }
    // Sec11.5.2's own HIGH-confidence reading is that the chooser can
    // genuinely leave the kind unresolved in some untraced circumstance
    // (U6.0's own census confirms state 0x21 is live, which per Sec11.6
    // step2 can only happen then) - this flag exercises exactly that,
    // instead of the ordinary cascade below.
    if (ext.exitKindChooserCannotResolve) return -1;

    if (ext.exitTippedAndAtAltitude) {
        slot.interactionPointType = 0xf;
        return resolve(kExitKindAtAltitude);
    }
    if (req.forceDive || ext.exitDiveRoomClear) {
        slot.interactionPointType = 0x10;
        return resolve(kExitKindDive);
    }
    if (ext.exitSideClearOutward) { slot.variant14 = 9; return resolve(kExitKindFast); }
    if (ext.exitSideClearForward) { slot.variant14 = 0x0a; return resolve(kExitKindFast); }
    if (ext.exitSideClearBackward) { slot.variant14 = 0x0b; return resolve(kExitKindFast); }
    if (ext.exitSpeedAtCruise) {
        slot.variant14 = 0x0c;
        slot.interactionPointType = 0x0d;
        return resolve(kExitKindAtSpeed);
    }
    if (ext.exitSpeedAtFast) {
        slot.variant14 = 0x0d;
        slot.interactionPointType = 0x0e;
        return resolve(kExitKindAtSpeed);
    }
    // "No branch was found that writes kind 0x14; it is HIGH: the
    // state's implicit default, left in place when no special-case
    // branch is taken" (Sec11.5.2) - reproduced as the explicit fallback.
    return resolve(kExitKindStandard);
}

// ===========================================================================
// U6.7 (Sec11) - the four animated exit states' own bodies.
// ===========================================================================

void VehicleInteractionRegistry::OnExitChangeSeat(VehicleInteractionRecord& rec, int i) {
    // 0x00b02330 (Sec11.3.1 on-entry, 57 insns, CONFIRMED).
    VehicleSlot& slot = rec.slots[i];
    slot.state = kExitChangeSeat;
    RecordTrace(kExitChangeSeat);
    slot.variant10 = slot.variant14 = slot.variant18 = -1;
    // The lookup uses seat = slot +0x2c (Sec9.4.1's change-seat
    // exception) - hit plays; a miss has no fallback of its own.
    slot.hasActiveAnimation = slot.external.animationLookupHit;
}

void VehicleInteractionRegistry::TickExitChangeSeat(VehicleInteractionRecord& rec, int i, int32_t /*dtMs*/) {
    // 0x00b062b0 (Sec11.3.2 per-frame, 88 insns, CONFIRMED).
    VehicleSlot& slot = rec.slots[i];
    if (!slot.external.animationFinished && slot.hasActiveAnimation) return;  // step2: wait for 0x00b0c170

    // step3: teleport to the new seat's transform - vehicle geometry
    // this slice does not model, NOT reproduced (the seat index itself
    // is real data a caller/future pass could update here; not invented).

    // step4/5: the exit chooser 0x00b02150 (U6.6 Sec6.7.2 step4's own
    // citation, already reproduced inline by OnExit15LeaveSeat) - reused
    // here unchanged.
    const int32_t next = slot.exitRequest.beingExtracted ? kExitExtract : kExitExit;
    SetExit(rec, i, next, true, false);
}

void VehicleInteractionRegistry::OnExitExtract(VehicleInteractionRecord& rec, int i) {
    // 0x00b02900 (Sec11.4.1 on-entry, 204 insns, CONFIRMED).
    VehicleSlot& slot = rec.slots[i];
    slot.state = kExitExtract;
    RecordTrace(kExitExtract);
    slot.variant10 = slot.exitRequest.exitKind;      // +0x10 = exit request +0x20
    slot.variant14 = -1;
    slot.variant18 = -1;
    slot.sideIndex = slot.exitRequest.approachSide;   // +0x20 = exit request +0x24
    slot.hasActiveAnimation = false;

    // step3: hijacker-resync scan.
    const int hijackerSlot = FindHijackerSlotTargetingSeat(rec, slot.seatIndex);

    // step4: the lookup (on the hijacker's enter-request if found, else
    // a sentinel/fallback) - stood in by the usual hit/miss signal.
    if (!slot.external.animationLookupHit) {
        // Miss: the instant fallback (Sec9.4.3's own table: with no
        // hijacker found, this key always misses real data).
        slot.exitRequest.instant = true;
        SetExit(rec, i, kExitInstantPlacement, true, false);
        return;
    }
    slot.hasActiveAnimation = true;  // hit: play directly

    // step5: fast-entry vs second-anim branch, keyed on the HIJACKER's
    // own enter-request fast-entry bit.
    if (hijackerSlot >= 0) {
        VehicleSlot& hijacker = rec.slots[hijackerSlot];
        if (hijacker.enterRequest.fastEntry) {
            // "called on the hijacker's own slot, not the victim's"
            // (Sec11.4.1 step5/Sec11.7.2's "three contexts" finding).
            StartFastEntry(rec, hijackerSlot);
        }
        // else: the second-anim/camera-gate branch needs the camera
        // system this slice does not model - NOT reproduced.
    }

    // step6: the full-victim removal path for a despawn-eligible
    // occupant (0x0070de70, a human-runtime contract, HIGH) - NOT
    // reproduced.
}

void VehicleInteractionRegistry::TickExitExtract(VehicleInteractionRecord& rec, int i, int32_t /*dtMs*/) {
    // 0x00b05b90 (Sec11.4.2 per-frame, 362 insns, CONFIRMED).
    VehicleSlot& slot = rec.slots[i];

    // "Not instantiated, or no human at all - finalise at once" vs
    // "Instantiated - wait, then snap and hand off." No per-occupant
    // instantiation flag exists in this registry (a different gap from
    // rec.vehicleInstantiatedNow, which is per-VEHICLE) - this slice
    // treats every occupant as instantiated (the common case for a human
    // actually being extracted), so only the "instantiated" branch below
    // applies; the inline-at-once branch is left for a future caller
    // that models a non-instantiated occupant.
    if (!slot.external.animationFinished && slot.hasActiveAnimation) return;

    // Once finished: the physics-proxy snap (step1) and the by-kind
    // weapon-drop/damage dispatch (step2) are human-runtime/physics
    // contracts this slice does not model - NOT reproduced; this slice's
    // own OBSERVABLE transition (inline to the finaliser) is reproduced
    // for the kind == kill (0xd) case only. Sec11.4.2 explicitly flags
    // every OTHER case as its own new OPEN item ("how an instantiated,
    // non-kill extract eventually reaches 0x19 is OPEN") - this slice
    // does not invent a transition for those; they stay in 0x1d.
    if (slot.variant10 == 0x0d) {
        SetExit(rec, i, kExitFinalise, true, false);
    }
}

void VehicleInteractionRegistry::OnExitExit(VehicleInteractionRecord& rec, int i) {
    // 0x00b05560 (Sec11.5.1 on-entry, 185 insns, CONFIRMED).
    VehicleSlot& slot = rec.slots[i];
    slot.state = kExitExit;
    RecordTrace(kExitExit);
    slot.variant10 = -1;
    slot.variant14 = -1;
    slot.variant18 = 6;  // exception (Sec11.0 item6): a live sub-timer, not -1

    // step3's point-set retry and step5's door handling need vehicle
    // geometry this slice does not model - NOT reproduced.

    // step4: choose the kind (always, since step2 just reset +0x10).
    ChooseExitKind(rec, i, /*flagForced=*/slot.exitRequest.guardianAngelForced || slot.exitRequest.forceEject);

    slot.exitRequest.exitStarted = false;  // step6: clear +0x32 bit0
}

void VehicleInteractionRegistry::TickExitExit(VehicleInteractionRecord& rec, int i, int32_t /*dtMs*/) {
    // 0x00b063c0 (Sec11.5.3 per-frame, 696 insns, CONFIRMED dominant
    // control flow - the exact kind-to-shape pairing of the "started"
    // branch below, beyond climb/eject, is HIGH per the spec's own
    // closing note, Sec11.5.3/OPEN item108).
    VehicleSlot& slot = rec.slots[i];
    ExitRequestRecord& req = slot.exitRequest;

    if (!req.exitStarted) {
        const bool overrideSet = req.animationOverride != -1;
        const bool skipPlay = (!overrideSet && !slot.external.animationLookupHit) ||
                               (overrideSet && req.ejectCompanionResolved);
        if (!skipPlay) {
            // Play the chosen (or overridden) animation; the jump-rope/
            // instantiate/network-force/camera-prime side effects (step
            // 2a-b) are role-only or need systems this slice does not
            // model - NOT reproduced. Stays in 0x1f this frame.
            slot.hasActiveAnimation = true;
            req.exitStarted = true;
            return;
        }
        // No clip to play.
        if (slot.variant10 == kExitKindEject) {
            // Eject fast-forwards to the finaliser in the same frame
            // (Sec11.5.4 step4) - no physics-velocity field exists here
            // to write into (see the file banner); only this OBSERVABLE
            // state effect is reproduced.
            SetExit(rec, i, kExitFinalise, true, false);
            return;
        }
        // The point-set teleport path needs vehicle geometry this slice
        // does not model (always "no point" here) - falls through to
        // the traverse fallback, confirming U6.6 Sec6.1's own citation
        // ("0x00b063c0 (0x1f per-frame) -> 0x21").
        SetExit(rec, i, kExitTraverse, true, false);
        return;
    }

    // A later frame (started).
    if (slot.variant10 == kExitKindClimb) {
        if (slot.variant18 == 6) {
            if (!slot.external.halfWindowElapsed.has_value()) {
                throw sr3luahost::OpenStateError(
                    "0x1f sub-timer half-window (+0x18 6->7)",
                    "spec-player-runtime.md Sec11.5.1 item6/Sec11.5.3");
            }
            if (!*slot.external.halfWindowElapsed) return;
            slot.variant18 = 7;
            return;
        }
        // +0x18 == 7: open the door fully / read the walk axes to feed
        // the climb - vehicle geometry this slice does not model, stays
        // in 0x1f (the OPEN tail Sec14.13/Sec11.5.3 itself flags).
        return;
    }
    if (slot.variant10 == kExitKindEject) {
        SetExit(rec, i, kExitFinalise, true, false);
        return;
    }
    // Every other kind: the dominant "several entries inline a set-exit
    // to 0x19 directly" shape (Sec11.5.3's own table) - the single,
    // documented simplification for the seven kinds whose exact pairing
    // beyond climb/eject is HIGH, not independently verified.
    SetExit(rec, i, kExitFinalise, true, false);
}

void VehicleInteractionRegistry::OnExitTraverse(VehicleInteractionRecord& rec, int i) {
    // "0x21's own on-entry always runs 0x1f's entire on-entry first"
    // (Sec11.0 item8/Sec11.6 step1) - confirmed, surprising behaviour,
    // reproduced literally.
    OnExitExit(rec, i);

    VehicleSlot& slot = rec.slots[i];
    if (slot.variant10 != -1) {
        // The common case in this slice's own cascade (ChooseExitKind
        // almost always resolves to at least kExitKindStandard) - state
        // stays 0x1f after the subroutine call, exactly as the real
        // engine's own citation describes for this case.
        return;
    }
    // Only reached when VehicleOccupancyExternalSignals::
    // exitKindChooserCannotResolve is set (ChooseExitKind's own doc
    // comment) - the documented, rarer path Sec11.6 step2 itself
    // describes, and the one U6.0's own census confirms as reachable
    // (state 0x21 IS live).
    slot.state = kExitTraverse;
    RecordTrace(kExitTraverse);
    slot.variant14 = slot.variant18 = -1;
    slot.interactionPointType = 0x10;
    slot.landingKind = 2;  // the animated-camera test's surface result - vehicle geometry not modelled, defaults to "vehicle"
    slot.hasActiveAnimation = slot.external.animationLookupHit;
    if (!slot.hasActiveAnimation) {
        slot.exitRequest.instant = true;
        SetExit(rec, i, kExitInstantPlacement, true, false);
    }
}

void VehicleInteractionRegistry::TickExitTraverse(VehicleInteractionRecord& rec, int i, int32_t /*dtMs*/) {
    // 0x00b06d30 (Sec11.6 per-frame, 147 insns, CONFIRMED).
    VehicleSlot& slot = rec.slots[i];
    if (slot.external.animationProgress < 1.0f && !slot.external.animationFinished) return;  // step2

    // step3: open the exit door fully (not modelled) and read the walk
    // axes to set the finaliser's own landing-locomotion bit.
    if (slot.external.isLocalPlayer) {
        slot.exitRequest.landingRunNotWalk = slot.external.anyWalkAxisNonZero;
    }
    // step4: inline to the finaliser.
    SetExit(rec, i, kExitFinalise, true, false);
}

// ===========================================================================
// Sec7.5/Sec9.5's "next-state chooser" 0x00afb220 - see the file banner.
// ===========================================================================

int32_t VehicleInteractionRegistry::ProbeOpenVsTraverse(VehicleInteractionRecord& rec, int i) {
    // Sec9.5: "a hand-built key open/open standard/none/ground/none,
    // matched directly on the enter set with counter -1. It decides
    // between 0x0c (open, only on a match) and 0x0b (traverse)."
    return rec.slots[i].external.animationLookupHit ? kEnterOpen : kEnterTraverse;
}

int32_t VehicleInteractionRegistry::ProbeExtractFailVsExtract(VehicleInteractionRecord& rec, int i) {
    // Sec9.5: "a temporary phase-1, state-0x0e slot with +0x14=5 and the
    // real +0x1c/+0x20: found -> 0x0e, otherwise -> 0x0d."
    return rec.slots[i].external.animationLookupHit ? kEnterExtractFailPick : kEnterExtract;
}

int32_t VehicleInteractionRegistry::NextStateChooser(VehicleInteractionRecord& rec, int i, int32_t flag) {
    if (nextStateChooserOverride) return nextStateChooserOverride(rec, i, flag);
    throw sr3luahost::OpenStateError(
        "next-state chooser 0x00afb220's full cascade",
        "spec-player-runtime.md Sec7.5/Sec9.5 (OPEN item109: only two of its seven CONFIRMED outcomes are resolved)");
}

}  // namespace sr3player
