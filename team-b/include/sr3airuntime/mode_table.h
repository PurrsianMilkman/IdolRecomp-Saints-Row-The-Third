#pragma once

// sr3airuntime/mode_table.h - AI runtime unit A0: the 44-entry mode table and
// the three-layer mode-switching protocol (request -> apply -> transition),
// the event pump, the sub-state setter, the finish routine and the
// ordered-action seam.
//
// Source: spec-ai-runtime.md Sec2 (the mode table, Sec2.1-Sec2.5), Sec3
// (request Sec3.1, apply Sec3.2, transition Sec3.3, acceptance Sec3.4),
// Sec4 (finish Sec4.1, hand-off Sec4.3 - corrected by Sec9.2, the
// ordered-action seam Sec4.4) and Sec6 (the full 44-row table, cited here
// as kModeTable). Every behaviour below is CONFIRMED in that text unless a
// comment says otherwise. Addresses in comments are copied from the spec so
// a reader can cross-check the section; nothing here was derived from the
// executable (clean-room: see CLAUDE.md / spec Sec12).
//
// WHAT THIS UNIT OWNS: the per-human mode bytes (+0x500..+0x504, +0x317 bits
// 3/4), the request/apply/transition/acceptance machinery, the event list
// and pump (Sec2.3), the sub-state setter (Sec2.5), the finish routine
// (Sec4.1) and the ordered-action commit/try-start/cancel seam (Sec4.4).
//
// WHAT THIS UNIT DOES NOT OWN: each mode's own internal behaviour (what
// Combat Movement's tick actually does, how Idle's begin function picks a
// point, ...). That is "mode content" - a different, much larger spec this
// project has not been asked to write. Per-mode enter/exit/tick/event hooks
// are therefore plain callbacks (ModeHooks), defaulted to no-ops, which a
// host registers when (if) it implements a mode's content. Five begin
// functions were detailed beyond their call signature by the original A0
// pass (Idle, Player, Stunned, Human shield, Panic - Sec3.2's "Begin
// functions observed in this pass"); unit A5 (Sec13.1) has now resolved the
// internal gating of the remaining 7 of the 13 begin-group modes (Cower,
// Flee - both bodies, Gunfire wary, Passenger, RollerBlader, Veh evade,
// Veh threat's own call), so all 13 begin-group modes are implemented for
// real below (Dbno's begin was already resolved via the Veh-threat
// fall-through paragraph, Sec3.2). The BeginFunctionHook mechanism is kept
// as a defensive fallback (and a seam for any future spec unit that adds a
// 14th begin-group mode via kModeTable), but every mode id currently in the
// Begin apply-group now has a dedicated, spec-cited implementation, so that
// fallback's OpenStateError is not reachable from any real mode id today.

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "sr3airuntime/common.h"
#include "sr3luahost/open_state.h"

namespace sr3airuntime {

using sr3luahost::OpenStateError;
using sr3luahost::OpenValue;

// --- mode ids (Sec6's row order; the engine's own strings, quoted as data) --------
namespace mode {
inline constexpr int32_t Idle = 0;
inline constexpr int32_t Aim = 1;
inline constexpr int32_t BruteStomp = 2;
inline constexpr int32_t BruteMelee = 3;
inline constexpr int32_t Bust = 4;
inline constexpr int32_t CombatMovement = 5;
inline constexpr int32_t Commando = 6;
inline constexpr int32_t CoverPopout = 7;
inline constexpr int32_t CoverStay = 8;
inline constexpr int32_t Cower = 9;
inline constexpr int32_t Dbno = 10;
inline constexpr int32_t Dog = 11;
inline constexpr int32_t FireSpray = 12;
inline constexpr int32_t Flee = 13;
inline constexpr int32_t Follow = 14;
inline constexpr int32_t GrenadeThrow = 15;
inline constexpr int32_t GunfireEvade = 16;
inline constexpr int32_t GunfireWary = 17;
inline constexpr int32_t Hijack = 18;
inline constexpr int32_t Holdup = 19;
inline constexpr int32_t Hostage = 20;
inline constexpr int32_t HumanShield = 21;
inline constexpr int32_t Investigate = 22;
inline constexpr int32_t Melee = 23;
inline constexpr int32_t Panic = 24;
inline constexpr int32_t Passenger = 25;
inline constexpr int32_t PickupItem = 26;
inline constexpr int32_t Reload = 27;
inline constexpr int32_t ReturnToShore = 28;
inline constexpr int32_t RiotCop = 29;
inline constexpr int32_t RollerBlader = 30;
inline constexpr int32_t Skydiving = 31;
inline constexpr int32_t Squirted = 32;
inline constexpr int32_t Streaking = 33;
inline constexpr int32_t Stunned = 34;
inline constexpr int32_t Taunt = 35;
inline constexpr int32_t WeaponThrow = 36;
inline constexpr int32_t VehClearDoor = 37;
inline constexpr int32_t VehEnter = 38;
inline constexpr int32_t VehEvade = 39;
inline constexpr int32_t VehThreat = 40;
inline constexpr int32_t VehTrans = 41;
inline constexpr int32_t ZombieEat = 42;
inline constexpr int32_t Player = 43;
inline constexpr int32_t kCount = 44;            // Sec2.1: the setter rejects any index >= 0x2c
inline constexpr int32_t kNone = 0x2d;           // 45: "no mode" (Sec2.1)
inline constexpr int32_t kSubStateNone = 0xb2;   // sub-state "none" (Sec2.5, Sec3.3 step 2)
}  // namespace mode

// --- the 44-row static table (Sec6) ----------------------------------------------
// ApplyGroup is the group the apply step (Sec3.2) puts a requested mode in.
enum class ApplyGroup : uint8_t { Generic, Begin, Ignored };
// The event-handler slot's disposition on a call with no stimulus to react
// to (Sec2.2's three constant-return stubs; everything else is a real
// function whose return is one of the four disposition codes of Sec2.3).
enum class EventSlotKind : uint8_t { Real, ConstReturn1, ConstReturn3, ConstReturn4 };
// Sec6's own three-state columns for +0x14/+0x18/+0x1c: null, the shared
// no-op stub 0x00754410, or a real function (this project does not model
// mode-internal net/init-params content, so "real" and "the shared act-net
// pair 0x004f3eb0/0x004f5440" collapse to the same SlotFill::Real - what
// matters for the A0 contract is only presence/absence, per Sec2.1's "every
// slot column has exactly one reader", none of which this unit implements).
enum class SlotFill : uint8_t { Null, Nop, Real };

struct ModeRow {
    const char* name;      // the engine's own string (Sec2.1, Sec6)
    bool cae;               // "CAE" column: reachable via an action's category byte (Sec6, Sec9.5)
    ApplyGroup applyGroup;   // Sec3.2's three-way dispatch
    EventSlotKind eventSlot; // Sec2.2 / Sec2.3
    SlotFill initParams;     // +0x14 (Sec2.1)
    SlotFill netWrite;       // +0x18
    SlotFill netRead;        // +0x1c
    bool remoteRequest;      // +0x20 present? (Sec2.1; never "nop" per Sec6's fill table)
};

// Transcribed from spec-ai-runtime.md Sec6's table verbatim (names, CAE,
// Apply group, event-slot kind, +0x14/+0x18/+0x1c/+0x20 presence). Verified
// against Sec6's own "Fill counts" table (14/11/19 null/nop/real for +0x14,
// 3/10/31 for +0x18, 3/4/37 for +0x1c, 28/0/16 null/nop/real for +0x20, and
// 5/1/1 of the three constant event stubs) while building this table: every
// column total matches.
inline constexpr std::array<ModeRow, mode::kCount> kModeTable = {{
    /*0  Idle            */ {"Idle",             true,  ApplyGroup::Begin,    EventSlotKind::Real,         SlotFill::Real, SlotFill::Real, SlotFill::Real, true},
    /*1  Aim             */ {"Aim",              false, ApplyGroup::Ignored,  EventSlotKind::ConstReturn1, SlotFill::Real, SlotFill::Real, SlotFill::Real, true},
    /*2  Brute Stomp     */ {"Brute Stomp",      false, ApplyGroup::Ignored,  EventSlotKind::Real,         SlotFill::Null, SlotFill::Null, SlotFill::Null, false},
    /*3  Brute Melee     */ {"Brute Melee",      true,  ApplyGroup::Generic,  EventSlotKind::Real,         SlotFill::Null, SlotFill::Null, SlotFill::Null, false},
    /*4  Bust            */ {"Bust",             false, ApplyGroup::Ignored,  EventSlotKind::Real,         SlotFill::Nop,  SlotFill::Nop,  SlotFill::Real, true},
    /*5  Combat Movement */ {"Combat Movement",  true,  ApplyGroup::Generic,  EventSlotKind::Real,         SlotFill::Null, SlotFill::Real, SlotFill::Real, false},
    /*6  Commando        */ {"Commando",         true,  ApplyGroup::Generic,  EventSlotKind::Real,         SlotFill::Nop,  SlotFill::Real, SlotFill::Real, false},
    /*7  Cover popout    */ {"Cover popout",     true,  ApplyGroup::Generic,  EventSlotKind::Real,         SlotFill::Null, SlotFill::Real, SlotFill::Real, false},
    /*8  Cover stay      */ {"Cover stay",       true,  ApplyGroup::Generic,  EventSlotKind::Real,         SlotFill::Null, SlotFill::Real, SlotFill::Real, false},
    /*9  Cower           */ {"Cower",            false, ApplyGroup::Begin,    EventSlotKind::Real,         SlotFill::Real, SlotFill::Real, SlotFill::Real, true},
    /*10 Dbno            */ {"Dbno",             false, ApplyGroup::Begin,    EventSlotKind::Real,         SlotFill::Nop,  SlotFill::Nop,  SlotFill::Nop,  false},
    /*11 Dog             */ {"Dog",              false, ApplyGroup::Ignored,  EventSlotKind::Real,         SlotFill::Real, SlotFill::Nop,  SlotFill::Real, false},
    /*12 Fire spray      */ {"Fire spray",       true,  ApplyGroup::Generic,  EventSlotKind::Real,         SlotFill::Nop,  SlotFill::Real, SlotFill::Real, false},
    /*13 Flee            */ {"Flee",             false, ApplyGroup::Begin,    EventSlotKind::Real,         SlotFill::Real, SlotFill::Real, SlotFill::Real, true},
    /*14 Follow          */ {"Follow",           true,  ApplyGroup::Generic,  EventSlotKind::Real,         SlotFill::Null, SlotFill::Real, SlotFill::Real, false},
    /*15 Grenade throw   */ {"Grenade throw",    true,  ApplyGroup::Generic,  EventSlotKind::Real,         SlotFill::Nop,  SlotFill::Real, SlotFill::Real, false},
    /*16 Gunfire evade   */ {"Gunfire evade",    false, ApplyGroup::Ignored,  EventSlotKind::Real,         SlotFill::Real, SlotFill::Real, SlotFill::Real, true},
    /*17 Gunfire wary    */ {"Gunfire wary",     false, ApplyGroup::Begin,    EventSlotKind::Real,         SlotFill::Real, SlotFill::Real, SlotFill::Real, true},
    /*18 Hijack          */ {"Hijack",           true,  ApplyGroup::Generic,  EventSlotKind::Real,         SlotFill::Null, SlotFill::Real, SlotFill::Real, false},
    /*19 Holdup          */ {"Holdup",           false, ApplyGroup::Ignored,  EventSlotKind::Real,         SlotFill::Real, SlotFill::Real, SlotFill::Real, true},
    /*20 Hostage         */ {"Hostage",          false, ApplyGroup::Ignored,  EventSlotKind::ConstReturn1, SlotFill::Nop,  SlotFill::Nop,  SlotFill::Nop,  true},
    /*21 Human shield    */ {"Human shield",     true,  ApplyGroup::Begin,    EventSlotKind::Real,         SlotFill::Nop,  SlotFill::Nop,  SlotFill::Nop,  false},
    /*22 Investigate     */ {"Investigate",      true,  ApplyGroup::Generic,  EventSlotKind::Real,         SlotFill::Null, SlotFill::Real, SlotFill::Real, false},
    /*23 Melee           */ {"Melee",            true,  ApplyGroup::Generic,  EventSlotKind::Real,         SlotFill::Null, SlotFill::Null, SlotFill::Null, false},
    /*24 Panic           */ {"Panic",            false, ApplyGroup::Begin,    EventSlotKind::Real,         SlotFill::Real, SlotFill::Real, SlotFill::Real, true},
    /*25 Passenger       */ {"Passenger",        true,  ApplyGroup::Begin,    EventSlotKind::Real,         SlotFill::Real, SlotFill::Real, SlotFill::Real, true},
    /*26 Pickup item     */ {"Pickup item",      true,  ApplyGroup::Generic,  EventSlotKind::Real,         SlotFill::Null, SlotFill::Nop,  SlotFill::Real, true},
    /*27 Reload          */ {"Reload",           true,  ApplyGroup::Generic,  EventSlotKind::Real,         SlotFill::Nop,  SlotFill::Real, SlotFill::Real, false},
    /*28 Return to shore */ {"Return to shore",  false, ApplyGroup::Ignored,  EventSlotKind::Real,         SlotFill::Real, SlotFill::Real, SlotFill::Real, false},
    /*29 Riot cop        */ {"Riot cop",         false, ApplyGroup::Ignored,  EventSlotKind::Real,         SlotFill::Real, SlotFill::Real, SlotFill::Real, false},
    /*30 RollerBlader    */ {"RollerBlader",     true,  ApplyGroup::Begin,    EventSlotKind::Real,         SlotFill::Real, SlotFill::Nop,  SlotFill::Real, false},
    /*31 Skydiving       */ {"Skydiving",        false, ApplyGroup::Generic,  EventSlotKind::Real,         SlotFill::Nop,  SlotFill::Nop,  SlotFill::Real, false},
    /*32 Squirted        */ {"Squirted",         false, ApplyGroup::Ignored,  EventSlotKind::ConstReturn3, SlotFill::Real, SlotFill::Real, SlotFill::Real, false},
    /*33 Streaking       */ {"Streaking",        false, ApplyGroup::Ignored,  EventSlotKind::ConstReturn1, SlotFill::Real, SlotFill::Real, SlotFill::Real, true},
    /*34 Stunned         */ {"Stunned",          false, ApplyGroup::Begin,    EventSlotKind::Real,         SlotFill::Null, SlotFill::Real, SlotFill::Real, false},
    /*35 Taunt           */ {"Taunt",            true,  ApplyGroup::Generic,  EventSlotKind::Real,         SlotFill::Null, SlotFill::Real, SlotFill::Real, false},
    /*36 Weapon throw    */ {"Weapon throw",     true,  ApplyGroup::Generic,  EventSlotKind::ConstReturn4, SlotFill::Null, SlotFill::Real, SlotFill::Real, false},
    /*37 Veh clear door  */ {"Veh clear door",   false, ApplyGroup::Ignored,  EventSlotKind::ConstReturn1, SlotFill::Real, SlotFill::Real, SlotFill::Real, false},
    /*38 Veh enter       */ {"Veh enter",        false, ApplyGroup::Ignored,  EventSlotKind::Real,         SlotFill::Real, SlotFill::Real, SlotFill::Real, true},
    /*39 Veh evade       */ {"Veh evade",        false, ApplyGroup::Begin,    EventSlotKind::Real,         SlotFill::Real, SlotFill::Real, SlotFill::Real, true},
    /*40 Veh threat      */ {"Veh threat",       false, ApplyGroup::Begin,    EventSlotKind::Real,         SlotFill::Real, SlotFill::Real, SlotFill::Real, true},
    /*41 Veh trans       */ {"Veh trans",        false, ApplyGroup::Generic,  EventSlotKind::Real,         SlotFill::Nop,  SlotFill::Nop,  SlotFill::Real, false},
    /*42 Zombie eat      */ {"Zombie eat",       true,  ApplyGroup::Generic,  EventSlotKind::Real,         SlotFill::Null, SlotFill::Real, SlotFill::Real, false},
    /*43 Player          */ {"Player",           false, ApplyGroup::Begin,    EventSlotKind::ConstReturn1, SlotFill::Nop,  SlotFill::Nop,  SlotFill::Nop,  false},
}};

// The nine vehicle-compatible modes (Sec3.4 step 4, table 0x004dd410).
inline constexpr std::array<int32_t, 9> kVehicleCompatibleModes = {
    mode::GrenadeThrow, mode::Hijack, mode::Hostage, mode::Panic, mode::Passenger,
    mode::Reload, mode::Stunned, mode::VehTrans, mode::Player};
bool IsVehicleCompatibleMode(int32_t m);

// --- the 177-entry sub-state name table (Sec2.5) ---------------------------------
// Only a formatter reads names (Sec2.1's +0x00 debug printer); the runtime
// itself only needs the setter's state-gated write. Names are not
// reproduced here (not needed for the contract; would be 177 lines of pure
// citation for no behavioural value).
inline constexpr uint8_t kSubStateInvalidMin = 0xb1;  // formatter prints >= 0xb1 as INVALID (Sec2.1)

// --- the event list (Sec2.3) ------------------------------------------------------
// Each human holds a circular list of event records (+0x1c54); kind 0x77
// means "consumed" in the original encoding. This project keeps live records
// in a plain vector and drops consumed ones instead of marking-and-skipping,
// which is an equivalent outward behaviour (Sec2.3 never distinguishes a
// "consumed-but-still-linked" record from an absent one to any caller).
struct AiEvent {
    int32_t kind = 0;             // +0x08
    int64_t postedAtMs = 0;       // when 0x004f5dd0 queued it (staged, Sec10.5)
    int64_t deliverAtMs = 0;      // +0x24: timer; a negative delay -> random 250-750ms (host's job to roll)
    uint8_t modeFilter = mode::kNone;      // +0x28 (0x2d = any mode)
    uint8_t subStateFilter = 0xb2;          // +0x29 (0xb2 = any sub-state)
    uint32_t sourceHandle = 0;    // +0x10/+0x14
    uint32_t recipientHandle = 0; // +0x18/+0x1c
    bool hasPayload = false;      // +0x2a bit 1
    std::array<uint8_t, 0x20> payload{};  // +0x30
};

// Event-pump disposition codes (Sec2.3's switch on the handler's result).
enum class EventDisposition : int32_t {
    Consumed = 1,       // handled
    Rejected = 2,       // not applicable
    Kept = 3,           // re-offered later, unchanged
    KeptRearmed = 4,    // re-offered later, timer re-armed 250-750ms
};
// Any OTHER handler return value also means "consumed" (Sec2.3's table).
EventDisposition DispositionFromHandlerResult(int32_t result);

// The 0x58-byte action/committed block is common.h's ActionBlock (shared
// with A1/A3 - Sec9.2 step 5, Sec11.2). A0 only ever copies it whole (the
// template reset of Sec4.1 step 4, the staged/committed fields of Sec4.4);
// it never reads inside it.

// --- per-human mode state (A0's own fields) ---------------------------------------
struct ModeState {
    uint8_t subState = mode::kSubStateNone; // +0x500
    int32_t currentMode = mode::kNone;      // +0x501
    int32_t previousMode = mode::kNone;     // +0x502
    bool justEntered = false;               // +0x503 (who clears it is spec OPEN 1; see ModeMachine::clearJustEntered)
    int32_t requestedMode = mode::kNone;    // +0x504
    bool switchPending = false;             // +0x317 bit 3
    bool switchInProgress = false;          // +0x317 bit 4
    bool hasPendingTargetHandle = false;    // +0x508/+0x50c validity
    uint64_t pendingTargetHandle = 0;
    uint64_t ownerTargetHandle = 0;         // +0x1330, used as the apply-time fallback (Sec3.2 step 3)
    // +0x4a5/+0x4a6 bits the Cower/Flee/Veh-evade begin functions read
    // (Sec3.2's begin-function table). Named per the bits the spec actually
    // cites; the rest of +0x4a5/+0x4a6 is mode/order content, out of scope.
    bool bit4a6_1_cantFleeToggle = false;   // Sec9.9: apply's Cower case toggles cant_flee around the begin call
    bool bit4a6_2_fleeVariant = false;      // Sec3.2: Flee begin picks 0x004da0b0 vs 0x004d91a0 by this bit
    bool bit4a6_4_cowerForced = false;      // Sec3.2: "flags from +0x4a5/+0x4a6" passed to the Cower begin
    // Sec13.1.1's own push survey of apply's mode-9 call site: apply unpacks
    // 4 INDIVIDUAL bits (not the single "+0x4a5/+0x4a6" value bit4a6_4 above
    // stood in for pre-A5) and passes them as 4 separate bool arguments to
    // Cower's begin.
    bool bit4a5_5_cowerFlag5 = false;  // bit 5 of +0x4a5
    bool bit4a5_6_cowerFlag6 = false;  // bit 6 of +0x4a5
    bool bit4a5_7_cowerFlag7 = false;  // bit 7 of +0x4a5
    bool bit4a6_0_cowerFlag0 = false;  // bit 0 of +0x4a6
    uint64_t secondaryHandle458 = 0;        // +0x458: Idle's point handle, Human shield's victim, Veh evade's handle (Sec3.1 step 2, Sec3.2)
    // Sec13.1.1 step 6: the ONE confirmed sub-field of the +0x8d0 live
    // mode-parameter block this project models - Cower's own stored target
    // handle (+0x8d8/+0x8dc), updated on the "already Cower, just refresh"
    // fast path without a full re-transition. The rest of +0x8d0 stays
    // "mode content, not modelled" exactly as transition()'s own comment
    // already says.
    uint64_t cowerStoredTargetHandle = 0;
    bool hasIdleSeedPoint = false;          // Idle begin's optional seeded point (0x00512000)
    Vec3 idleSeedPoint;
    int32_t panicReason = -1;               // the reason Panic's begin derives/reads (Sec3.2); -1 = none yet
    std::vector<AiEvent> events;            // +0x1c54, the live (delivered) list
    std::vector<AiEvent> staging;           // +0x1c50, this-frame-posted records (Sec10.5)

    // +0x44c bits (Sec4.1, Sec4.4, Sec9.1-Sec9.2).
    static constexpr uint32_t kActionFinished = 0x01;      // bit 0
    static constexpr uint32_t kReEvaluateRequested = 0x02; // bit 1
    static constexpr uint32_t kOrderedCompleted = 0x04;    // bit 2
    static constexpr uint32_t kOrdered = 0x08;             // bit 3
    static constexpr uint32_t kOrderedForceRecheck = 0x10; // bit 4
    uint32_t actionFlags = 0;

    uint8_t committedReason = 0;             // +0x444
    uint8_t currentAction = 0x47;            // +0x445 (0x47 = none)
    EngineTimer commitTimer;                 // +0x438 (Sec9.1)
    EngineTimer idleTimer;                   // +0x43c (Sec4.1, Sec9.1, Sec9.2)
    EngineTimer orderedRecheckTimer;         // +0x440 (Sec9.4 step 3, Sec4.4)
    ActionBlock committedBlock;              // +0x450
    ActionBlock stagedBlock;                 // +0x4a8
    uint32_t stagedRequestId = 0;            // +0x4aa (kind-3 id)
    bool stagedRequestIdValid = false;
};

// --- acceptance test (Sec3.4) -----------------------------------------------------
// Inputs the acceptance test needs that this unit does not itself own
// (health/vehicle state); provided per-call rather than modelled as world
// state this unit tracks, since Sec3.4's own text treats them as externally
// read facts (+0xcc8, in-vehicle tests).
struct AcceptanceInputs {
    bool capturedOrLocked = false;  // 0x0060b530(human) (Sec3.4 step 2)
    int32_t healthState = 0;        // +0xcc8 (Sec3.4 step 3): 0/7 normal-ish, 3 water, 5/6 down, 7 "shielded"
    bool inVehicle = false;         // 0x009b9160 or 0x009b91e0 (Sec3.4 step 4)
    bool stunnedLockForceFlagSet = false;  // +0x4a4 bit 0 (Sec3.4 step 1's "only when set")
};
// 0x004de110(human, mode) -> bool. CONFIRMED order (Sec3.4).
bool AcceptanceTest(const ModeState& st, int32_t requestedMode, const AcceptanceInputs& in);

// --- the mode machine --------------------------------------------------------------
// Per-mode content hooks. Defaults are no-ops / "no override" so the A0
// contract (ordering, dispatch, acceptance) is fully exercised even with no
// mode content registered - exactly what this unit's own tests need.
struct ModeHooks {
    std::function<void(int32_t newMode)> onExit;   // +0x08: exit(human, new_mode)
    std::function<void()> onEnter;                 // +0x04: enter(human)
    std::function<void()> onTick;                  // +0x0c: tick(human)
    // Event handler: returns a disposition per Sec2.3's switch, or
    // EventDisposition(0)/unset to mean "fall through to the default
    // handler" (modelled as returning Rejected here, since this project
    // does not implement the default reactive-mode dispatcher 0x004ebc70 -
    // Sec8 item 3 - it is mode/reaction CONTENT, out of this unit's scope).
    std::function<EventDisposition(AiEvent&)> onEvent;
};

// A begin function for a mode id not (yet) given a dedicated
// ModeMachine::run*Begin implementation. Every one of the 13 begin-group
// mode ids in kModeTable has such an implementation as of Sec13.1 (A5), so
// this hook is currently a defensive fallback only (a seam for a future
// spec unit adding a 14th begin-group mode), not a live gap. Returns true
// if it entered its mode (possibly a DIFFERENT one, as several begin
// functions do - Sec3.3). Defaults to nullptr, in which case
// ModeMachine::apply() throws OpenStateError citing Sec3.2 - the
// "skip and revisit" the project's standing rules ask for, rather than
// guessing preconditions no text gives.
using BeginFunctionHook = std::function<bool()>;

// Inputs the spec-detailed begin functions need from outside this unit
// (object/type-descriptor/world state this unit does not own - Sec1.2,
// Sec3.2, Sec3.3, and now Sec13.1/A5 for the 7 begins that unit resolved).
// Refreshed by the host before apply(). Fields are grouped by which begin(s)
// read them; several fields are shared verbatim because the spec cites the
// SAME helper+arguments from more than one begin's listing (noted per field).
struct BeginFunctionInputs {
    bool isLocallyOwned = true;
    bool humanUnusable = false;             // 0x00853b10 (Idle begin)
    bool isPlayer = false;                  // 0x00853b30(human, 0x21) (Idle/Player; also Gunfire wary step2, Passenger step2 - Sec13.1.3/13.1.4 cite the identical test)
    bool isDown = false;                    // 0x009715d0(human) (Dbno / Veh-threat fall-through)
    bool vehicleReentrySucceeded = false;   // 0x00521640/0x00521700 (Idle begin; outcome not detailed)
    bool hasPoint = false;                  // Idle begin's own has_point argument
    Vec3 point;
    bool humanShieldVictimValid = true;     // 0x009a9830 (Human shield begin)
    uint64_t humanShieldVictimHandle = 0;
    bool panicIsLocalPlayer = false;
    bool panicDerivedReasonProvided = false;  // Panic begin's non-local-player path ("several tests", Sec3.2) - not derived here
    int32_t panicDerivedReason = 0;

    // --- Sec13.1.0's shared preamble (all 7 of A5's begins) -----------------
    // The generic "this AI slot's type can run a scripted mode entry" class
    // gate (type-descriptor table 0x02cc9900, confirmed elsewhere in this
    // spec as runtime-populated / not file-backed - Sec13.2.1's own oracle
    // run). Not modelled in detail; default true (an ordinary human passes
    // it), matching this project's convention for similarly-unmodelled
    // low-level class checks (e.g. beginInputs.isPlayer's own default use
    // elsewhere).
    bool classAllowsScriptedModeEntry = true;

    // --- Facts shared verbatim across two or more of the 7 (same helper AND
    // same arguments at both cited call sites - Sec13.1's own cross-refs) --
    bool helper0x00a00560 = false;          // 0x00a00560(human): Cower step5 (must HOLD), Veh evade step2 (DECLINES when it holds) - opposite polarity, same raw fact
    bool flagE8Bit0x200 = false;            // [human+0xe8] bit 0x200: Cower step7, Flee's 0x004da0b0 idle-decline bullet, Veh evade step4
    bool targetReferenceResolves = false;   // 0x004d4c70(target_lo, target_hi, 0): Cower step4 (bool only), Flee step4 (bool + point below)
    Vec3 targetReferencePoint;              // the resolved point when targetReferenceResolves (Flee only; Cower never reads the point itself)
    bool pairedGate0x008c01b0 = false;       // 0x008c01b0(human): Gunfire wary's 0x004dbd90 step3, Veh evade step2 - "if true, run the paired handler, then decline unconditionally"
    std::function<void()> pairedHandler0x008c0240;  // the paired handler's side effect (host-settable; default no-op - its content is mode/reaction CONTENT, Sec13.1.3)
    bool enteringVehicle0x009b9210 = false;  // 0x009b9210(human) ("entering a vehicle" sub-state selector, Sec2.5): Veh evade step2, Veh threat step2

    // Mirrors of specific named ForceFlags bits (decision.h/A1's own 41-flag
    // table) that these begins read directly. Kept as plain bools here
    // (not a ForceFlags reference) because A0 (this header) does not and
    // should not depend on A1's decision.h; a host that also owns a
    // HumanAiState copies the real bit across before calling apply().
    bool forceAlwaysCower = false;    // +0x2b9 bit 7 (ForceFlags::alwaysCower): Cower step4, Gunfire wary's 0x004e8070 non-player branch
    bool forceCantFlee = false;       // +0x2ba bit 5 (ForceFlags::cantFlee): Cower step5's gate-skip
    bool forceIgnoreAttacks = false;  // +0x2b9 bit 3 (ForceFlags::ignoreAttacks): Gunfire wary's 0x004dbd90 step3
    bool forceCombatDisabled = false; // +0x2b8 bit 4 (ForceFlags::combatDisabled): Gunfire wary's 0x004dbd90 step3

    // --- Sec13.1.1: Cower begin 0x004d5050 ----------------------------------
    bool cowerUsable = true;               // 0x004eaa40(human) step3; HYPOTHESIS on meaning ("is usable/not mid-cleanup"), CONFIRMED as a gate
    bool cowerCanApproachTarget = false;   // 0x004d8ff0(human, target_lo, target_hi, 0) step5 (literal-0 4th arg; Flee's own variant is separate below)
    // OPEN item 1 (Sec13.1.1 step7): bit 0 of the real-transition flags byte
    // comes from a helper 0x0097e9b0(...) whose own ARGUMENT SHAPE "could
    // not be pinned down with confidence" by A5's own text - not even the
    // call signature is confirmed, so this is an OpenValue (house
    // convention, open_state.h) rather than an invented bit.
    OpenValue<bool> cowerFlagsByteBit0{"Cower begin 0x004d5050 flags-byte bit 0 (0x0097e9b0 result)",
                                        "spec-ai-runtime.md Sec13.1.1 step 7, OPEN item 1"};

    // --- Sec13.1.2: Flee - both bodies (0x004d91a0, 0x004da0b0) -------------
    bool fleeHasEscapePoint = false;  // the function's own 3rd arg ("escape_point_or_null"): apply's bit-set call resolves it via 0x00da38e0 (out of scope); the candidate search below supplies the current candidate instead
    Vec3 fleeEscapePoint;
    uint8_t fleeField4a0 = 0;         // the function's own opaque 4th arg; its SOURCE at apply's call site is not given by this unit (seam) - only the bits the listing itself tests (3, 6, and the |0x20 literal) are used
    bool fleeCanApproachTarget = false;  // 0x004d8ff0(human, target_lo, target_hi, field_0x4a0): read identically by BOTH 0x004d91a0's own step3 and 0x004da0b0's preamble ("as above")
    bool fleeAlreadyFleeingSameTargetArmed = false;  // 0x004da0b0 step1: (target handle already matches, or 0x00456920 says equivalent) AND +0x8d0 bit5 set - combined into one fact (equivalence helper + unmodelled +0x8d0 bit, Sec13.1.2 step1)
    bool fleeTooExposedToBreakCover = false;  // 0x004d5d20(human, ref.x, ref.z): used identically by 0x004d91a0 step5 and 0x004da0b0 step2, HYPOTHESIS "still too exposed/close to break cover"
    // Candidate search (0x004da0b0 step3). The near-cover probe's own
    // CONFIRMED bits ([human+0x1e00] bit7, [human+0x1da8]!=0) are combined
    // into one fact; field_0x4a0 bit6 is tested directly from fleeField4a0
    // above. "A failing ratio here is what triggers the Cower-redirect...
    // the near-cover probe and the Cower-redirect are the same branch" -
    // modelled as: the gate below being true AND the probe failing is what
    // gates an attempt at the Cower redirect (a JUDGMENT CALL on the exact
    // interleaving the prose describes but does not lay out step-by-step;
    // see ai_mode_table.cpp's own citation at the call site).
    bool fleeNearCoverGateBits = false;      // [human+0x1e00] bit7 AND [human+0x1da8] != 0
    bool fleeNearCoverProbeFails = false;    // 0x00bc0800 vs 1.0 (true = ratio failed, i.e. not yet near enough cover)
    bool fleeNormalizedDirectionValid = false;  // 0x00bbfbe0 + 0x00dab810 + 0x004d4bf0
    Vec3 fleeNormalizedDirectionPoint;
    bool fleePathSearchSucceeded = false;    // 0x004d7e20/0x00b71950, elapsed-time-budgeted via 0x00dab5e0/0x00d9e250; on success this candidate builds its OWN point and transitions directly (no outparam needed here)
    bool fleeIdleInsufficientTimePassed = false;  // the Idle(0)-only decline bullet's own elapsed-time test (placement within the search is a JUDGMENT CALL - see the .cpp citation)

    // --- Sec13.1.3: Gunfire wary - 0x004e8070 then 0x004dbd90 --------------
    bool gunfireWaryPrehookInVehicle = false;  // 0x004e8070's non-player branch, "in a vehicle" test -> requests Passenger instead of Cower
    bool gunfireWaryTargetValid = false;       // 0x004dbd90's own null/self checks: a distinct, non-null target object resolved
    bool gunfireWaryIsFollower = false;        // 0x0097e280(human) (0x004dbd90 step3)
    // 0x004dbd90 step3's own "0x008c01b0(human) holds -> run paired handler,
    // still decline" reuses the shared pairedGate0x008c01b0/
    // pairedHandler0x008c0240 facts above (same helper pair Veh evade's own
    // gate reads).

    // --- Sec13.1.4: Passenger begin 0x0051ed00 ------------------------------
    bool singlePlayerSessionFlag = false;  // 0x00889d10(): the second "not owned" escape hatch (step3); HYPOTHESIS "single-player session"
    bool passengerSeatResolves = false;    // 0x004dcf00([human+0x16c0], [human+0x16c4], 0) (step4)

    // --- Sec13.1.5: RollerBlader begin 0x00530530 ---------------------------
    bool rollerBladerDescriptorBit = false;  // [human+0xf0]+0x48 bit6 (step2) - the exact gate action 68's own routine documents (actions.h's ActionWorldFacts::rollerBladerDescriptorBit is A3's separate copy of the same engine bit)

    // --- Sec13.1.6: Veh evade begin 0x005232a0 ------------------------------
    bool vehEvadeTargetResolves = false;      // 0x004dcf00(lo, hi, 0)
    bool flagE8Bit0x1000 = false;             // [human+0xe8] bit 0x1000
    bool isBruteSubtype0x009422d0 = false;    // 0x009422d0(human)
    bool helper0x0097e9b0 = false;            // 0x0097e9b0(human): VehEvade's OWN usage has a clearly-confirmed single-arg call shape (unlike Cower's OPEN item1 above), so this is a plain fact, not an OpenValue
    bool vehEvadeGameModeSwitchIsTwo = false; // 0x00614d00() == 2
    bool vehEvadeNavmeshQuerySucceeds = false; // 0x00a12180(&outbuf, 0x0130c230, [human+0xb40], ...) (step3)
    bool vehEvadeIdleTimerElapsed = false;    // tunable 0x0114a1560's elapsed-time check, read only when current mode is Idle (step4)

    // --- Sec13.1.7: Veh threat's own call 0x00523db0 ------------------------
    // OPEN item 3: the virtual-dispatch "don't flag vehicle-threat on
    // whatever the camera/possession system currently focuses" chain
    // (0x00614cf0/0x00680db0/[+0x1c]/[+0x40], two vtable calls) is "skipped
    // entirely (non-blocking) unless every link resolves." This project has
    // no modelled camera/possession subsystem at all, so every link in that
    // chain is, by construction, never populated here - the chain simply
    // never resolves in this port, which is this port's own faithful state,
    // not a guess at ENGINE behaviour. Exposed as a host override (default
    // false = never declines via this path) only so a future host that DOES
    // model such a subsystem has a seam to plug into.
    bool vehThreatPossessionGateDeclines = false;
    bool vehThreatTargetResolves = false;      // 0x004e4ce0(target_lo, target_hi, 1) (step4)
    bool vehThreatRelevancePredicate = false;  // 0x004e7200(human, target) (step5)
    bool vehThreatHelper0x004e9110 = false;    // 0x004e9110(human) (step6, arg4 != 0 branch - the only one apply's call site exercises)
    bool vehThreatHelper0x009d8ca0 = false;    // 0x009d8ca0(human, target) thiscall (step6)
};

class ModeMachine {
public:
    ModeState state;
    std::array<ModeHooks, mode::kCount> hooks;
    std::array<BeginFunctionHook, mode::kCount> beginHooks;  // defensive fallback only; see BeginFunctionHook's own comment - no mode id currently in kModeTable's Begin group reaches this
    AcceptanceInputs acceptance;        // refreshed by the host before request()/apply()
    BeginFunctionInputs beginInputs;    // refreshed by the host before apply()
    // Sec13.1.1's apply-side bracket (context, not inside Cower's own begin):
    // when +0x4a6 bit1 (state.bit4a6_1_cantFleeToggle) is set, apply calls
    // 0x004e0870(&human+0x2b0, 0) immediately BEFORE Cower's begin and
    // 0x004e0870(&human+0x2b0, 1) immediately AFTER it returns.
    // HYPOTHESIS, not resolved further by A5 ("flagged OPEN"): a
    // pause/resume pair. Exposed as a host hook (default no-op, since this
    // project does not know what the pause/resume actually does) rather
    // than invented behaviour; called with isResume=false before, true
    // after.
    std::function<void(bool isResume)> cowerPauseResumeHook;
    // The engine global 0x012e1b84 (Sec3.2's Panic begin): modelled per
    // ModeMachine instance (one per human) as a pragmatic approximation,
    // since only the local player's own Panic begin ever reads/resets it.
    int32_t panicSavedGlobalReason = -1;
    // 0x00d9e180(lo, hi): the engine's own RNG, not specified by this spec
    // beyond its range (Sec2.3's event re-arm, Sec10.5's post delay). Hosts
    // that need real randomness inject it here; the default is the
    // deterministic midpoint, which is NOT a recovered engine value - it is
    // a test-friendly placeholder, documented as such.
    std::function<int64_t(int64_t, int64_t)> randomRangeMs =
        [](int64_t lo, int64_t hi) { return (lo + hi) / 2; };

    // --- Sec3.1: request(human, mode) -> bool, CONFIRMED order -------------------
    bool request(int32_t requestedMode, int64_t nowMs);

    // --- Sec3.2: apply(human), CONFIRMED order, the three-group dispatch ---------
    // The begin-function group additionally consults beginInputs (above);
    // the pending target handle itself (Sec3.2 step 3) is tracked in
    // ModeState but never resolved to an object by this unit.
    void apply(int64_t nowMs);

    // --- Sec3.3: the transition primitive 0x004dd380(human, new_mode, block) -----
    // hasBlock/block mirrors "a parameter block was passed, or null" (step 3);
    // with hasBlock false the live block (not modelled in detail; see
    // ModeState's comment) keeps its old contents, i.e. this is a no-op here.
    void transition(int32_t newMode, bool hasBlock, int64_t nowMs);

    // --- Sec2.5: the sub-state setter 0x004dd2d0(human, expected_mode, sub_state) -
    // Returns whether the write happened (expected_mode matched).
    bool setSubState(int32_t expectedMode, uint8_t subState);
    // Who clears +0x503 (just-entered) is spec OPEN (Sec8 item 1: HYPOTHESIS
    // "the enter slot or the first tick"). Not called automatically by
    // transition(); the host calls it per whichever hypothesis it adopts.
    void clearJustEntered() { state.justEntered = false; }

    // --- Sec2.3: the event pump 0x004f5b90(human, flagOnly) -----------------------
    // flagOnly=true mirrors the three "flag only" call sites (Sec1.4): sets
    // +0x2a bit 0 only, delivers nothing. The default handler
    // (0x004ebc70, Sec8 item 3) is mode-reaction CONTENT this unit does not
    // implement; a handler-less event (no mode slot, or the slot returned 0)
    // is therefore marked Rejected rather than guessing a reaction.
    void pumpEvents(bool flagOnly, int64_t nowMs);
    // 0x004f5dd0: stage an event for (eventual) delivery (Sec10.5). delayMs
    // is a duration from now; a negative value means "random 250-750ms"
    // (rolled via randomRangeMs above).
    void postEvent(AiEvent ev, int64_t delayMs, int64_t nowMs);
    // 0x004f5ae0: move staged records to the live list, once per frame
    // (Sec10.5). Must run before pumpEvents() sees a same-frame post.
    void deliverStagedEvents();

    // --- Sec4.1: the finish routine 0x004f4f50(human) -----------------------------
    // The per-action cool-down roll (step 1: Repeat_min/Repeat_max,
    // Sec9.7) is NOT here - it reads the behaviour record and the
    // per-action repeat-timer array, both A1's (decision.h's HumanAiState).
    // A1 wraps this call and performs that roll itself; this function only
    // touches the mode-state fields A0 owns (+0x44c, +0x43c, +0x445/+0x450,
    // +0x4a8/+0x4aa).
    struct FinishInputs {
        int32_t stagedActionCategoryMode = mode::kNone;  // action table lookup, A1's job
    };
    // Returns true if the ordered-action completion step ran (Sec4.1 step 3),
    // so the host can run its own completion side effects (complete the
    // kind-3 id, wake a waiting script thread - both outside this unit).
    bool finishAction(const FinishInputs& in, int64_t nowMs);

    // --- Sec4.4: the ordered-action seam (kind-3 id pool) -------------------------
    // The request pool itself (0x006f62b0/0x004f4d00/0x004f4b90) is
    // spec-lua-api-behaviour.md's, not re-specified here (Sec4.4's own
    // citation). This project has no Lua/mission layer to drive it (out of
    // scope per this task), so OrderIdPool below is a minimal, self-
    // contained stand-in: it reproduces only the observable contract this
    // unit's tests need (alloc/complete/cancel an id), not the real pool's
    // network forwarding.
    class OrderIdPool {
    public:
        uint32_t allocate();
        void complete(uint32_t id);
        void cancel(uint32_t id);
        bool isOpen(uint32_t id) const;

    private:
        uint32_t next_ = 1;
        std::vector<uint32_t> open_;
    };

    // 0x004f4d00(human, action, no_id): commit an externally ordered action.
    void commitOrder(uint8_t actionId, int32_t categoryMode, const ActionBlock& block,
                      bool noId, OrderIdPool& pool, int64_t nowMs);
    // 0x004f4c90(human): cancel the staged order.
    void cancelOrder(OrderIdPool& pool);

private:
    void clearPendingTargetHandle();
    // Dispatches to one of the thirteen begin functions this spec details
    // (the original five plus Dbno via the Veh-threat fall-through, plus
    // the seven Sec13.1/A5 resolved) or, for any OTHER mode id the Begin
    // apply-group might one day contain, to beginHooks[m] - throwing
    // OpenStateError (Sec3.2) if no hook was registered, per this project's
    // no-invented-fixes rule.
    bool runBeginFunction(int32_t m, int64_t nowMs);
    bool runIdleBegin(int64_t nowMs);
    bool runPlayerBegin(int64_t nowMs);
    bool runStunnedBegin(int64_t nowMs);
    bool runHumanShieldBegin(int64_t nowMs);
    bool runPanicBegin(int64_t nowMs);
    bool runDbnoBegin(int64_t nowMs);

    // --- Sec13.1 / unit A5: the 7 begin functions resolved by this spec unit ---
    // Cower (Sec13.1.1). runCowerBeginCore takes the 4 unpacked flag
    // arguments explicitly because Flee's own redirect (Sec13.1.2 step2)
    // calls Cower's begin a SECOND time with all 4 forced to 0, bypassing
    // apply's own state-bit unpack - runCowerBegin() (the normal
    // apply-group dispatch target) is a thin wrapper that reads the 4 bits
    // from state and forwards to the core.
    bool runCowerBegin(int64_t nowMs);
    bool runCowerBeginCore(bool flag5, bool flag6, bool flag7, bool flag0, int64_t nowMs);

    // Flee (Sec13.1.2): 0x004d91a0 ("has a point") and 0x004da0b0 (the
    // candidate-search dispatcher that calls 0x004d91a0 internally, and
    // also apply's own selector target when +0x4a6 bit2 is clear).
    bool runFleeBegin(int64_t nowMs);
    bool runFleeWithPoint(bool hasPoint, const Vec3& point, uint8_t field4a0, int64_t nowMs);
    bool runFleeCandidateSearch(int64_t nowMs);

    // Gunfire wary (Sec13.1.3): 0x004e8070 (never itself transitions; a
    // reaction pre-hook reused by Passenger's own gate too) then, only if a
    // target resolved, 0x004dbd90 (the real mode-17 entry).
    void runGunfireWaryPrehook(int64_t nowMs);
    bool runGunfireWaryEntry(int64_t nowMs);
    bool runGunfireWaryBegin(int64_t nowMs);

    bool runPassengerBegin(int64_t nowMs);       // Sec13.1.4, 0x0051ed00
    bool runRollerBladerBegin(int64_t nowMs);    // Sec13.1.5, 0x00530530
    bool runVehEvadeBegin(int64_t nowMs);        // Sec13.1.6, 0x005232a0
    bool runVehThreatOwnBegin(int64_t nowMs);    // Sec13.1.7, 0x00523db0 (apply always ALSO runs Dbno's begin afterward - unchanged from pre-A5)
};

}  // namespace sr3airuntime
