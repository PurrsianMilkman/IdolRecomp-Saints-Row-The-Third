#pragma once

// sr3playeranim/bridge.h - "a character walks": the coupling between
// sr3player (movement/intent/locomotion, spec-player-runtime.md) and
// sr3animruntime (control parameters + node-graph evaluation,
// spec-anim-runtime.md). Nothing in either library references the other;
// this is the first unit that does. The two specs above are the ONLY
// sources for every behaviour cited below; addresses/sections in comments
// are cross-references, never derived from the executable.
//
// SCOPE. This is a new integration, not a ported engine unit - there is no
// single "0x......" address for "the bridge" the way other units have. What
// IS real and spec-cited:
//   * the field -> control-parameter writer (spec-anim-runtime.md Sec4.2,
//     the `0x00958690` census), reproduced row by row in WriteHumanWriterParams();
//   * driving the player's own graph (`player_state_machine`,
//     spec-anim-runtime.md Sec7.13) through the REAL loader+evaluator
//     (sr3animruntime::NodeGraphRuntime, U2/U3) every frame, both passes;
//   * the playback-rate formula (spec-player-runtime.md Sec5.4.3 step 7);
//   * the frame order (spec-player-runtime.md Sec5.1/Sec5.9): controls ->
//     [parameter write + graph evaluation] -> compose -> integrate ->
//     publish, exactly once per frame, with the SAME one-frame-lag the real
//     engine has on current speed / vertical speed / Speed Along rvec
//     (Sec5.9 item 4) falling out naturally from calling PlayerMovement's
//     already-tested phases individually (crouchRoutine/sprintRoutine/
//     moveSlot, then composeVelocity/integrate/publish) instead of its
//     monolithic runFrame(), so the graph's root-motion output (E) can feed
//     the SAME frame's composer exactly as `0x0099d1b0` consumes
//     `0x004c58c0`'s output same-frame.
//
// CHOSEN SIMPLIFICATION - THE POST STEP (spec-anim-runtime.md Sec6.10 step 5
// / Sec8.9 step 5, labelled OPEN in results/verify_anim_node_graph_20261004.md's
// "Known gaps"): the real engine turns the graph's 32-entry output list into
// LAYERS on the controller's animation instance (0x004cca40, 0x004bce30/
// 0x004bce90, 0x004bdb10, 0x004bd9b0/0x004bda10), each layer carrying a flag
// bit 0x01 ("phase-synchronised": CONFIRMED behaviour, HIGH name,
// spec-player-runtime.md Sec5.4.3 "Step 3 in detail") that is the REAL
// discriminator root motion sums over (set -> T, the un-normalised,
// phase-synchronised group; clear -> E, the weight-normalised rest) - NOT a
// controller-slot bit, NOT `Anim_Movement` (spec answer `a1f90ec`). This
// project has no animation-instance object, no layer array and no U4/U5
// pose system to put that bit on. PostOutputList() below is the smallest
// honest stand-in: it reads the graph's OutputEntry list DIRECTLY (bypassing
// Controller/LayerApi entirely - a single continuously-running graph
// instance has no cross-fade/slot-weight machinery to reproduce, so slot
// weight is CHOSEN = 1.0), and uses the clip block's own LOOP BIT
// (OutputEntry::kOutLoop) as the proxy for that missing layer flag: a
// looping clip (a plain locomotion cycle) is exactly what the real engine's
// layer-post also marks phase-synchronised for a graph's clips (see the
// spec passage above: "the re-trigger step" and "the graph post step for
// looping clips" both set the real bit through the clip's own loop_flag).
// This proxy is therefore not a guess - it is the one real, already-decoded
// per-entry fact (OutputEntry::flags & kOutLoop) that predicts the real bit
// correctly for every ordinary graph clip; it just is not the engine's own
// object field, because this project does not have that field anywhere.
//
// 2026-10-07: CHECKED AGAINST spec-anim-runtime.md Sec9-Sec12 (U5/U4/U6/U7,
// just landed) and STILL THE RIGHT CALL - this gap was not closed. Sec11
// (U4) says outright that the layer-flag-0x01 phase-sync split itself is
// "cited below, not re-derived" from spec-player-runtime.md Sec5.4.3 (Sec11.0)
// - i.e. unchanged by this merge. What Sec11 actually adds (the pose-request
// queue, the 8,192-byte per-job sample buffer, the bone-hierarchy compose
// pipeline `0x004c31b0`/`0x004c36f0`, the real per-layer array at instance
// +0x70) all presuppose a real animation-instance object with bones and a
// layer array - exactly the object this file's banner already says this
// project does not have. Sec9 (U5) independently confirms the post-sample
// step this gap's own framing once suspected might be root motion is instead
// the human spine-bend/look chain (bone rotations, Sec9.0 item 1) - also not
// this project's business. So: no new text supplies the missing object: the
// loop-bit proxy stays the smallest honest stand-in until a real layer array
// exists to put the real bit on.
//
// WHAT T/E CANNOT BE: actual translation vectors. `sr3anim` deliberately
// does not decode the `.anim_pc` keyframe/track payload (see its own
// README entry - "the obvious model is measurably wrong and it needs the
// runtime sampling subsystem, not more parsing"), so there is no real
// per-clip root-motion delta anywhere in this codebase (confirmed by
// tests/real_anim_node_graph_test.cpp's own FakeServices, which stubs
// `recordDuration` to a constant 1.0f even against the real 249-graph
// population - the same honest limitation, not unique to this unit).
// ClipRootMotionSource below is an EXPLICIT INJECTABLE SEAM for "how far
// does this clip's root move per second" - OPEN by default (throws), so the
// real, testable logic (which entries land in T vs E, the un-normalised vs
// normalised weighting, the loop-bit bucketing, the 0.0001 drop threshold)
// is exercised with caller-supplied numbers rather than fabricated ones.
// This replaces movement.h's own prior placeholder
// (MoveFrameInput::animationTranslationE, "zero = no root motion" - written
// before this unit existed) with a real graph-driven value built the same
// honest way.
//
// WHAT IS WRITTEN PER spec-anim-runtime.md Sec4.2, AND WHAT IS LEFT OPEN -
// see WriteHumanWriterParams()'s own comment for the row-by-row account.
//
// NOT THIS UNIT'S JOB (unchanged from sr3player/sr3animruntime's own scope
// notes): real Havok physics, vehicles, Lua native wiring, NPC-specific
// animation shaping, the real U4/U5 layer-and-pose system.

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "sr3animruntime/control_parameters.h"
#include "sr3animruntime/node_graph.h"
#include "sr3animruntime/node_graph_runtime.h"
#include "sr3luahost/open_state.h"
#include "sr3player/camera.h"
#include "sr3player/movement.h"

namespace sr3playeranim {

using sr3luahost::OpenStateError;
using sr3luahost::OpenValue;
using sr3player::Vec3;

// ---------------------------------------------------------------------------
// Subsystems this unit does not model (spec-anim-runtime.md Sec4.2 rows for
// combat, aim, weapons, parachute, vehicle). OPEN until seeded; each is
// independently gated so a caller that never triggers the gate never pays
// for it. SeedPlayerAnimCommonCase() is the CHOSEN opt-in for the plain
// on-foot player this project can actually run (mirrors sr3player's own
// SeedOnFootCommonCase idiom) - none of these is a recovered engine value.
// ---------------------------------------------------------------------------
struct AnimParamWorld {
    static constexpr const char* kSec = "spec-anim-runtime.md Sec4.2";
    OpenValue<bool> combatReady{"combat ready 0x00943a20", kSec};          // row 4
    OpenValue<bool> aimBool{"+0xd40 bit 0x10 (aim)", kSec};                // row 18
    OpenValue<bool> fireBool{"+0xdc bit 31 (fire)", kSec};                 // row 28
    OpenValue<bool> parachuteOpen{"+0xe8 bit 26 (parachute open)", kSec};  // row 23
    OpenValue<float> freeFallVertControl{"+0xaf4 (free fall vert control)", kSec};  // row 22 (only read if world.freeFall)
    OpenValue<float> aimPitch{"aim pitch (+0xf74 derived)", kSec};         // row 26
    OpenValue<float> aimHeading{"aim heading (+0xf70 derived)", kSec};     // row 27
    OpenValue<float> vehicleSurfLean{"0x006c4430 vehicle surf lean", kSec};  // row 30
    OpenValue<bool> reloading{"0x009ba8b0 reloading", kSec};               // row 31
};
void SeedPlayerAnimCommonCase(AnimParamWorld& w);

// ---------------------------------------------------------------------------
// The field -> control-parameter writer (spec-anim-runtime.md Sec4.2's
// `0x00958690` census, the human writer). Writes directly into a
// GraphInstance's alias slots (bypassing Controller's parameter BLOCK and
// its child-ring/freeze machinery entirely - a single continuously-running
// graph instance with no cross-fade members has nothing for that machinery
// to do; see tests/synthetic_anim_node_graph_runtime_test.cpp's own
// `World::set()` for the same direct-write pattern this project already
// uses in its U3 tests). A name not aliased by `graph` is silently skipped
// (spec-anim-runtime.md control_parameters.h: "a name is therefore writable
// this way only if the target object's own graph aliases it").
//
// ROW ACCOUNT (Sec4.2's table; # = that table's row number):
//   1-2  Jump Dive Bool / Offhand Grenade Bool: only gated by +0xccd == 4,
//        which this project's on-foot scope never reaches (world.ccd stays
//        0) - never written, matching the real latch ("outside the state,
//        the last written value stays", Sec4.2 notes). Thrown as OPEN if a
//        caller ever DOES set ccd == 4 (the climb/dive subsystem is not
//        modelled).
//   3    dead: world.dead (already an OPEN MovementWorld field; reused, not
//        duplicated).
//   4    combat ready: AnimParamWorld::combatReady (OPEN, false in common case).
//   5    Supported By Ground: world.supportedByGround (reused).
//   6    desired speed: Length(st.desiredMove), the UNSMOOTHED +0x2048 -
//        CONFIRMED the local-player branch (spec-player-runtime.md Sec5.3
//        row 6: "0x0091f5e0 is 'is the local player', CONFIRMED by
//        listing" - this object always is, so the else branch (+0xa60) is
//        unreachable here, not merely unmodelled).
//   7    Vertical Speed: prevVelocityD04.y - ONE FRAME OLD (Sec5.9 item 4):
//        the caller passes the state as it stood BEFORE this frame's
//        compose/integrate/publish ran (i.e. last frame's publish()).
//   8-11 move forward/left/right/back bool: Sec4.2 cites `+0xd40` bits
//        0x0400/0x0800/0x1000, but `+0xd40`'s own writer is OPEN 38
//        (sr3player/movement.h: "Movement flags +0xd40 (writers OPEN 38)") -
//        a dead always-0 stand-in there would make these four bools
//        constantly read "forward" regardless of real input, which is
//        useless for driving a walking demo. CHOSEN DEVIATION (documented,
//        not silent): derive them instead from the PUBLISHED camera-relative
//        walk axes (ControlsGlobals::publishedA/B, written by moveSlot's own
//        readAxes() step 7 THIS frame), bucketed with the SAME precedence
//        rule row 8 itself describes (forward = none of the other three):
//          left  = publishedA < -eps      right = publishedA >  eps
//          back  = publishedB < -eps      forward = !(left || right || back)
//        (eps = 1e-4, matching readAxes' own centred threshold).
//   12   backpedal bool: `+0xd40` bit 0x2000 - read directly from
//        world.movementFlagsD40 (the SAME already-OPEN field, exactly as
//        specced; stays false unless a caller seeds it, unlike 8-11 above
//        where that would be useless).
//   13   alternate strafe bool: `+0xd40` bit 0x0200 - same as row 12.
//   14   walk bool: st.b1d94 & stance::kWalk (existing stance bit, reused
//        per the task's own instruction).
//   15   run bool: no dedicated stance bit exists for "run" (only kWalk/
//        kSprint do) - CHOSEN: st.locomotionState == kLocoRun (+0xccc == 2,
//        Sec5.5-owned state, not OPEN).
//   16   sprint bool: st.b1d94 & stance::kSprint (reused).
//   17   crouch bool: st.crouched (existing field, reused).
//   18   aim bool: AnimParamWorld::aimBool (OPEN, false in common case).
//   19   fine aim bool: st.fineAim (existing field, reused - same idiom as
//        row 17).
//   20   current speed: prevSpeedD1c - ONE FRAME OLD (see row 7).
//   21a/21b (combat ready) direction / desired direction: genuinely
//        unimplemented (needs 0x009ba300 and a different heading pair) -
//        throws OPEN if AnimParamWorld::combatReady is ever set true.
//   21c  direction (not combat ready, the only reachable branch in common
//        case): WrapAngle(HeadingFromForward(D) - HeadingFromForward(F)),
//        F = world.orientation.forward (the "+0x64" facing axis) - reuses
//        sr3player::camera.h's own HeadingFromForward/WrapAngle rather than
//        inventing a second heading convention.
//   22   Free Fall Vert Control: world.freeFall (reused) ? OPEN (+0xaf4 not
//        modelled) : 0.0f.
//   23   Parachute Open Bool: AnimParamWorld::parachuteOpen (OPEN, false).
//   24   Speed Along rvec: Dot(world.orientation.right, prevVelocity1cec) -
//        ONE FRAME OLD (spec-player-runtime.md Sec5.3 row 24: "resolves
//        Sec4.2's HYPOTHESIS... last frame's value, since 0x0099d1b0 runs
//        after 0x00958690"). NOTE: this is `+0x1cec`, i.e.
//        st.controllerVelocity1cec - NOT st.desiredSpeedA60/facingA50
//        (those are a different, already-smoothed quantity, Sec5.3's own
//        agreement-row distinction).
//   25   turn angle: st.turnAngleA68 - always 0 in this project's scope
//        (movement.h: "only the step-3 zero is modelled"; spec's own OPEN 37
//        leaves the real writer unresolved) - reused, not re-invented.
//   26-27 Aim Pitch / Aim Heading: AnimParamWorld::aimPitch/aimHeading
//        (OPEN, 0.0 in common case).
//   28   fire bool: AnimParamWorld::fireBool (OPEN, false).
//   29   Weapon Sprint Bool: sprinting (locomotionState == kLocoSprint, not
//        riding) ? bit 0x20 of st.b1d97 : false - that bit's own writer is
//        "sprint style... animation only, not kept" (movement.h step 16),
//        so this is honestly always false in this project, via the SAME
//        documented gap, not a new one.
//   30   vehicle surf lean: AnimParamWorld::vehicleSurfLean (OPEN, 0.0).
//   31   Reloading Bool: AnimParamWorld::reloading (OPEN, false).
//
// PLUS one name outside Sec4.2's table entirely: `locomotion`. Direct
// inspection of the real `player_state_machine` graph (spec-anim-runtime.md
// Sec7.13; the real name has a space - "player state machine" - the row
// file is `player_state_machine.xtbl`) shows its OWN top-level idle<->
// locomotion gate is a `locomotion`-parameter condition, and `locomotion` is
// exactly one of Sec7.13's ten "referenced by a graph, no compiled-in
// writer" names - nothing in the real Sec4.2 census ever writes it. CHOSEN
// (this unit's own judgement call, not a recovered engine fact): write it
// from `st.locomotionState` (+0xccc, Sec5.5-owned - kLocoIdle/Walk/Run/
// Sprint, 0..3) - the SAME concept under the SAME name, just owned by the
// locomotion bridge rather than the human writer. Type-written per the
// registry's own declared type for the name (bool/int/float), since this
// name's real control_parameters.xtbl type was not independently confirmed
// here.
//
// `desired speed` is the ONLY row that needs world.ccd/world.orientation
// indirectly (through the unreachable-else-branch note); `direction` (21c)
// and `Speed Along rvec` (24) both need world.orientation seeded - the SAME
// precondition PlayerMovement::composeVelocity already has once root motion
// becomes non-zero (movement.h: "a caller with no root motion need not seed
// R"), not a new one this bridge introduces.
struct AnimParamSnapshot {
    Vec3 velocity1cec;   // the PREVIOUS frame's st.controllerVelocity1cec (+0x1cec)
    Vec3 velocityD04;    // the PREVIOUS frame's st.velocityD04 (+0xd04/+0xd08/+0xd0c)
    double speedD1c = 0;  // the PREVIOUS frame's st.speedD1c (+0xd1c)
};
void WriteHumanWriterParams(sr3animruntime::GraphInstance& row, const sr3animruntime::GraphAliasTable& aliases,
                             const sr3animruntime::ControlParameterRegistry& registry, const sr3player::PlayerMovement& pm,
                             const AnimParamWorld& animWorld, const AnimParamSnapshot& prev);

// ---------------------------------------------------------------------------
// The root-motion seam (see the file banner's "WHAT T/E CANNOT BE"). OPEN by
// default: this project has no keyframe/track sampler for any `.anim_pc`
// clip. Translation is per SECOND, in the clip's own root-local frame, at
// clip rate 1.0 - the post step scales it by this step's `dt` itself.
class ClipRootMotionSource {
public:
    virtual ~ClipRootMotionSource() = default;
    virtual Vec3 translationPerSecond(int32_t record, bool loop) = 0;
};
class OpenClipRootMotionSource : public ClipRootMotionSource {
public:
    Vec3 translationPerSecond(int32_t record, bool loop) override;
};

// One graph-output entry turned into the smallest honest "layer" (see the
// file banner's CHOSEN SIMPLIFICATION). Not a real engine object - no
// instance, no handle, no pose.
struct PostedClip {
    int32_t record = -1;
    float weight = 0.0f;    // post the 0.0001 drop threshold (Sec8.9 item 6); slot weight CHOSEN = 1.0
    bool loop = false;       // the clip's own loop bit - this unit's proxy for layer flag 0x01
    float clipTime = 0.0f;   // non-looping only (0 for a looping entry, matching OutputEntry's own convention)
    float duration = 0.0f;   // services.recordDuration(record)
};
struct PostStepResult {
    std::vector<PostedClip> clips;
    Vec3 T;              // un-normalised, phase-synchronised-proxy (looping) sum - spec-player-runtime.md Sec5.4.3 step 3
    Vec3 E;              // weight-normalised non-looping sum
    bool hadAnyOutput = false;
};
// spec-anim-runtime.md Sec8.9 items 3-6 + spec-player-runtime.md Sec5.4.3
// "Step 3 in detail"'s T/E split, with the loop-bit proxy in place of the
// real layer flag. `dt` turns `motion`'s per-second rate into this step's
// delta (CHOSEN: a plain Euler step, the same fidelity as every other
// per-frame integration in this project).
PostStepResult PostOutputList(const sr3animruntime::GraphOutputList& out, sr3animruntime::GraphRuntimeServices& services,
                               ClipRootMotionSource& motion, double dt);

// ---------------------------------------------------------------------------
// The playback rate (spec-player-runtime.md Sec5.4.3 step 7 / stride
// matching), clamped to [0.1, 1.5]. CHOSEN construction value 1.0: no spec
// gives +0x1a90's numeric construction value ("the human constructor,
// displacement sweep" is HIGH, not CONFIRMED numeric) - 1.0 matches this
// project's own established "no spec -> neutral default" convention
// (e.g. GraphInstance::rate40, PlayerMovementState::speedScaleA64).
class PlaybackRateTracker {
public:
    double rate() const { return rate_; }
    // `desiredSpeed` = THIS frame's st.desiredSpeedA60 (the composer's own
    // `s`, Sec5.4.3 step 4 - NOT the `desired speed` CONTROL PARAMETER,
    // which is a different, unsmoothed quantity per Sec5.3's own agreement
    // row). `tMagnitude` = Length(PostStepResult::T) from the SAME frame's
    // post step. CHOSEN: holds its previous value when desiredSpeed <= 1e-4
    // or dt <= 0 - the spec's formula is explicitly conditioned on
    // "s > 1e-4" and does not describe a write for the complement; holding
    // is the minimal-invention reading of "no write this frame" (documented
    // in results per this task's own judgement-call requirement).
    void update(double desiredSpeed, double tMagnitude, double dt);

private:
    double rate_ = 1.0;
};

// ---------------------------------------------------------------------------
// A plain, injectable adapter over GraphRuntimeServices (node_graph_runtime.h)
// for driving ONE graph instance with no Controller/LayerApi/AnimDataSource
// behind it (this unit's whole point is to not need the controller's slot
// array for a single continuously-running graph). Each hook left unset
// keeps GraphRuntimeServices' own OPEN-throwing default.
class PlayerGraphServices : public sr3animruntime::GraphRuntimeServices {
public:
    std::function<int32_t(int32_t tagIndex)> tagToRecord;
    std::function<int32_t(const std::string&)> clipNameToRecord;
    std::function<float(int32_t record)> duration;

    int32_t recordForTag(const sr3animruntime::GraphInstance& row, int32_t tagIndex) override;
    int32_t recordForClipName(const std::string& clipName) override;
    float recordDuration(int32_t record) override;
};

// ---------------------------------------------------------------------------
// The orchestrator: one player, one continuously-running graph instance.
// ---------------------------------------------------------------------------
class PlayerAnimBridge {
public:
    // `graphIndex`/`graphDef` identify the already-loaded graph (by name,
    // e.g. "player_state_machine", spec-anim-runtime.md Sec7.13) to drive;
    // `instanceHandle` is a CHOSEN arbitrary bookkeeping id (this project has
    // no animation-instance object for it to mean anything beyond identity -
    // it is never read back). Allocates one row from `pool` immediately
    // (GraphInstancePool::instantiate), animGroupTable30 = 1 (CHOSEN: the
    // real anim-group-table copy this unlocks, Sec6.10, needs a Controller
    // environment this unit does not have; 1 is a non-zero sentinel, nothing
    // else reads its value) and rate40 left at its instancing default (1.0).
    PlayerAnimBridge(const sr3animruntime::NodeGraphLibrary& library, int32_t graphIndex,
                     const sr3animruntime::ControlParameterRegistry& registry, sr3animruntime::GraphInstancePool& pool,
                     sr3animruntime::GraphRuntimeServices& services, ClipRootMotionSource& motion,
                     const sr3animruntime::ControlFilterTable& filters, int32_t instanceHandle = 1);
    PlayerAnimBridge(const PlayerAnimBridge&) = delete;
    PlayerAnimBridge& operator=(const PlayerAnimBridge&) = delete;

    AnimParamWorld animWorld;

    // One full frame, in the real engine's own order (spec-player-runtime.md
    // Sec5.1): controls -> [parameter write + graph eval, both passes] ->
    // compose -> integrate -> publish. Calls PlayerMovement's INDIVIDUAL
    // phases (not runFrame()) so the graph's own E/T can feed the SAME
    // frame's composer - see the file banner. `pm.world.orientation` MUST be
    // seeded before calling this (same precondition composeVelocity already
    // has once root motion is non-zero).
    void stepFrame(sr3player::PlayerMovement& pm, const sr3player::MoveFrameInput& baseFrame);

    const PostStepResult& lastPost() const { return lastPost_; }
    double playbackRate() const { return rateTracker_.rate(); }
    const sr3animruntime::GraphInstance& graphInstance() const;
    const sr3animruntime::GraphOutputList& lastOutput() const { return lastOutput_; }

private:
    sr3animruntime::GraphInstancePool& pool_;
    int32_t row_ = -1;
    const sr3animruntime::NodeGraphLibrary& lib_;
    int32_t graphIndex_;
    const sr3animruntime::ControlParameterRegistry& registry_;
    sr3animruntime::GraphRuntimeServices& services_;
    ClipRootMotionSource& motion_;
    sr3animruntime::NodeGraphRuntime runtime_;
    PlaybackRateTracker rateTracker_;
    PostStepResult lastPost_;
    sr3animruntime::GraphOutputList lastOutput_;
};

// ---------------------------------------------------------------------------
// Camera hookup (do this LAST - the camera already exists, U4). Populates
// CameraSystem::target from the character's own current-frame state: just
// field assignment, no new behaviour. `facingFlat` should be a horizontal
// (y = 0) forward direction - OrientationFromFacing() below builds a full
// right/up/forward triple from it via sr3player::RotationFromAngles (the
// SAME convention camera.h's own rotations use, so the camera and the
// character agree on handedness) rather than inventing a second one.
sr3player::Orientation OrientationFromFacing(Vec3 facingFlat);
void DriveCameraTarget(sr3player::CameraSystem& cam, const sr3player::PlayerMovement& pm, uint64_t targetId = 1);

}  // namespace sr3playeranim
