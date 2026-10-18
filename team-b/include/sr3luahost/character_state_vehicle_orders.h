// CharacterStateVehicleOrders: one part of CharacterState (character_state.h).
//
// Split out of character_state.h (Team C refactor, 2026-10-17); every line
// below moved verbatim. The doc-comment conventions of character_state.h's
// own top note apply ("this header" = the engine-state header set).
#pragma once

#include "sr3luahost/character_state_records.h"

namespace sr3luahost {

// Part of CharacterState: the core dispatchers (vehicle exit/enter requests, move/teleport, damage records), AI mode, ranking tranche 05, invulnerability, fake revival, human shields, player controls.
struct CharacterStateVehicleOrders {
    // --- Wave 2 batch W2-1, core dispatchers (2026-10-08): vehicle_exit_do,
    // vehicle_enter_do, move_to_do, teleport_to_object, vehicle_enter_
    // check_done, vehicle_pathfind_navmesh_do, character_damage, qte_start,
    // vehicle_exit_check_done, vehicle_enter_group_check_done, vehicle_enter_
    // group_do, vehicle_enter_guardian_angel, vehicle_exit_guardian_angel
    // (spec-lua-api-behaviour.md Sec20.9/20.32/21.11/21.22/22.1/22.3/22.4/
    // 24.9/38.1; spec-player-runtime.md Sec6/Sec7). See
    // lua_spec_confirmed_stubs_b15_wave2_core_dispatchers.cpp for every
    // stub's own citation. Placed here, next to the existing vehicle-
    // occupancy fields above (stateEnum/currentVehicleName/isAlive), which
    // these natives REUSE rather than duplicate. ---------------------------

    // The exit request record (spec-player-runtime.md Sec6.2: 0x33 bytes,
    // constructor 0x004dccd0, copied into the occupancy slot at +0xa0 by the
    // exit request 0x00af3cd0) - ONLY the fields this batch's two exit
    // natives write are kept, each defaulting to the constructor's own
    // CONFIRMED reset value. This project does not drive the occupancy state
    // machine (spec-player-runtime.md Sec7, sr3player's VehicleOccupancy) from
    // the Lua host, so the record is the call's observable output here, not
    // an input to a simulated exit. This comment originally continued "...
    // so +0x16d4 (stateEnum) is deliberately NOT advanced" - stale even
    // before 2026-10-10's own instant-release fix: stub_vehicle_exit_do's and
    // stub_vehicle_exit_guardian_angel's seated branches have written +0x16d4
    // = 2 synchronously since 2026-10-08, and the instant path now completes
    // to 0 (released) too - see stub_vehicle_exit_do's own doc comment, right
    // after its `c.stateEnum.set(2);` line, for the current, full account.
    struct VehicleExitRequestRecord {
        bool instant = false;             // +0x30 bit 0 - vehicle_exit_do arg 2 (Sec6.2/Sec6.3.3)
        bool flag30Bit4 = false;          // +0x30 bit 4 - vehicle_exit_do arg 4; reader U6.7, role OPEN
        int32_t animationOverrideId = -1; // +0x2c - vehicle_exit_do arg 5 via 0x004bf810; -1 = none (constructor)
        bool flag31Bit4 = false;          // +0x31 bit 4 - set by 0x00a66c70 = vehicle_exit_guardian_angel (Sec6.3.3)
        bool replicated = true;           // +0x32 bit 1 - constructor default 1 (Sec6.2)
    };
    std::optional<VehicleExitRequestRecord> lastVehicleExitRequest;
    uint64_t vehicleExitRequestCount = 0; // 0x00af3cd0 calls issued by this batch's natives
    uint64_t vehicleExitCancelCount = 0;  // 0x00af3e10 calls (vehicle_exit_do's cancel path)

    // 2026-10-09, spec-player-runtime.md Sec7.4a: the eligibility gate
    // 0x00b0ca60(character, forced, eligibility_bit) itself - Team A traced
    // it exhaustively and confirmed it is a PURE character-state check, no
    // vehicle/physics argument at all (9 refusal paths + 1 success path,
    // plain boolean, no secondary reason code). The three fields below are
    // new per-character facts this gate reads that nothing in this project
    // modelled before; every other field it reads (stateEnum +0x16d4,
    // ragdollStateEnumCd2 +0xcd2, lifeState/killFinalizedFlagE3 +0xcc8/+0xe3,
    // movementStateByteCcd +0xccd, stateCodeByte501 +0x501 below) is REUSED,
    // not duplicated - see vehicleEnterEligibilityGate() in
    // lua_spec_confirmed_stubs_b15_wave2_core_dispatchers.cpp for the full
    // step-by-step implementation and citation of each reuse.
    //
    // Step 3's +0x16d8/+0x16dc "busy/pending" pair (HYPOTHESIS, flagged OPEN
    // by Sec7.3/Sec13.0/Sec13.1 step 1 and Sec7.4a step 3 alike - a SECOND
    // confirmed reader with identical polarity, strengthening but not
    // resolving that open item). Checked first: no existing field models
    // this pair (sr3player's VehicleInteractionRecord::driverHandle, Sec8.1,
    // is the SAME relative offset on the VEHICLE object - a different real
    // field by the project's own object-type-keyed-offset convention, not a
    // reuse candidate). CHOSEN false (not confirmed): applies UNCONDITIONALLY
    // even to a forced request (Sec7.4a step 3 is not gated on "forced"), so
    // leaving this OpenValue-strict would throw OpenStateError on every
    // single enter attempt including vehicle_enter_guardian_angel's forced
    // path, which would make implementing this gate at all pointless (same
    // "a default is needed so as not to block everything" reasoning as
    // aiTargetOwnerGatePasses/aiTargetActorGateBit33 above) - the common
    // case for a character who is not already mid some OTHER tracked
    // interaction is that this pair is zero. A test can still flip it to
    // exercise the refusal branch.
    bool vehicleEnterEligibilityBusyPendingNonZero = false; // CHOSEN false - +0x16d8/+0x16dc, Sec7.4a step 3

    // Step 6's generic, non-vehicle-specific interaction-lock helper
    // (0x009562d0, widely reused elsewhere) - Sec7.4a's own text: "shape
    // confirmed, exact semantics OPEN, hinges on a +0x1b78-indirected
    // sub-object and two unidentified hash constants 0x1d56/0x1d7e." The
    // spec explicitly flags this as unresolved, so - unlike the busy/pending
    // pair above - this is kept genuinely OPEN (no CHOSEN default), per this
    // project's "never silently guess at something the spec itself calls
    // open" discipline: a test must .set() it to get past step 6 for a
    // NON-forced request (forced skips this check entirely, Sec7.4a step 6's
    // own "only when NOT forced" gate). True = the lock PASSES (does not
    // refuse) - same "GatePasses" polarity convention as
    // forceFlagGatePasses/controlLockGatePasses/behaviorSetVtableGatePasses
    // above.
    OpenValue<bool> genericInteractionLockGatePasses{
        "character generic interaction-lock helper 0x009562d0 (shape confirmed, exact semantics OPEN)",
        "spec-player-runtime.md Sec7.4a step 6"};

    // Step 8's "+0x2ba bit 0x10, the exact, already-named cant_enter_vehicle
    // flag" (Sec7.4a). Checked first: this project's OWN cantFleeFlag/
    // followerCanAbandon/cantCowerFlag fields below are a DIFFERENT real
    // byte at absolute +0xa (their own doc comments are explicit about that
    // offset), not this ai_force_flags-table byte at +0x2ba (sub-object
    // +0x2b0 + 0xa, the same human_ai_data sub-object humanAiDataForceFlagsByte8/
    // humanAiDataForceFlagsByte9 below already model two OTHER bytes of,
    // +8/+9) - no existing field is this byte, despite the coincidentally
    // overlapping flag NAMES (the real engine apparently has two
    // independent "cant_flee"/"can_leave_behind"-named bits at two different
    // byte offsets; not reconciled here, out of this task's scope). CHOSEN
    // false (not confirmed for THIS project's own per-instance data, since
    // this host has no .xtbl per-class AI data loader): cross-referenced
    // against sr3airuntime's OWN already-confirmed model of this exact
    // physical byte, decision.h's ForceFlags::cantEnterVehicle (+0x2ba bit
    // 4 of the 41-flag table, Sec9.9), which that unit's own A1 pass
    // defaults to false for every one of its 41 fields - the same
    // "ai_force_flags default off" conclusion a sibling, independently-
    // confirmed unit of this project already reached, reused here rather
    // than re-guessed from nothing (sr3airuntime and sr3luahost are
    // separate subsystems with no header coupling, so the VALUE is reused,
    // not the field itself).
    bool cantEnterVehicleFlag2ba = false; // CHOSEN false - +0x2ba bit 0x10, Sec7.4a step 8

    // The shared enter submitter 0x00af22a0 (spec-player-runtime.md Sec7.4,
    // completing Sec38.1): its boolean result depends on the eligibility
    // predicate 0x00b0ca60 (Sec7.4a, IMPLEMENTED above as
    // vehicleEnterEligibilityGate() - a refusal there is now a CONFIRMED
    // false, computed, not OPEN) and the vehicle/seat pick
    // 0x00b0fdf0/0x00b105c0 (U6.2 - world state this host still has no
    // model of). A test's own .set() on this field, when present, still
    // wins outright (checked BEFORE the gate runs at every call site) - full
    // backward compatibility with every existing test that drives the
    // overall accepted/refused result directly. Two separate facts, because
    // a "forced" request (request +0x23 bit 3, vehicle_enter_guardian_angel)
    // skips several of the eligibility refusals an ordinary one is subject
    // to (Sec7.4 step 3; Sec7.4a step 6).
    //
    // 2026-10-09 follow-up: an ELIGIBLE gate result used to fall through to
    // this OpenValue's own .get(), which throws OpenStateError whenever no
    // test has .set() it - in practice this meant EVERY real mission-drive
    // run (m05/m07/m09 confirmed by direct measurement) blocked here anyway,
    // because nothing in a real run ever calls .set() on this field: wiring
    // the eligibility gate alone did not move a single mission. Sec7.4 step 4
    // (the seat pick, U6.2) is a genuinely separate, unmodelled unit - no
    // spec text here gives its success/failure condition, and this host has
    // no vehicle seat-occupancy/availability state to compute it from. CHOSEN
    // default, in computeVehicleEnterSubmitAccepted() (lua_spec_confirmed_
    // stubs_b15_wave2_core_dispatchers.cpp): an eligible gate result with no
    // test override now resolves to `true` (seat pick assumed to succeed)
    // rather than throwing - the same "a default is needed so as not to
    // block everything" reasoning as vehicleEnterEligibilityBusyPendingNonZero
    // above, applied one level up. This is a genuine, flagged guess, not a
    // confirmed fact: it is wrong for any real case where the real
    // 0x00b0fdf0/0x00b105c0 would refuse an eligible character (e.g. a full
    // vehicle, no free seat matching the request) - this project has no way
    // to tell that case apart from a successful pick yet. Routed to the
    // disassembly team as an open question (see this task's own report):
    // what, concretely, can 0x00b0fdf0 (with a vehicle) / 0x00b105c0
    // (auto-pick) refuse, beyond "no free seat" - and does any confirmed
    // mission-script case in m05/m07/m09 actually depend on a refusal here
    // (i.e. is `true` ever observably wrong for these three missions
    // specifically)? The OpenValue itself is left in place, name and spec
    // citation unchanged, so a test's own .set() (exact seat-pick outcome)
    // still overrides the CHOSEN default exactly as before, and so a future
    // pass that actually models U6.2 only has to add .set() calls, never
    // touch this field's shape.
    OpenValue<bool> vehicleEnterSubmitAccepted{
        "0x00af22a0 enter submitter result (eligibility 0x00b0ca60 + seat pick 0x00b0fdf0/0x00b105c0)",
        "spec-player-runtime.md Sec7.4; spec-lua-api-behaviour.md Sec38.1"};
    OpenValue<bool> vehicleEnterSubmitAcceptedForced{
        "0x00af22a0 enter submitter result for a forced request (+0x23 bit 3)",
        "spec-player-runtime.md Sec7.4; spec-lua-api-behaviour.md Sec38.1"};
    // vehicle_enter_do's third dispatch path (Sec38.1): the AI order that
    // stages a default action template and starts action 0x31 (= index 49
    // "vehicle enter scpt" of the 70-entry CAE table, Sec30.4) - whether it
    // starts is OPEN.
    OpenValue<bool> vehicleEnterAiOrderStarts{"vehicle_enter_do AI-order path: action 0x31 start result",
                                              "spec-lua-api-behaviour.md Sec38.1"};
    static constexpr int kVehicleEnterPathInstant = 1;       // instant placement (submitter)
    static constexpr int kVehicleEnterPathNamedSeat = 2;     // named-seat placement (submitter)
    static constexpr int kVehicleEnterPathAiOrder = 3;       // AI order, action 0x31
    static constexpr int kVehicleEnterPathGuardianAngel = 4; // forced submit (vehicle_enter_guardian_angel)
    struct VehicleEnterRequestRecord {
        std::string vehicleName; // the resolved vehicle (direct, or the character-fallback's own vehicle)
        int path = 0;            // kVehicleEnterPath*
        int64_t seatIndex = -1;  // request +0x00 "requested seat (-1 none)" (spec-player-runtime.md Sec7.2); int64 = the 0x00ea2596 conversion's own width
        std::string seatName;    // vehicle_enter_do arg 7 ("" = absent)
        bool arg5 = false;       // vehicle_enter_do's two optional booleans - roles not given by Sec38.1
        bool arg6 = false;
        bool accepted = false;   // the dispatched path's own result (an order's result for group_do is never read)
    };
    std::optional<VehicleEnterRequestRecord> lastVehicleEnterRequest;
    uint64_t vehicleEnterRequestCount = 0;

    // vehicle_enter_check_done (Sec22.3): the state-code byte pair compared
    // against '&'/0x12. CONFIRMED (2026-10-10 Team A spec sync, closing this
    // field's own previously-OPEN spawn-default question): +0x501 is the
    // character's current AI-mode byte, spawn-default 0x00 = Idle, zeroed
    // unconditionally by the character base constructor 0x00954340 (see
    // EngineState::getOrCreateCharacter's own seed). Exactly 3 writers exist
    // project-wide: construction (above), the main gameplay AI-mode
    // transition primitive 0x004dd380 (spec-ai-runtime.md Sec3.3, every
    // voluntary mode change), and the proxy-only network read 0x004ea530
    // (non-owned/proxy human only, spec-ai-runtime.md Sec2.4 point 3). The
    // full 44-value mode enum is spec-ai-runtime.md Sec3.2 (0=Idle,
    // 18=Hijack, 43=Player, etc; see kAiModeIdle/kAiModeHijack below). The
    // "rich helper" 0x009b95c0 consulted only when arg 2 is true (its
    // internal role beyond the decoded branch shape is OPEN) is unaffected.
    OpenValue<int32_t> stateCodeByte501{"character state-code byte +0x501", "spec-lua-api-behaviour.md Sec22.3"};
    // +0x504 (requested-mode byte): spawn-default 0x2d/45, the shared
    // "no mode" sentinel for the previous/requested-mode fields (spec-lua-
    // api-behaviour.md Sec22.3, same 2026-10-10 sync) - not pre-seeded here
    // because no test currently depends on its spawn value and the "no
    // mode" sentinel is not itself a mode-enum value worth a constant; the
    // vehicle_enter_check_done 0x12 comparison below still names kAiModeHijack.
    OpenValue<int32_t> stateCodeByte504{"character state-code byte +0x504", "spec-lua-api-behaviour.md Sec22.3"};
    OpenValue<bool> enterCheckHelper9b95c0{"0x009b95c0 vehicle-enter re-check helper result (arg 2 true only)",
                                           "spec-lua-api-behaviour.md Sec22.3"};
    // Reused again, 2026-10-09, by Sec7.4a's eligibility gate step 8: the
    // SAME physical byte +0x501 is the per-human "current AI mode" field
    // sr3airuntime's own A0 unit fully models as ModeState::currentMode
    // (mode_table.h) - a DIFFERENT subsystem with no header coupling to
    // this one (checked: no sr3luahost header includes sr3airuntime's, and
    // vice versa - the Lua host's vehicle-enter path does not drive the AI
    // mode machine). "Idle" is CONFIRMED mode id 0 there (mode::Idle) and
    // independently by this very spec file's own direct citations
    // (spec-player-runtime.md lines 647/656: "+0x501 is 0 (Idle)") - a
    // cross-subsystem CONFIRMED fact, not a guess, so Sec7.4a step 8's
    // "allow if current AI mode isn't Idle" reuses stateCodeByte501 and
    // compares it against the literal 0 (kAiModeIdle below) rather than
    // including sr3airuntime's header for one constant.
    static constexpr int32_t kAiModeIdle = 0; // sr3airuntime::mode::Idle (mode_table.h) - same physical +0x501
    // CONFIRMED (spec-ai-runtime.md Sec3.2 mode 18; spec-lua-api-behaviour.md
    // Sec22.3, 2026-10-10 sync): mode 18 = Hijack in the 44-value mode enum -
    // the literal 0x12 vehicle_enter_check_done tests against stateCodeByte504
    // (see stub_vehicle_enter_check_done).
    static constexpr int32_t kAiModeHijack = 0x12; // sr3airuntime mode::Hijack equivalent - same physical +0x501/+0x504

    // Sec7.4a step 8's AI sub-state byte (+0x500) - CONFIRMED meaning
    // ("observe vehicle world accident"/"observe vehicular accident" at
    // values 0x10/0x11, resolved against the already-documented 177-entry
    // sub-state table 0x012e1168) but, like stateCodeByte504 above (the
    // requested-mode byte's own per-character runtime value, not its spawn
    // default), no per-character VALUE is given by any spec this project
    // has, so it is OPEN until a test sets it - the SAME "+0x500/+0x501" real pair
    // sr3airuntime's A0 unit already fully owns as ModeState::subState/
    // currentMode (mode_table.h), reused here only by VALUE citation per
    // this field's own doc comment above, same cross-subsystem-boundary
    // reasoning, no new field added on the sr3airuntime side for this.
    OpenValue<int32_t> aiSubStateByte500{"character AI sub-state byte (+0x500)",
                                          "spec-player-runtime.md Sec7.4a step 8; spec-ai-runtime.md Sec2.5 (sub-state table 0x012e1168)"};

    // move_to_do (Sec21.22): the per-character predicate 0x00a3bae0(target,
    // arg5) - one OPEN fact per value of its boolean argument, since the
    // result may depend on it; the mode-3 animation-tag probe (0x004b46c0 on
    // (+0xd24)+0x10, codes 0x32/0x34 - "either resolves" keeps mode 3, else
    // mode 1's code); and the move dispatch 0x00993630's result.
    OpenValue<bool> moveToPredicateA3baeArgFalse{"0x00a3bae0(character, false) move-to predicate",
                                                 "spec-lua-api-behaviour.md Sec21.22/Sec22.15"};
    OpenValue<bool> moveToPredicateA3baeArgTrue{"0x00a3bae0(character, true) move-to predicate",
                                                "spec-lua-api-behaviour.md Sec21.22/Sec22.15"};
    OpenValue<bool> moveToMode3AnimTagResolves{"0x004b46c0 lookup of code 0x32 or 0x34 on (+0xd24)+0x10 (move mode 3)",
                                               "spec-lua-api-behaviour.md Sec21.22"};
    OpenValue<bool> moveToDispatchSucceeds{"0x00993630 move dispatch result", "spec-lua-api-behaviour.md Sec21.22"};
    // The per-character move-to sub-record (Sec22.15: +0xc30 a 4-byte field
    // cleared to -1, +0xc34 a 2-byte field cleared to 0). Write-only in this
    // batch (move_to_check_done's existing stub, b04, still consults only the
    // shared request pool), so std::nullopt = "never written by an in-scope
    // native" rather than an invented construction value. +0xc30 holds the
    // 0x00d9e180(2000,4000) randomized deadline on success (Sec22.15: "hidden
    // this = character +0xc30"); this host stores a nextMonotonicStamp()
    // marker for it (same CHOSEN clock stand-in as regenRestartDeadlineStamp),
    // -1 on every early-failure path (CONFIRMED sentinel).
    std::optional<int64_t> moveToTimerC30;
    std::optional<int32_t> moveToRequestTagC34;
    struct MoveToRequestRecord {
        std::string destinationName; // arg 2 (only when arg 6 is false - the arg-6 path is refused, see the stub)
        int64_t moveMode = 0;        // arg 3, truncated
        int32_t stateCode = 0;       // the settled 1/2/3 state code passed to 0x00993630
        bool arg4 = false;
        bool arg5 = false;
        int32_t returnedId = 0;
    };
    std::optional<MoveToRequestRecord> lastMoveToRequest;
    uint64_t moveToPreMoveCallCount = 0; // 0x0095acb0(target, 1, 0.2, 0, 0) - unconditional once arg 1 resolves

    // teleport_to_object (Sec22.4) - its OWN record, deliberately separate
    // from teleport's (Sec4.3) EngineState::teleportRequests()/
    // teleportScriptedRequestCount(): the two natives share the request-id
    // kind (4) and the general applier 0x00996b50 but not their argument
    // records (arg 8 is a boolean for teleport, a heading in radians here),
    // so nothing teleport's existing fields mean is reused or overwritten.
    struct TeleportToObjectRequest {
        std::string targetObjectName;
        bool arg3 = false;           // CONFIRMED default false; role not named by Sec22.4
        bool arg4 = true;            // CONFIRMED default true (Sec22.4's own default asymmetry note)
        double offsetX = 0.0, offsetY = 0.0, offsetZ = 0.0; // args 5-7, raw - no coordinate system in this host
        double headingRadians = 0.0; // arg 8 as passed
        double headingDegrees = 0.0; // arg 8 * 57.29583 (CONFIRMED constant 0x0111a3f8)
        bool tookPlayerPath = false; // class-row +0xa bit 0x2 (0x009e2220) vs the general applier (0x00996b50)
        bool appliedLocally = false; // general path with authority: applied + kind-4 request completed at once (Sec35.4)
    };
    std::optional<TeleportToObjectRequest> lastTeleportToObjectRequest;
    uint64_t teleportToObjectCount = 0;
    uint64_t teleportToObjectShieldEjectCount = 0; // 0x00996b50's forced human-shield-partner ejection fired

    // character_damage (Sec20.32): 0x0096fa20's early exit on the target's
    // descriptor byte +0x33 bit 0x2 (no construction value given - OPEN), and
    // the non-authoritative branch's opcode-0x41 record (amount, flags word,
    // source) - the only branch whose outcome this host can reproduce.
    OpenValue<bool> damageImmuneDescriptorBit33{"character descriptor byte +0x33 bit 0x2 (0x0096fa20 early exit)",
                                                "spec-lua-api-behaviour.md Sec20.32"};
    struct CharacterDamageRecord {
        float amount = 0.0f;   // single precision, NOT rounded (Sec20.32's distinct numeric sub-idiom)
        uint32_t flags = 0;    // 0x100 always | 0x200 from arg 3
        std::string sourceName; // "" = no (resolved) source
    };
    std::optional<CharacterDamageRecord> lastReplicatedDamage;
    uint64_t characterDamageReplicateCount = 0;

    // qte_start (Sec20.9): the named-hook-slot-array registration at base
    // +0x1f00, preset slot 0x18 (0x005e4660 - the SAME setter on_death/
    // on_attack_performed's generic path reach; empty string clears, same as
    // onDeathCallback). Same single-dedicated-field-per-hook-kind convention
    // as onAttackPerformedGenericSlotCallback (slot 8 on this same base).
    std::string qteResultHookCallback;
    uint64_t qteStartReplicateCount = 0; // 0x0060dd60's non-authoritative opcode-0x59 record

    // --- Ranking tranche 05 (spec-lua-api-behaviour.md Sec33), batch 2026-10-02 ---

    // ai_clear_priority_target (Sec33.2): the character's AI forced-target
    // handle (human_ai_data/forced_target_handle), cleared to the null id
    // pair by that call. No Lua-visible query of this field exists among
    // this project's in-scope names; "" is this project's own chosen
    // "null id pair" stand-in (same convention as onTakeDamageCallback's
    // own empty-string default above).
    std::string forcedTargetHandle;

    // ai_set_in_scripted_cover (Sec33.2): AI force-flag byte +0x2bc bit
    // 0x1 - the SAME physical byte ignoreAI above models bit 0x2 of
    // (Sec33.2's own cross-reference). This minimal registry keeps the two
    // bits as independent fields (stated simplification, not a shared bit
    // word - same convention this header already uses throughout, see
    // this struct's own top note) rather than retrofitting ignoreAI into a
    // bit word.
    OpenValue<bool> inScriptedCover{"AI force-flag byte +0x2bc bit 0x1 (in_scripted_cover)",
                                    "spec-lua-api-behaviour.md Sec33.2"};

    // ai_set_in_scripted_cover (false path) / ai_do_scripted_rush (Sec33.2/
    // Sec33.3): the character's current "scripted action" id - CONFIRMED
    // mechanism (a single id field this document's own scripted-action
    // family writes, e.g. action 8 "take cover" elsewhere, 13 here, 22 for
    // rush); the real-world meaning of each id beyond ones already named
    // elsewhere is OPEN/HYPOTHESIS per entry, not claimed here.
    OpenValue<int32_t> scriptedAction{"character scripted-action id", "spec-lua-api-behaviour.md Sec33.2/Sec33.3"};

    // ai_do_scripted_rush (Sec33.3), "with a target" path: the rush
    // target's own name - this project has no position data to copy the
    // real target position into (stated simplification, same "record the
    // raw request, not real physics" convention as MinimapIconRecord/
    // ObjectIndicatorRecord above).
    std::string scriptedRushTargetName;

    // ai_pay_attention_to_position (Sec33.2): this project has no real
    // position/coordinate data for any named object anywhere (stated,
    // project-wide simplification) - records only the resolved source-
    // object's own name and the derived attention mode (7 = true/"attend",
    // 3 = false, CONFIRMED mapping; bit 0x4 of the real mode IS the Lua
    // boolean, per spec).
    std::string attentionSourceObjectName;
    int32_t attentionMode = 0; // 0 = never called; else 7 or 3 (CONFIRMED values)

    // boss_battle_matt_begin (Sec33.5): dword +0xec bit 0x4000 on the
    // character literally named "Matt" - a new, generic per-character bit
    // this header has not previously modeled (real-world meaning OPEN per
    // spec). OpenBits32 since only this one bit is ever confirmed.
    OpenBits32 flagsEc{"character dword +0xec", "spec-lua-api-behaviour.md Sec33.5"};

    // player_revive (Sec32.3): object +0xcc8, 6 = "downed" (the same test
    // Sec7.13 `human_is_downed` uses, cross-referenced there - that function
    // itself is out of this batch's scope); 5 = dead/kill-finalized (Sec34.5,
    // written by the kill finalizer). CONFIRMED (Sec34.5, 2026-10-03): the
    // constructor zeroes this field, so a freshly-spawned character starts
    // at 0 ("alive, ordinary," neither downed nor dead) - seeded at
    // construction (EngineState::getOrCreateCharacter), not left OPEN.
    OpenValue<int32_t> lifeState{"character life-state field (+0xcc8)", "spec-lua-api-behaviour.md Sec7.13/Sec32.3/Sec34.5"};
    // Sec34.5: byte +0xe3 bit 0x01, the second of the two real conditions
    // the 0x0096f4f0 alive/not-gone predicate tests (alongside lifeState ==
    // 5 above) - see CharacterState::notDeadForActionPlay()'s own doc
    // comment for how the two combine. HIGH CONFIDENCE (Sec34.5): clear at
    // construction, inferred from a zero-filled allocation rather than an
    // observed constructor write - seeded at construction all the same
    // (EngineState::getOrCreateCharacter).
    OpenValue<bool> killFinalizedFlagE3{"character dead/kill-finalized flag (+0xe3 bit 0x01)",
                                        "spec-lua-api-behaviour.md Sec34.5"};

    // Attacker/threat-style handle pair, character +0x18f0/+0x18f4 (found
    // 2026-10-09 during spec-lua-api-behaviour.md Sec3.4's own in-place
    // correction - see CharacterState::stateCodeByte501/aiSubStateByte500's
    // own doc comments and set_ignore_ai_flag's stub for what that
    // correction actually fixed; this field is the genuine handle the
    // correction found while tracing where a REAL attacker/threat
    // reference might live, since Sec3.4's own text did not name one).
    // CONFIRMED zeroed unconditionally at character construction (via
    // 0x00954340 -> 0x00942920 - the same zeroing-constructor chain
    // Sec34.1 already traces for lifeState/stateEnum/maxHitPoints above),
    // so a freshly-spawned or script-created character (the m07 kind)
    // always starts at 0 ("no reference") - seeded at construction
    // (EngineState::getOrCreateCharacter), not left OPEN. CONFIRMED
    // cleared mutually on death by the SAME kill-pipeline finalizer
    // (0x009a7610) killFinalizedFlagE3 above already models - see
    // character_kill's own stub (lua_spec_confirmed_stubs_b30_
    // tranchef_camera.cpp) for where this is applied. Its only confirmed
    // consumer anywhere in this project is the already-documented native
    // human_being_revived_by (Sec54.3) - the "reviver handle" (who can
    // revive this character). No writer that sets it nonzero during
    // normal gameplay (taking damage, engaging/being engaged) was found -
    // whether this field doubles as a genuine combat attacker-reference or
    // is purely a reviver handle stays OPEN; do not assume a combat-
    // engagement writer exists for it without further tracing. It is NOT
    // the same thing as the former (removed) attackerThreatRef field above
    // and must not be read by set_ignore_ai_flag's own gate.
    OpenValue<int64_t> reviverHandle{"character attacker/threat-style reviver handle (+0x18f0/+0x18f4)",
                                     "spec-lua-api-behaviour.md Sec3.4 (2026-10-09 correction)/Sec54.3"};

    // character_kill (Sec3.15, 0x00a41a60, cleared 2026-10-08/tranche F).
    // CONFIRMED: resolves via the SAME generic/#PLAYER#-then-fallback chain
    // as on_vehicle_destroyed/character_is_dead (playerClassChainResolves()
    // primary, objectResolves() fallback), then routes a FIXED lethal-
    // damage constant (-1.0f) through the shared generic damage-application
    // routine (0x0096fa20) with a flags word carrying bit 0x100 alone when
    // arg 2 is true (tranche-F resolution: no conflict with Sec20.32's
    // different caller). This project does not simulate 0x0096fa20's own
    // ~5,200-instruction body (hit-point math, notoriety/network-authority
    // checks, ragdoll impulse) - "lethal" is modeled as deterministic: the
    // call always results in death (lifeState -> 5, the SAME value
    // character_is_dead/player_revive already read as "dead/kill-
    // finalized"), since the constant's own name and the entry's own "not a
    // bypass" framing both read as "guaranteed death via the normal
    // pipeline," not a possibly-survivable hit. CHOSEN, not independently
    // re-derivable from a static read of 0x0096fa20 itself (out of scope).
    // arg 2's own precise real-world meaning stays OPEN (the bit itself is
    // CONFIRMED, its purpose is not) - recorded test-observably, not acted
    // on beyond that.
    uint64_t characterKillCallCount = 0;
    bool lastCharacterKillArg2 = false;
    // arg 3 (death-reaction/animation name, default absent): looked up in
    // the SAME animation-state name->id table set_animation_state (Sec7.11)
    // uses (EngineState::lookupAnimationStateId()), only once the
    // character is confirmed dead - CONFIRMED gate, HIGH CONFIDENCE lookup
    // table reuse per Sec3.15's own citation of Sec7.11's table.
    int32_t lastDeathReactionAnimationId = -1;
    uint64_t deathReactionDispatchCount = 0;
    // arg 4 (optional boolean, default true - CONFIRMED opt-in: the
    // finalizer runs only when explicitly false). CONFIRMED finalizer gate:
    // only when lifeState is currently 6 ("downed," the same value
    // human_is_downed/player_revive already test) does the finalizer's own
    // dead-state writer (0x00952760) run, writing the SAME literal 5
    // lifeState already uses; the finalizer's own "clears the character's
    // threat/attacker-reference fields" effect has no other in-scope reader
    // in this project, so only that it ran is recorded.
    uint64_t characterKillFinalizerCount = 0;

    // Test-observable count of successful player_revive calls (downed ->
    // processed). The real post-revive life-state value is not stated by
    // Sec32.3 (HIGH CONFIDENCE only that the character is no longer downed
    // afterward) - not modeled here, to avoid guessing a numeric value the
    // spec itself doesn't give.
    uint64_t reviveCount = 0;

    // npc_go_idle (Sec32.4): count of AI-orders-sub-object resets (+0x510:
    // clears several handles, zeroes several fields, resets 22 fixed-size
    // slots - not individually modeled, only that a reset occurred is
    // counted).
    uint64_t aiOrdersResetCount = 0;

    // --- turn_invulnerable (spec-lua-api-behaviour.md Sec3.7, re-touched by
    // ranking tranche 14's own Sec46.1 bit-conflict resolution), batch
    // 2026-10-02 -------------------------------------------------------

    // Sec3.7: object +0x1c98 bit 0x20 ("invulnerable", 0x00946190) and bit
    // 0x200 ("always apply player damage", 0x00946300). CONFIRMED (Sec3.7's
    // own text): "Both setters are the SAME record-and-replicate idiom
    // ...not a dead branch" - the double-gate (0x008ae480/0x008837a0) is
    // modeled explicitly here, mirroring VehicleState::forceFlagGatePasses
    // below (Sec30.5's identical shape for the vehicle side of this same
    // function) - a deliberate choice NOT to simplify to "always write
    // directly" the way ai_set_in_scripted_cover's own stated simplification
    // does (Sec33.2), because Sec3.7's text is unusually explicit that this
    // particular gate is real and tested, not a dead/unreachable branch.
    // UPDATE 2026-10-03 (Sec3.7's own single-player resolution, cross-ref
    // Sec6.22): with no co-op session installed, gate 1 (0x008ae480)
    // resolves true on every reachable branch (traced through its own
    // connection-state guard and sentinel fallthrough) and gate 2's
    // (0x008837a0) backing global stays at its zero-fill default, since its
    // one writer anywhere in the binary only ever runs while actively
    // applying an already-received network record - unreachable with no
    // session. So the whole idiom family takes the direct-local-apply
    // branch UNCONDITIONALLY in single player; the network-record fallback
    // is dead code there, not merely rare. CONFIRMED, not CHOSEN: seeded
    // true at construction (EngineState::getOrCreateCharacter), not a
    // default awaiting a test - a test can still .set(false) to exercise
    // the (now known-dead) replicate-only branch for structural coverage.
    OpenValue<bool> forceFlagGatePasses{"character force-flag double-gate (0x008ae480/0x008837a0)",
                                        "spec-lua-api-behaviour.md Sec3.7"};
    OpenBits32 forceFlags1c98{"character force-flag byte +0x1c98 (invulnerable 0x20 / always-apply-player-damage 0x200)",
                              "spec-lua-api-behaviour.md Sec3.7"};
    static constexpr uint32_t kInvulnerableBit1c98 = 0x20u;
    static constexpr uint32_t kAlwaysApplyPlayerDamageBit1c98 = 0x200u;
    // character_prevent_explosion_fling/character_prevent_bumping/
    // character_set_never_catch_fire (Sec7.20/Sec7.26/Sec7.29) - 3 more
    // confirmed bits on this SAME shared field (7 total across Sec3/Sec7).
    static constexpr uint32_t kPreventExplosionFlingBit1c98 = 0x10u;
    static constexpr uint32_t kPreventBumpingBit1c98 = 0x1000u;
    static constexpr uint32_t kNeverCatchFireBit1c98 = 0x4000u;
    // character_prevent_flinching (Sec3.14) / character_allow_ragdoll
    // (Sec3.16), batch 2026-10-08 (the 6-native clearance fast-follow): 2
    // further confirmed bits on this SAME shared +0x1c98 field (9 total).
    // Both CONFIRMED by independent direct disassembly 2026-10-08
    // (resolvers `0x00a281a0`/`0x00945eb0` for Sec3.14, `0x00a281a0`/
    // `0x00945d40` for Sec3.16; both reached via the generic/`#PLAYER#`-
    // chain resolver, same family as turn_invulnerable above, and gated by
    // the SAME forceFlagGatePasses double-gate). kNeverRagdollBit1c98 is
    // stored in the BIT'S OWN sense ("never ragdoll") - CONFIRMED the real
    // setter is called with the LOGICAL NEGATION of the Lua `allow`
    // argument (`test bl,bl; sete dl` at the call site), the same
    // "negated allow_*/enable_*" convention Sec3.16/Sec6.18/Sec12.9 already
    // establish elsewhere in this header.
    static constexpr uint32_t kPreventFlinchingBit1c98 = 0x800u; // Sec3.14 - stored in its own direct sense, no negation
    static constexpr uint32_t kNeverRagdollBit1c98 = 0x80u;      // Sec3.16 - stored as "never ragdoll" (negated from the Lua `allow` argument)

    // --- character_fake_revival_start/_end (Sec45.1, "ranking tranche 13"),
    // batch 2026-10-02 --------------------------------------------------

    // Dword +0xe4 bit 0x10000 ("fake revival in progress", HIGH CONFIDENCE -
    // established together by both functions' own shared no-null-check
    // shape, Sec45.1). A new per-character bit on a new dword, same
    // "generic per-character bit this header has not previously modeled"
    // convention as flagsEc above (Sec33.5).
    OpenBits32 flagsE4{"character dword +0xe4", "spec-lua-api-behaviour.md Sec45.1"};
    static constexpr uint32_t kFakeRevivalInProgressBit = 0x10000u;

    // --- character_take_human_shield_check_done (Sec45.1), batch
    // 2026-10-02 ---------------------------------------------------------

    // The taker's own held-hostage name. Sec45.1's own text: "A character
    // carries two separate hostage-handle fields, one for network-owned
    // characters and one for locally-owned ones" - this project has no real
    // networking layer (stated, project-wide simplification), so only the
    // single, locally-owned field is modeled; CHOSEN, write-only in scope
    // (no in-scope setter populates it - a test sets it directly, same
    // "test sets it directly" convention as CharacterState::currentVehicleName
    // above). Empty = "holds nothing".
    std::string humanShieldHostageName;

    // --- character_hidden (Sec45.1), batch 2026-10-02 (resumed session) ---

    // Bit 0 of +0x3b. CONFIRMED (Sec45.1): "the same bit is the 'skip hidden
    // object' test debris_flow_set_inactive's worker uses" - supports
    // "object is hidden" as the real meaning. NOTE the inverted-from-usual
    // unresolved behaviour lives in the STUB (lua_spec_confirmed_stubs.cpp),
    // not here: "an unresolved name reads as hidden (true), not an error" -
    // this field only ever holds the RESOLVED-case value.
    OpenValue<bool> hiddenFlag{"character hidden flag (+0x3b bit 0)", "spec-lua-api-behaviour.md Sec45.1"};

    // --- flee_to_navpoint (Sec44.5/Sec44.6), batch 2026-10-02 (resumed
    // session) -----------------------------------------------------------

    // CONFIRMED (Sec44.5): queues a per-character AI event (type 0x30) with
    // the navpoint position and a threat handle, "silently dropped if the
    // event pool is empty, the character is dead, or it is a player-class
    // object." This project has no real AI-event free-list/pool to exhaust
    // (same "no generic entity shape invented" boundary this header's own
    // top note states) - the dead/player-class gates ARE modeled (the stub
    // checks them before incrementing), but "pool empty" is not reachable
    // here, so this counter undercounts only in that one, documented way.
    // Plain counter, not OpenValue, matching aiOrdersResetCount's own
    // precedent just above for "a reset/request occurred N times," not a
    // specific record's own content.
    uint64_t fleeToNavpointRequestCount = 0;
    // `flee` (Sec45.1, 0x00a49b30): count of qualifying flee-order requests posted for this actor.
    uint64_t fleeRequestCount = 0;

    // --- action_play_do (spec-lua-api-behaviour.md Sec22.29), real-hit
    // probe-mode batch (2026-10-03) ---------------------------------------

    // Sec22.29's own text: "if the character resolves but is either forced
    // (arg 4) or currently NOT dead/flagged (the already-established
    // predicate 0x0096f4f0, item 22.3/Sec18.14)". This is the SAME real
    // predicate CharacterState::isAlive above already cites (Sec30.5) -
    // kept as a SEPARATELY SCOPED predicate rather than reused, matching
    // isAlive's own doc comment's explicit reasoning ("a DIFFERENT field
    // from isDeadHighConfidence... a separate call site this predicate is
    // never confirmed to share"): Sec22.29 never confirms this call site
    // shares timing/scope with vehicle_exit_group_check_done's own, so
    // widening isAlive to cover both would be assuming a fact neither
    // section states.
    //
    // UPDATE 2026-10-03 (Sec34.5, closing the 7-mission notDeadForActionPlay
    // blocker Sec22.29's own batch found): the real 0x0096f4f0 body for
    // THIS call site is CONFIRMED - dead if the resolved pointer is null
    // (handled upstream by this project's own objectResolves() resolve
    // step, before this predicate is ever reached), OR lifeState == 5
    // ("kill-finalized," above), OR killFinalizedFlagE3 is set (above).
    // Computed live from those two real fields rather than stored as its
    // own independently-settable flag, so a later write to either (e.g. a
    // future kill-finalizer implementation setting lifeState to 5) is
    // reflected here automatically instead of silently going stale.
    // Throws OpenStateError if either underlying field is itself OPEN
    // (e.g. a test .forget()s one to explore that case) - same discipline
    // as every other computed predicate in this file.
    bool notDeadForActionPlay() const { return lifeState.get() != 5 && !killFinalizedFlagE3.get(); }

    // get_dist's own shared position-lookup function (0x00a455a0, Sec4.2),
    // reused by several other natives (object_is_in_trigger's arg-2 parser
    // Sec1.12, the vehicle-seated-character redirect Sec20.24). OPEN by
    // default: no spec gives a CONFIRMED spawn position for any object (the
    // save format doesn't carry one either - spec-save-format.md Sec6.6,
    // confirmed 2026-10-03), so this starts unset for every character and
    // stays that way until something writes it (a future teleport/spawn
    // implementation). A read while unset throws OpenStateError, which is
    // the correct, honest outcome for "resolved object, unknown position" -
    // NOT the same case as "name failed to resolve at all", which get_dist
    // handles separately via objectResolves() and its own CONFIRMED
    // FLT_MAX sentinel (Sec4.2), never this field.
    OpenValue<WorldPosition> worldPosition{"character world position (get_dist's own 0x00a455a0 lookup)",
                                            "spec-lua-api-behaviour.md Sec4.2"};

    // --- player_controls_disable / player_controls_enable /
    // player_movement_disable (spec-lua-api-behaviour.md Sec7.22/Sec7.7/
    // Sec7.25), wired to the player control-state stack
    // (spec-player-runtime.md Sec2), batch 2026-10-03 -------------------

    // The byte at object +0x1d99: bit 0x08 "controls disabled" (setters
    // 0x009e2dd0 / 0x009e4940) and bit 0x10 "movement disabled" (0x009e2fa0 /
    // 0x009e4a60). Two further bits of the same byte are documented
    // elsewhere (0x20, Sec27.x; 0x80, Sec31.x) and not modelled here. Kept as
    // two independent OpenValue<bool> fields rather than one bit word - the
    // same per-bit simplification ignoreAI/inScriptedCover already use for
    // the shared byte +0x2bc - so a probe-mode read can go through
    // EngineState::probedFieldOr(). CONFIRMED/HIGH spawn default (spec-
    // player-runtime.md Sec2.10, synced 2026-10-03, after an earlier pass
    // found no spec gave this byte's construction value at all): HIGH
    // CONFIDENCE all-clear (both bits 0) at spawn - no constructor touches
    // +0x1d99 and no initialiser exists, so it rests on the pool allocation
    // being zero-filled, the same caveat already established for
    // lifeState/killFinalizedFlagE3 (Sec34.5). Seeded false at construction
    // (EngineState::getOrCreateCharacter) - a test can still .set()/.forget()
    // to exercise the disabled/OPEN cases directly.
    OpenValue<bool> controlsDisabled{"character byte +0x1d99 bit 0x08 (controls disabled)",
                                     "spec-lua-api-behaviour.md Sec7.7/Sec7.22"};
    OpenValue<bool> movementDisabled{"character byte +0x1d99 bit 0x10 (movement disabled)",
                                     "spec-lua-api-behaviour.md Sec7.8/Sec7.25"};
    static constexpr uint32_t kControlsDisabledBit1d99 = 0x08u;
    static constexpr uint32_t kMovementDisabledBit1d99 = 0x10u;

    // The single gate 0x008addb0 (variant 2) all three setters test: true =
    // apply directly; false = the co-op-session-gated network record
    // (opcode 0x32). CHOSEN (not CONFIRMED - Sec7.7's own text says "which
    // branch dominates in single-player is not established here"): seeded
    // TRUE at construction (EngineState::getOrCreateCharacter), this
    // project's standing "single player only, the co-op path is never
    // taken" simplification for co-op-gated branches, and the only reading
    // under which these setters do anything at all in single player - with
    // no session installed (Sec26.28, coopSession().present == false), the
    // gate-false branch's own session check fails and the call would be a
    // silent no-op. A test can .set(false) to exercise that branch.
    OpenValue<bool> controlLockGatePasses{"character single gate 0x008addb0 (variant 2)",
                                          "spec-lua-api-behaviour.md Sec7.7/Sec7.22/Sec7.25"};
};

} // namespace sr3luahost
