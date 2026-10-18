// Implements include/sr3player/vehicle_occupancy.h - see that file's own
// banner for scope, citations and judgement calls. Section references below
// (SecN.M) are all to spec-player-runtime.md.
#include "sr3player/vehicle_occupancy.h"

#include <algorithm>
#include <cctype>

#include "sr3luahost/open_state.h"

namespace sr3player {

namespace {

bool EqualsIgnoreCase(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}

std::string SurfaceToken(int32_t raw) {
    // Sec9.3 step2 / Sec9.0 item3: -1 none, 0 air, 1 ground, 2 vehicle, 3 water.
    switch (raw) {
        case 0: return "air";
        case 1: return "ground";
        case 2: return "vehicle";
        case 3: return "water";
        default: return "none";
    }
}

std::string SideToken(int32_t raw) {
    // Sec9.1's side map (0x00b0a5f0).
    switch (raw) {
        case 0: return "left";
        case 1: return "right";
        case 2: return "front";
        case 3: return "front left";
        case 4: return "front right";
        case 5: return "back";
        default: return "none";
    }
}

// The 0x0d/0x1d shared "extract <variant>" list (Sec9.3), indexed 1..8.
const char* ExtractVariantName(int32_t index1to8) {
    switch (index1to8) {
        case 1: return "extract standard";
        case 2: return "extract punch";
        case 3: return "extract bash";
        case 4: return "extract kill";
        case 5: return "extract no door";
        case 6: return "extract convertible";
        case 7: return "extract kick";
        case 8: return "extract door slam";
        default: return nullptr;
    }
}

}  // namespace

// ===========================================================================
// U6.1 - the key builder (Sec9.3) and the wildcard matcher (Sec9.1/Sec9.4).
// ===========================================================================

std::array<std::string, 5> BuildElementKey(const ElementKeyInputs& in) {
    // Tokens produced by the action/variant step (1-3 of them - "short key"
    // means the real builder stops after 1 or 2, Sec9.3's own "Short keys"
    // note). Surface/side then land right after whatever was written.
    std::vector<std::string> tokens;
    bool anySurface = false;  // Sec9.3 step2: "any" for states 0x11 and 0x1b

    switch (in.state) {
        case 0x0a:  // prepare
            tokens = {"prepare", (in.variant24 == 2) ? "prepare lift" : "prepare pull", "none"};
            break;
        case 0x0b:  // traverse
            tokens = {"traverse", (in.traverseNoDoor ? "traverse no door" : "traverse standard"), "none"};
            break;
        case 0x0c: {  // open
            const char* variant = nullptr;
            if (in.variant14 == -1) variant = "open standard";
            else if (in.variant14 == 2) variant = "open shady";
            else if (in.variant14 == 3) variant = "open shopper";
            if (variant) tokens = {"open", variant, "none"};
            else tokens = {"open"};  // short key
            break;
        }
        case 0x0d: {  // extract
            const char* variant = ExtractVariantName(in.variant10);
            if (variant) tokens = {"extract", variant, "none"};
            else tokens = {"extract"};  // short key
            break;
        }
        case 0x0e:
        case 0x1e: {  // extract fail (also the U6.1 Sec9.5 temporary-slot-only case for 0x1e)
            if (in.variant14 == 0) tokens = {"extract", "extract fail", "any"};
            else if (in.variant14 == 5) tokens = {"extract", "extract fail", "extract fail 1"};
            else if (in.variant14 == 6) tokens = {"extract", "extract fail", "extract fail 2"};
            else if (in.variant14 == 7) tokens = {"extract", "extract fail", "extract fail 3"};
            else tokens = {"extract", "extract fail"};  // short key (token 2 only)
            break;
        }
        case 0x0f: {  // enter
            const char* variant;
            if (in.variant10 == 0) variant = "enter guardian angel";
            else if (in.variant14 == 4) variant = "enter shopper";
            else if (in.enterConvertible) variant = "enter convertible";
            else variant = "enter standard";
            tokens = {"enter", variant, "none"};
            break;
        }
        case 0x10:
        case 0x20:  // never held (Sec7.0 item2), but the key builder is a pure function regardless
            tokens = {"close", "none", "none"};
            break;
        case 0x11:
        case 0x1b:  // change seat
            tokens = {"change seat", "none", "none"};
            anySurface = true;
            break;
        case 0x12:  // never held
            tokens = {"activate", "none", "none"};
            break;
        case 0x13:  // fast
            tokens = {"fast", "none", "none"};
            break;
        case 0x14: case 0x15: case 0x16: case 0x17: case 0x18: case 0x19:
            tokens = {"none", "none", "none"};  // the default target (Sec6.12/Sec9.3)
            break;
        case 0x1a:  // never held
            tokens = {"deactivate", "none", "none"};
            break;
        case 0x1c:  // live (no local producer) but a pure key still exists
            tokens = {"open", "open standard", "none"};
            break;
        case 0x1d: {  // extract (exit side, "the 0x0d list offset by 9")
            if (in.variant10 >= 0x0a && in.variant10 <= 0x11) {
                tokens = {"extract", ExtractVariantName(in.variant10 - 9), "none"};
            } else if (in.variant10 == 0x13) {
                tokens = {"extract", "extract fast", "none"};
            } else {
                tokens = {"extract"};  // short key (incl. -1 and 0x12)
            }
            break;
        }
        case 0x1f: {  // exit
            switch (in.variant10) {
                case 0x14:
                    tokens = {"exit", in.exitConvertible ? "exit convertible" : "exit standard", "none"};
                    break;
                case 0x15: {  // exit fast
                    const char* t2 = nullptr;
                    if (in.exitConvertible) t2 = "exit fast convertible";
                    else if (in.variant14 == 9) t2 = "exit fast outward";
                    else if (in.variant14 == 0x0a) t2 = "exit fast forward";
                    else if (in.variant14 == 0x0b) t2 = "exit fast backward";
                    if (t2) tokens = {"exit", "exit fast", t2};
                    else tokens = {"exit", "exit fast"};  // short key at token2
                    break;
                }
                case 0x16: {  // exit at speed
                    const char* t2 = nullptr;
                    if (in.variant14 == 0x0c) t2 = "exit at speed cruise";
                    else if (in.variant14 == 0x0d) t2 = "exit at speed fast";
                    if (t2) tokens = {"exit", "exit at speed", t2};
                    else tokens = {"exit", "exit at speed"};  // short key at token2
                    break;
                }
                case 0x17:  // exit dive
                    tokens = {"exit", "exit dive", in.exitConvertible ? "exit dive convertible" : "none"};
                    break;
                case 0x18: tokens = {"exit", "exit at altitude", "none"}; break;
                case 0x19: tokens = {"exit", "exit crawl", "none"}; break;
                case 0x1a: tokens = {"exit", "exit climb", "none"}; break;
                case 0x1b: tokens = {"exit", "exit eject", "none"}; break;
                case 0x1c: tokens = {"exit", "exit guardian angel", "none"}; break;
                default: tokens = {"exit"}; break;  // short key
            }
            break;
        }
        case 0x21:  // traverse (exit side)
            tokens = {"traverse", "none", "none"};
            break;
        default:
            tokens = {"none", "none", "none"};  // outside 0x0a-0x21: the default case (Sec9.3 step1)
            break;
    }

    std::array<std::string, 5> key = {"none", "none", "none", "none", "none"};
    size_t idx = 0;
    for (; idx < tokens.size() && idx < 5; ++idx) key[idx] = tokens[idx];
    if (idx < 5) key[idx++] = anySurface ? "any" : SurfaceToken(in.surfaceRaw);
    if (idx < 5) key[idx++] = SideToken(in.sideRaw);
    // Any remaining position stays "none" (Sec9.7 item1's zero-fill rule).
    return key;
}

std::optional<std::string> MatchElementKey(
    const std::vector<sr3tables_vehicle_world::VehicleInteractionAnimationElement>& set,
    const std::array<std::string, 5>& key) {
    for (const auto& element : set) {
        bool match = true;
        for (size_t pos = 0; pos < 5 && match; ++pos) {
            if (EqualsIgnoreCase(key[pos], "any")) continue;  // key-side wildcard only (Sec9.7 item2)
            const std::string elemToken = (pos < element.parameters.size()) ? element.parameters[pos] : std::string("none");
            if (!EqualsIgnoreCase(key[pos], elemToken)) match = false;
        }
        if (match) return element.animation;
    }
    return std::nullopt;  // silent miss (Sec9.4.2/Sec9.7 item5)
}

std::optional<std::string> FindAnimation(
    VehicleSlotPhase phase, const ElementKeyInputs& in,
    const std::vector<sr3tables_vehicle_world::VehicleInteractionAnimationElement>& enterSet,
    const std::vector<sr3tables_vehicle_world::VehicleInteractionAnimationElement>& exitSet) {
    const auto key = BuildElementKey(in);
    return MatchElementKey(phase == VehicleSlotPhase::kEnter ? enterSet : exitSet, key);
}

// ===========================================================================
// U6.0 - registry mechanics (Sec7.1/Sec7.3).
// ===========================================================================

VehicleInteractionRecord* VehicleInteractionRegistry::FindByVehicle(uint64_t vehicleHandle) {
    for (auto& rec : records_) {
        if (rec.vehicleHandle == vehicleHandle) return &rec;
    }
    return nullptr;
}

VehicleInteractionRecord* VehicleInteractionRegistry::ClaimRecord(uint64_t vehicleHandle) {
    if (auto* existing = FindByVehicle(vehicleHandle)) return existing;
    if (records_.size() >= kCapacity) return nullptr;  // registry full (Sec7.4 step7)
    records_.emplace_back();
    records_.back().vehicleHandle = vehicleHandle;
    return &records_.back();
}

int VehicleInteractionRegistry::AllocateSlot(VehicleInteractionRecord& rec, uint64_t occupantHandle) {
    for (int i = 0; i < kSlotsPerRecord; ++i) {
        if (rec.slots[i].occupantHandle == 0) {
            rec.slots[i] = VehicleSlot{};
            rec.slots[i].occupantHandle = occupantHandle;
            return i;
        }
    }
    return -1;  // record full (Sec7.4 step8: "-1 -> false")
}

void VehicleInteractionRegistry::ReleaseSlotInternal(VehicleInteractionRecord& rec, int i) {
    VehicleSlot& slot = rec.slots[i];
    const uint64_t occ = slot.occupantHandle;
    if (slot.seatIndex >= 0 && slot.seatIndex < kSlotsPerRecord) {
        auto& seatRec = rec.seats[slot.seatIndex];
        if (seatRec.seatedOccupant == occ) seatRec.seatedOccupant = 0;  // Sec7.3 release step1
    }
    stateEnums_[occ] = 0;  // +0x16d4 = 0 (Sec7.3 step2/Sec7.8)
    seatOf_.erase(occ);
    if (rec.driverHandle == occ) rec.driverHandle = 0;
    slot = VehicleSlot{};  // free the slot (Sec7.3 step3/4)
}

void VehicleInteractionRegistry::ReleaseSlot(VehicleInteractionRecord& rec, int slotIndex) {
    if (slotIndex < 0 || slotIndex >= kSlotsPerRecord) return;
    ReleaseSlotInternal(rec, slotIndex);
}

std::pair<VehicleInteractionRecord*, int> VehicleInteractionRegistry::FindSlotByOccupant(
    uint64_t occupantHandle) {
    for (auto& rec : records_) {
        for (int i = 0; i < kSlotsPerRecord; ++i) {
            if (rec.slots[i].occupantHandle == occupantHandle) return {&rec, i};
        }
    }
    return {nullptr, -1};
}

// --- The three setters (Sec7.5): dispatch only, no state write here - each
// on-entry handler stores its own state number (Sec7.0 item2). A state with
// no handler below (the 5 never-held numbers) leaves the slot untouched. ---

void VehicleInteractionRegistry::SetEnter(VehicleInteractionRecord& rec, int slotIndex, int32_t state,
                                           bool /*send*/) {
    DispatchEnterOnEntry(rec, slotIndex, state);
}

void VehicleInteractionRegistry::SetExit(VehicleInteractionRecord& rec, int slotIndex, int32_t state,
                                          bool /*send*/, bool /*fromNetwork*/) {
    // Sec7.5: set-exit is a no-op unless the occupant resolves to a human;
    // this module has no non-human occupant kind, so every slot "resolves".
    // The tip-over test (Sec6.7.1, run before 0x15 when not from the
    // network) needs vehicle orientation data this slice does not model -
    // OPEN, not reproduced (see the header's U6.6 scope note).
    DispatchExitOnEntry(rec, slotIndex, state);
}

void VehicleInteractionRegistry::SetRide(VehicleInteractionRecord& rec, int slotIndex, int32_t state) {
    DispatchRideOnEntry(rec, slotIndex, state);
}

void VehicleInteractionRegistry::StubOnEntry(VehicleInteractionRecord& rec, int i, int32_t state) {
    rec.slots[i].state = state;
    RecordTrace(state);
}

void VehicleInteractionRegistry::DispatchEnterOnEntry(VehicleInteractionRecord& rec, int i, int32_t state) {
    switch (state) {
        case kEnterSubmitted: OnEnterRequested(rec, i); return;          // state 0 (Sec10.2)
        case kEnterHub: OnEnterHub(rec, i); return;                       // state 1 (Sec10.3)
        case kEnterInstantWaitSeat: OnEnterInstantWaitSeat(rec, i); return;  // state 2 (Sec10.5)
        case 0x03: StubOnEntry(rec, i, state); return;                    // change-seat wait - U6.3's own body, not this slice's (Sec10.5)
        case kEnterInstantTakeSeat: OnEnterInstantTakeSeat(rec, i); return;  // state 4 (Sec10.5)
        case 0x05: case 0x06: case 0x07:
            StubOnEntry(rec, i, state);  // walk / arrival router / dead - U6.3's own walk machinery, not this slice's (Sec10.6/10.7/10.8)
            return;
        case kEnterCancel: OnEnterCancel(rec, i); return;                 // state 8 (Sec10.9)
        case kEnterCommit: OnEnterCommit(rec, i); return;                 // state 9 (Sec8.2)
        case kEnterPrepare: OnEnterPrepare(rec, i); return;               // state 0x0a (Sec14.2)
        case kEnterTraverse: OnEnterTraverse(rec, i); return;             // state 0x0b (Sec14.3)
        case kEnterOpen: OnEnterOpen(rec, i); return;                     // state 0x0c (Sec14.4)
        case kEnterExtract: OnEnterExtract(rec, i); return;               // state 0x0d (Sec14.5)
        case kEnterExtractFailPick: OnEnterExtractFailPick(rec, i); return;  // state 0x0e (Sec14.6)
        case kEnterForcedInPlace: OnEnterForcedInPlace(rec, i); return;   // state 0x0f (Sec10.4.4/Sec14.7)
        case kEnterChangeSeat: OnEnterChangeSeat(rec, i); return;         // state 0x11 (Sec14.9)
        case kEnterFast: OnEnterFast(rec, i); return;                     // state 0x13 (Sec14.10)
        // 0x10, 0x12: never held (Sec7.0 item2/3) - no case, slot unchanged.
        default:
            return;
    }
}

void VehicleInteractionRegistry::DispatchExitOnEntry(VehicleInteractionRecord& rec, int i, int32_t state) {
    switch (state) {
        case kExitRequested: OnExit14Requested(rec, i); return;
        case kExitLeaveSeat: OnExit15LeaveSeat(rec, i); return;
        case kExitWaitSeat: OnExit16WaitSeat(rec, i); return;
        case kExitInstantPlacement: OnExit17InstantPlacement(rec, i); return;
        case kExitParked: OnExit18Parked(rec, i); return;
        case kExitFinalise: OnExit19Finalise(rec, i); return;
        case kExitChangeSeat: OnExitChangeSeat(rec, i); return;  // state 0x1b (Sec11.3)
        case 0x1c: StubOnEntry(rec, i, state); return;            // 0x1c's own on-entry is confirmed EMPTY (Sec11.2) - the generic stub reproduces it exactly
        case kExitExtract: OnExitExtract(rec, i); return;          // state 0x1d (Sec11.4)
        case kExitExit: OnExitExit(rec, i); return;                // state 0x1f (Sec11.5)
        case kExitTraverse: OnExitTraverse(rec, i); return;        // state 0x21 (Sec11.6)
        // 0x1a, 0x1e, 0x20: never held - no case, slot unchanged.
        default:
            return;
    }
}

void VehicleInteractionRegistry::DispatchRideOnEntry(VehicleInteractionRecord& rec, int i, int32_t state) {
    // "Only state 0x22 reaches its on-entry" (Sec7.5) - anything else is a
    // no-op, matching the real set-ride exactly.
    if (state == kRideActive) OnRideActive(rec, i);
}

// --- U6.0's scoped-down submitter (Sec7.4 steps 7/8/11) ---

std::pair<VehicleInteractionRecord*, int> VehicleInteractionRegistry::SubmitEnterRequest(
    uint64_t vehicleHandle, uint64_t occupantHandle, EnterRequestRecord request) {
    VehicleInteractionRecord* rec = ClaimRecord(vehicleHandle);
    if (!rec) return {nullptr, -1};
    const int slot = AllocateSlot(*rec, occupantHandle);
    if (slot < 0) return {rec, -1};
    rec->slots[slot].enterRequest = request;
    SetEnter(*rec, slot, kEnterSubmitted, true);
    return {rec, slot};
}

// --- Enter-side on-entry handlers this slice implements ---

void VehicleInteractionRegistry::OnEnterRequested(VehicleInteractionRecord& rec, int i) {
    // 0x00af9a90 (Sec10.2), scoped to the CONFIRMED core: phase/stateEnum/
    // state (step1), the condition row (step6: "the human cannot walk but
    // can look while the entry is pending"), and the stick-abort grace
    // (step8). NOT reproduced: the local-player-only side effects (steps
    // 3-5: leave fine aim/scope view, zero the animation-facing velocity,
    // the camera look-at clear) and the reload/skydive/weapon cancellations
    // of step4 - each needs a system (camera/animation/inventory) this
    // slice does not touch.
    VehicleSlot& slot = rec.slots[i];
    slot.state = kEnterSubmitted;
    RecordTrace(kEnterSubmitted);
    slot.phase = VehicleSlotPhase::kEnter;          // Sec7.8/Sec10.2 step1: slot +0x08 = 1
    stateEnums_[slot.occupantHandle] = 1;            // Sec7.8/Sec10.2 step1: human +0x16d4 = 1
    slot.variant10 = slot.variant14 = slot.variant18 = -1;  // Sec10.2 step1

    conditionRows_[slot.occupantHandle] = 0x0a;  // Sec10.2 step6: condition row 0xa

    // Sec10.2 step8: arm the 800ms stick-abort grace. The real engine arms
    // this unconditionally (the PER-FRAME check is what skips an instant
    // request, Sec10.0 item8/TickStickAbort below).
    slot.stickAbortGraceMs = 800;
}

void VehicleInteractionRegistry::OnEnterHub(VehicleInteractionRecord& rec, int i) {
    // 0x00afd4c0 (Sec10.3), scoped to the routing decision (step15/Sec10.0
    // item1) and its CONFIRMED prerequisites: the "begun" callback (step2),
    // the claim-and-enqueue (step7, simplified - see EnterRequestRecord's
    // own doc comment on the distance-ordered queue, Sec10.1's own defect/
    // HYPOTHESIS, not reproduced), the force-instant pre-rules (step11) and
    // the instant branch's eviction (Sec10.4.2 H1-H4). NOT reproduced:
    // steps 3-6/8-10/12-14/16-18 (surface token, towed-object release,
    // "clear the way" for the local player, combat-ready/scripted-move
    // cancellation, hat-hiding, animation-state ends, the vehicle-AI
    // reaction, the door unlatch) - each needs a system (camera, animation,
    // vehicle-AI, customisation, door/physics) this slice does not touch.
    // "State 1 is never held across a frame" (Sec10.0 item1) is reproduced
    // naturally: every branch below calls SetEnter again before returning.
    VehicleSlot& slot = rec.slots[i];
    slot.state = kEnterHub;
    RecordTrace(kEnterHub);
    slot.variant10 = slot.variant14 = slot.variant18 = -1;
    EnterRequestRecord& req = slot.enterRequest;
    const int32_t seat = req.requestedSeat;

    // step2: the "begun" callback, fired once with ok=true.
    if (req.onBegunCallback) {
        req.onBegunCallback(true);
        req.onBegunCallback = nullptr;
    }

    // step7 (simplified claim-and-enqueue, Sec10.1).
    if (seat >= 0 && seat < kSlotsPerRecord) {
        VehicleSeatRecord& seatRec = rec.seats[seat];
        if (seatRec.claim == 0) seatRec.claim = slot.occupantHandle;
        else if (seatRec.claim != slot.occupantHandle) seatRec.nextClaim = slot.occupantHandle;
        seatOf_[slot.occupantHandle] = seat;  // +0x16c8
    }

    if (req.instant) {
        // Sec10.4.2 H1-H4.
        uint64_t occupant = (seat >= 0 && seat < kSlotsPerRecord) ? rec.seats[seat].seatedOccupant : 0;
        if (occupant != 0 && occupant != slot.occupantHandle && req.requestKind != 0) {
            if (req.isTankEntry && req.evictedVictimIsPlayerClass) {
                // H1 tank branch: a player-class victim cancels the entry outright.
                SetEnter(rec, i, kEnterCancel, true);
                return;
            }
            // Ordinary eviction - also this slice's simplified stand-in for
            // the tank's own "kill and destroy" sub-case on a non-player
            // victim (Sec10.4.2 H1): no damage/AI system is modelled here,
            // so the occupant is asked to leave instead of being killed.
            // This only REQUESTS the exit (Sec6.6.1's own 0x14 does not
            // auto-advance unless vehicleMayBeGone is set) - the seat is
            // not necessarily free yet when this call returns.
            RequestExit(occupant, ExitRequestRecord{});
        }
        // H4
        if (req.requestKind == 1) {
            SetEnter(rec, i, kEnterCancel, true);
            return;
        }
        occupant = (seat >= 0 && seat < kSlotsPerRecord) ? rec.seats[seat].seatedOccupant : 0;
        if (occupant != 0 && occupant != slot.occupantHandle) {
            SetEnter(rec, i, kEnterInstantWaitSeat, true);
        } else {
            SetEnter(rec, i, kEnterInstantTakeSeat, true);
        }
        return;
    }

    if (req.fastEntry) {  // request +0x23 bit0 (Sec10.3 step15)
        SetEnter(rec, i, kEnterFast, true);
        return;
    }
    if (req.forced || req.hasAnimationOverride) {  // Sec10.3 step15
        SetEnter(rec, i, kEnterForcedInPlace, true);
        return;
    }
    SetEnter(rec, i, 0x05, true);  // the walk to the vehicle (Sec10.3 step15) - U6.3's own body, stubbed here
}

void VehicleInteractionRegistry::OnEnterInstantWaitSeat(VehicleInteractionRecord& rec, int i) {
    // 0x00af95d0 (Sec10.5): resolve, store the state, variant reset. Nothing else.
    VehicleSlot& slot = rec.slots[i];
    slot.state = kEnterInstantWaitSeat;
    RecordTrace(kEnterInstantWaitSeat);
    slot.variant10 = slot.variant14 = slot.variant18 = -1;
}

void VehicleInteractionRegistry::TickEnterInstantWaitSeat(VehicleInteractionRecord& rec, int i, int32_t /*dtMs*/) {
    // 0x00afeca0 (Sec10.5): "seat without an occupant -> set-enter 4". No
    // time limit (Sec10.4.2 step4: "with request +0x08 = 0 nobody evicts
    // the occupant ... the wait lasts until the occupant leaves of its own
    // accord" - that case is folded into this same check since this slot's
    // own request kind already decided, at the hub, whether eviction ran).
    VehicleSlot& slot = rec.slots[i];
    const int32_t seat = slot.enterRequest.requestedSeat;
    const uint64_t occupant = (seat >= 0 && seat < kSlotsPerRecord) ? rec.seats[seat].seatedOccupant : 0;
    if (occupant == 0 || occupant == slot.occupantHandle) {
        SetEnter(rec, i, kEnterInstantTakeSeat, true);
    }
}

void VehicleInteractionRegistry::OnEnterInstantTakeSeat(VehicleInteractionRecord& rec, int i) {
    // 0x00afabd0 (Sec10.5): "if the human is the head of its entry-point
    // queue: tilt fix, then set-enter 9 - the commit. Otherwise it stays in
    // 4." Head-of-queue is simplified to "holds (or can take) the seat
    // claim" here (Sec10.1's own distance-ordered queue is not reproduced,
    // see EnterRequestRecord's doc comment). The tilt fix (righting a
    // tipped vehicle) needs vehicle orientation data this slice does not
    // model - not reproduced.
    VehicleSlot& slot = rec.slots[i];
    slot.state = kEnterInstantTakeSeat;
    RecordTrace(kEnterInstantTakeSeat);
    slot.variant10 = slot.variant14 = slot.variant18 = -1;

    const int32_t seat = slot.enterRequest.requestedSeat;
    bool headOfQueue = true;
    if (seat >= 0 && seat < kSlotsPerRecord) {
        VehicleSeatRecord& seatRec = rec.seats[seat];
        headOfQueue = (seatRec.claim == 0 || seatRec.claim == slot.occupantHandle);
        if (headOfQueue) seatRec.claim = slot.occupantHandle;
    }
    if (headOfQueue) SetEnter(rec, i, kEnterCommit, true);
    // else: stays in 4, retried by TickEnterInstantTakeSeat (per-frame, Sec10.5).
}

void VehicleInteractionRegistry::TickEnterInstantTakeSeat(VehicleInteractionRecord& rec, int i, int32_t /*dtMs*/) {
    // 0x00afeeb0 (Sec10.5), simplified: re-check the claim and retry the
    // commit. The real per-frame also cancels a still-walking queue head
    // and evicts a reappeared occupant (not reproduced - no competing
    // claimant/occupant scenario is modelled past the hub's own eviction).
    OnEnterInstantTakeSeat(rec, i);
}

void VehicleInteractionRegistry::OnEnterCancel(VehicleInteractionRecord& rec, int i) {
    // 0x00afb930 (Sec10.9), scoped to the CONFIRMED core: state/variant
    // reset (step1), the "begun" callback fired with ok=false and the
    // completion callback NEVER touched (step3/Sec10.0 item4 - reproduced
    // by omission, since this slice has no completion-callback field at
    // all), the claim/next-claim clean-up (step4, simplified - same
    // footing as the hub's own claim-and-enqueue), the condition row back
    // to 0 (step9, "undoes state 0's row 0xa"), and the release LAST
    // (step12). NOT reproduced: the door/vehicle-AI/camera/hat/ragdoll/
    // upright side effects (steps 5, 7, 8, 11, 13) - each needs a system
    // this slice does not touch.
    VehicleSlot& slot = rec.slots[i];
    const uint64_t occ = slot.occupantHandle;
    slot.state = kEnterCancel;
    RecordTrace(kEnterCancel);
    slot.variant10 = slot.variant14 = slot.variant18 = -1;

    EnterRequestRecord& req = slot.enterRequest;
    if (req.onBegunCallback) {           // step3: ok=false; +0x3c is never touched (Sec10.0 item4)
        req.onBegunCallback(false);
        req.onBegunCallback = nullptr;
    }

    const int32_t seat = req.requestedSeat;  // step4 (simplified)
    if (seat >= 0 && seat < kSlotsPerRecord) {
        VehicleSeatRecord& seatRec = rec.seats[seat];
        if (seatRec.claim == occ) {
            seatRec.claim = seatRec.nextClaim;
            seatRec.nextClaim = 0;
        } else if (seatRec.nextClaim == occ) {
            seatRec.nextClaim = 0;
        }
    }

    conditionRows_[occ] = 0;  // step9

    ReleaseSlotInternal(rec, i);  // step12: release last
}

void VehicleInteractionRegistry::OnEnterCommit(VehicleInteractionRecord& rec, int i) {
    // Sec8.2's commit, scoped to the CONFIRMED observable core - see the
    // header's own doc comment on CommitSeating for what is NOT reproduced
    // (weapon holstering, vehicle-band callees, AI/drunk-driver, HUD/
    // tutorial/cheat effects, the team-check re-exit, the camera hand-off).
    VehicleSlot& slot = rec.slots[i];
    const uint64_t occ = slot.occupantHandle;

    slot.state = kEnterCommit;                        // step1: +0x0c = 9
    RecordTrace(kEnterCommit);
    slot.variant10 = slot.variant14 = slot.variant18 = -1;
    const int32_t seat = slot.enterRequest.requestedSeat;
    slot.seatIndex = seat;                              // step3: +0x2c = request +0x10

    if (seat >= 0 && seat < kSlotsPerRecord) {
        VehicleSeatRecord& seatRec = rec.seats[seat];
        seatRec.seatedOccupant = occ;                    // step5 (0x00b0b930): +0x18 = human
        seatRec.claim = seatRec.nextClaim;                // claim <- next claim
        seatRec.nextClaim = 0;
        seatOf_[occ] = seat;                               // +0x16c8 = seat
    }

    // step21 - the task's own central ask: condition row +0xcc8 = 8 (seat 0)
    // / 9 (elsewhere), via the shared locomotion setter 0x00942110(human,
    // 8/9, 0, -1.0) (Sec8.1/Sec8.2 step21).
    conditionRows_[occ] = (seat == 0) ? 8 : 9;

    if (seat == 0) {
        rec.driverHandle = occ;  // step24: vehicle +0x16d8/+0x16dc (Sec8.1)
    }

    // Sec8.2 step33's team-check immediate re-exit is NOT reproduced here:
    // this project has no "team" field anywhere to compare (OPEN, not
    // invented - see the header's doc comment on CommitSeating).

    SetRide(rec, i, kRideActive);  // step29: "near the end" (Sec8.2)
}

void VehicleInteractionRegistry::OnRideActive(VehicleInteractionRecord& rec, int i) {
    // 0x00b099d0 (Sec8.5): resolve, +0x16d4 = 3, slot phase = 3, state =
    // 0x22. "If an item is in hand, attach it" is skipped - no inventory
    // system in this project's current scope.
    VehicleSlot& slot = rec.slots[i];
    slot.phase = VehicleSlotPhase::kRide;
    stateEnums_[slot.occupantHandle] = 3;
    slot.state = kRideActive;
    RecordTrace(kRideActive);
}

// --- Exit-side on-entry/per-frame handlers (Sec6) ---

void VehicleInteractionRegistry::OnExit14Requested(VehicleInteractionRecord& rec, int i) {
    // 0x00b021c0 (Sec6.6.1).
    VehicleSlot& slot = rec.slots[i];
    const uint64_t occ = slot.occupantHandle;
    stateEnums_[occ] = 2;                               // step1: human +0x16d4 = 2
    slot.phase = VehicleSlotPhase::kExit;                // step2: slot +0x08 = 2
    slot.state = kExitRequested;                          // step2: +0x0c = 0x14
    RecordTrace(kExitRequested);
    slot.variant10 = slot.variant14 = slot.variant18 = -1;

    // step3: unless down/dead (not modelled - always applies here):
    // condition row = 0xb, locomotion 0, speed -1 (Sec6.6.1/Sec5.5's terms).
    conditionRows_[occ] = 0x0b;

    // step5: skip-the-wait. A single-player port treats the network gate as
    // always true (Sec6.13 point3), so the condition reduces to just
    // "vehicleMayBeGone".
    if (slot.exitRequest.vehicleMayBeGone) {
        slot.exitRequest.instant = true;
        SetExit(rec, i, kExitLeaveSeat, true, false);
    }
}

void VehicleInteractionRegistry::TickExit14(VehicleInteractionRecord& rec, int i, int32_t dtMs) {
    // 0x00b05b00 (Sec6.6.2): network gate always true in single-player
    // (Sec6.13 point3), so the condition reduces to "the start time has
    // passed".
    VehicleSlot& slot = rec.slots[i];
    if (slot.exitRequest.startDelayMs > 0) slot.exitRequest.startDelayMs -= dtMs;
    if (slot.exitRequest.startDelayMs <= 0) {
        SetExit(rec, i, kExitLeaveSeat, true, false);
    }
}

void VehicleInteractionRegistry::OnExit15LeaveSeat(VehicleInteractionRecord& rec, int i) {
    // 0x00b036c0 (Sec6.7.2), scoped to the routing decision and the
    // directly-reproducible parts. NOT reproduced: the despawn-dead early
    // return (needs network/isDead/isRagdolled/class-bit inputs this slice
    // does not model), the human-side/driver's-vehicle hand-off calls
    // (vehicle-band, no vehicle physics here), and the actual inventory
    // switch of equip-on-exit (Sec6.7.3) - only its REQUEST is recorded.
    VehicleSlot& slot = rec.slots[i];
    slot.state = kExitLeaveSeat;
    RecordTrace(kExitLeaveSeat);
    slot.variant10 = slot.variant14 = slot.variant18 = -1;
    slot.landingKind = 1;  // step2: "+0x1c = 1"

    ExitRequestRecord& req = slot.exitRequest;
    if (req.instant) {
        // Instant path (Sec6.7.2).
        req.exitSeat = slot.seatIndex;
        const bool humanUsable = true;  // not modelled further (no life-state here) - defaults to the common case
        if (humanUsable && !req.instantPathSkipTo19) {
            SetExit(rec, i, kExitInstantPlacement, true, false);
        } else {
            SetExit(rec, i, kExitFinalise, true, false);
        }
    } else {
        // Animated path (Sec6.7.2 step4): equip-on-exit is recorded, not
        // acted on (Sec6.7.3 - see ExitRequestRecord::equipRifleOnExitResolved).
        slot.equipRifleOnExitRequested = req.equipRifleOnExitResolved;
        const int32_t target = req.beingExtracted ? kExitExtract : kExitExit;
        // This lands in a generic StubOnEntry (U6.7's own body, out of
        // scope for this slice) - only the state number is recorded.
        SetExit(rec, i, target, true, false);
    }
}

void VehicleInteractionRegistry::OnExit16WaitSeat(VehicleInteractionRecord& rec, int i) {
    // 0x00b022c0 (Sec6.8): state, variant reset only.
    VehicleSlot& slot = rec.slots[i];
    slot.state = kExitWaitSeat;
    RecordTrace(kExitWaitSeat);
    slot.variant10 = slot.variant14 = slot.variant18 = -1;
}

void VehicleInteractionRegistry::TickExit16(VehicleInteractionRecord& rec, int i, int32_t dtMs) {
    // 0x00b06020 (Sec6.8). Only the "wait at the own seat" branch's timeout
    // sub-path is reproduced: the door-clear check (0x00b0a3f0) and the
    // "move to another seat" sub-path both need vehicle-band seat-occupancy
    // queries this slice does not model (OPEN, not invented).
    VehicleSlot& slot = rec.slots[i];
    if (!slot.exitRequest.waitAtOwnSeat) return;  // OPEN sub-path, see above
    if (slot.exitWaitSeatTimerMs > 0) slot.exitWaitSeatTimerMs -= dtMs;
    if (slot.exitWaitSeatTimerMs > 0) return;  // still waiting (timer unarmed, i.e. <=0, means "expired")
    slot.exitRequest.instant = true;
    SetExit(rec, i, kExitInstantPlacement, true, false);
}

void VehicleInteractionRegistry::OnExit17InstantPlacement(VehicleInteractionRecord& rec, int i) {
    // 0x00b02640 (Sec6.9): placement/animation calls need vehicle
    // geometry/an animation system this slice does not model - only the
    // bookkeeping and the pass-through to the finaliser are reproduced.
    VehicleSlot& slot = rec.slots[i];
    slot.state = kExitInstantPlacement;
    RecordTrace(kExitInstantPlacement);
    slot.variant10 = slot.variant14 = slot.variant18 = -1;
    if (!slot.exitRequest.keepInPlace) slot.landingKind = 1;  // "ground", no real teleport modelled
    SetExit(rec, i, kExitFinalise, true, false);  // "0x17 is a pass-through" (Sec6.9)
}

void VehicleInteractionRegistry::OnExit18Parked(VehicleInteractionRecord& rec, int i) {
    // 0x00b02860 (Sec6.10): "no transition and no per-frame handler" -
    // reachable only via the network applier in the real game, or (here) a
    // direct test call.
    VehicleSlot& slot = rec.slots[i];
    slot.state = kExitParked;
    RecordTrace(kExitParked);
    slot.variant10 = slot.variant14 = slot.variant18 = -1;
}

ExitFinaliseResult VehicleInteractionRegistry::OnExit19Finalise(VehicleInteractionRecord& rec, int i) {
    // 0x00b03d80 (Sec6.11), scoped to the CONFIRMED core: state/variant
    // reset, the condition-row write to 0 ("keeps the locomotion state" -
    // not modelled further, this slice has no shared locomotion-state
    // storage), the landing-kind outcome (surfaced, not acted on), and the
    // release LAST (Sec6.13 point5's explicit ordering requirement).
    //
    // The "saved +0x10 == 0x17 or 0x18" airborne gate (Sec6.11 step11) is
    // flagged OPEN by the spec itself ("that field's meaning is U6.0's/
    // U6.1's", Sec6.11's own closing note) - reproduced literally below,
    // but since this slice resets +0x10 to -1 at every on-entry it
    // implements (Sec7.11 OPEN48), the saved value reaching this function
    // through this slice's own code paths is always -1, so this gate is
    // always false here. A future unit that actually writes +0x10 to 0x17/
    // 0x18 before reaching 0x19 would need to revisit this.
    VehicleSlot& slot = rec.slots[i];
    const uint64_t occ = slot.occupantHandle;
    const int32_t oldVariant10 = slot.variant10;  // step1: "save the old +0x10"

    slot.state = kExitFinalise;
    RecordTrace(kExitFinalise);
    slot.variant10 = slot.variant14 = slot.variant18 = -1;

    conditionRows_[occ] = 0;  // step9: condition row = 0 ("keeps the locomotion state")

    // step11's landing-kind branch (ragdoll not modelled - always the
    // "not ragdolled" path).
    ExitFinaliseResult result;
    result.oldVariant10 = oldVariant10;
    const bool airborneGate = (oldVariant10 == 0x17 || oldVariant10 == 0x18);  // see doc comment above
    if (slot.landingKind == 0 && airborneGate) {
        result.landing = ExitFinaliseResult::Landing::kAirborne;
    } else if (slot.landingKind == 3) {  // water (Sec9.3's +0x1c=3 mapping)
        result.landing = ExitFinaliseResult::Landing::kWater;
    } else {
        result.landing = ExitFinaliseResult::Landing::kGround;
    }

    ReleaseSlotInternal(rec, i);  // step15: "release the slot" - last (Sec6.13 point5)
    return result;
}

// --- U6.4: the seating commit's public entry point ---

VehicleInteractionRegistry::CommitSeatingResult VehicleInteractionRegistry::CommitSeating(
    uint64_t vehicleHandle, uint64_t occupantHandle, int32_t seatIndex, bool forced, bool skipTeamCheck) {
    CommitSeatingResult result;
    VehicleInteractionRecord* rec = ClaimRecord(vehicleHandle);
    if (!rec) return result;
    const int slot = AllocateSlot(*rec, occupantHandle);
    if (slot < 0) return result;

    EnterRequestRecord request;
    request.requestedSeat = seatIndex;
    request.forced = forced;
    request.skipTeamCheck = skipTeamCheck;
    rec->slots[slot].enterRequest = request;

    SetEnter(*rec, slot, kEnterSubmitted, true);  // "every submission starts at state 0" (Sec7.4)
    SetEnter(*rec, slot, kEnterCommit, true);      // this slice's own entry point into the commit

    result.record = rec;
    result.slotIndex = slot;
    return result;
}

// --- U6.6: the exit request (Sec6.3) ---

bool VehicleInteractionRegistry::RequestExit(uint64_t occupantHandle, ExitRequestRecord request,
                                              bool hasVehicleInteractionInfo, bool humanForcedInstantHint,
                                              ExitPermissionInputs permission) {
    auto [rec, slotIndex] = FindSlotByOccupant(occupantHandle);
    if (!rec || slotIndex < 0) return false;  // step1/6: no vehicle link / no slot

    // step4: force instant when there is no Vehicle_Interaction_Info record,
    // or the human's own +0x1c9c bit3 is set.
    if (!hasVehicleInteractionInfo || humanForcedInstantHint) request.instant = true;

    // step5: permission (Sec6.3.2). The local-player/UI gate (rule1) is
    // skipped (OPEN, not applicable outside that UI).
    const bool locked = (permission.cannotExitVehicles && !permission.sameVehicleAsActive) ||
                         (permission.cannotExitRcVehicle && permission.sameVehicleAsActive);
    const bool allowed = !locked || request.instant || permission.isDead || permission.isRagdolled;
    if (!allowed) return false;

    // step7: refuse an entering or already-exiting slot ("only riding slots
    // accepted" in practice; phase 0 is literally accepted too, Sec6.3.1).
    VehicleSlot& slot = rec->slots[slotIndex];
    if (slot.phase == VehicleSlotPhase::kEnter || slot.phase == VehicleSlotPhase::kExit) return false;

    slot.exitRequest = request;  // step8
    SetExit(*rec, slotIndex, kExitRequested, true, false);  // step9: always 0x14
    return true;
}

// --- Per-frame tick (Sec7.6 step5's phase dispatch only) ---

void VehicleInteractionRegistry::Tick(int32_t dtMs) {
    for (auto& rec : records_) {
        for (int i = 0; i < kSlotsPerRecord; ++i) {
            VehicleSlot& slot = rec.slots[i];
            if (slot.phase == VehicleSlotPhase::kExit) {
                // Only 0x14 and 0x16 have a per-frame handler in U6.6's
                // own range (Sec6.1's own table: "0x15, 0x17, 0x18 and
                // 0x19 have none"); U6.7 adds 0x1b/0x1d/0x1f/0x21 (0x1a/
                // 0x1c/0x1e/0x20 have none either, Sec11.1's own table).
                if (slot.state == kExitRequested) TickExit14(rec, i, dtMs);
                else if (slot.state == kExitWaitSeat) TickExit16(rec, i, dtMs);
                else if (slot.state == kExitChangeSeat) TickExitChangeSeat(rec, i, dtMs);
                else if (slot.state == kExitExtract) TickExitExtract(rec, i, dtMs);
                else if (slot.state == kExitExit) TickExitExit(rec, i, dtMs);
                else if (slot.state == kExitTraverse) TickExitTraverse(rec, i, dtMs);
            } else if (slot.phase == VehicleSlotPhase::kEnter) {
                // U6.3's instant route: only 0x02 and 0x04 have a per-frame
                // handler (Sec10.5); U6.5 adds 0x0a-0x0e/0x0f/0x11/0x13
                // (0x10/0x12 have none, Sec14.0 item1's own table).
                if (slot.state == kEnterInstantWaitSeat) TickEnterInstantWaitSeat(rec, i, dtMs);
                else if (slot.state == kEnterInstantTakeSeat) TickEnterInstantTakeSeat(rec, i, dtMs);
                else if (slot.state == kEnterPrepare) TickEnterPrepare(rec, i, dtMs);
                else if (slot.state == kEnterTraverse) TickEnterTraverse(rec, i, dtMs);
                else if (slot.state == kEnterOpen) TickEnterOpen(rec, i, dtMs);
                else if (slot.state == kEnterExtract) TickEnterExtract(rec, i, dtMs);
                else if (slot.state == kEnterExtractFailPick) TickEnterExtractFailPick(rec, i, dtMs);
                else if (slot.state == kEnterForcedInPlace) TickEnterForcedInPlace(rec, i, dtMs);
                else if (slot.state == kEnterChangeSeat) TickEnterChangeSeat(rec, i, dtMs);
                else if (slot.state == kEnterFast) TickEnterFast(rec, i, dtMs);
            }
        }
    }
}

// --- Observability ---

int32_t VehicleInteractionRegistry::ConditionRow(uint64_t occupantHandle) const {
    auto it = conditionRows_.find(occupantHandle);
    return it == conditionRows_.end() ? 0 : it->second;
}

int32_t VehicleInteractionRegistry::StateEnum(uint64_t occupantHandle) const {
    auto it = stateEnums_.find(occupantHandle);
    return it == stateEnums_.end() ? 0 : it->second;
}

std::optional<int> VehicleInteractionRegistry::SeatOf(uint64_t occupantHandle) const {
    auto it = seatOf_.find(occupantHandle);
    if (it == seatOf_.end()) return std::nullopt;
    return it->second;
}

}  // namespace sr3player
