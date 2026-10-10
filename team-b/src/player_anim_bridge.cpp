// sr3playeranim - the player<->animation coupling. See
// include/sr3playeranim/bridge.h for the full scope/citation banner; this
// file is the implementation of exactly what that header declares.
#include "sr3playeranim/bridge.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace sr3playeranim {

using namespace sr3animruntime;
using sr3player::Dot;
using sr3player::Length;
using sr3player::MoveFrameInput;
using sr3player::Orientation;
using sr3player::PlayerMovement;

// ---------------------------------------------------------------------------
// Common-case seeding
// ---------------------------------------------------------------------------
void SeedPlayerAnimCommonCase(AnimParamWorld& w) {
    w.combatReady.set(false);
    w.aimBool.set(false);
    w.fireBool.set(false);
    w.parachuteOpen.set(false);
    w.freeFallVertControl.set(0.0f);
    w.aimPitch.set(0.0f);
    w.aimHeading.set(0.0f);
    w.vehicleSurfLean.set(0.0f);
    w.reloading.set(false);
}

// ---------------------------------------------------------------------------
// The parameter writer
// ---------------------------------------------------------------------------
namespace {

// Writable only if `graph` aliases `name` (control_parameters.h: "a name is
// therefore writable this way only if the target object's own graph aliases
// it") - silently skipped otherwise, matching the real by-name writers.
int16_t ResolveAlias(const GraphAliasTable& aliases, const ControlParameterRegistry& registry, const char* name) {
    const int16_t global = registry.resolve(name);
    if (global < 0) return -1;
    return aliases.globalToAlias(global);
}
void SetBoolParam(GraphInstance& row, const GraphAliasTable& aliases, const ControlParameterRegistry& registry,
                   const char* name, bool v) {
    const int16_t alias = ResolveAlias(aliases, registry, name);
    if (alias < 0) return;
    row.values[static_cast<size_t>(alias)].dword0 = v ? 1u : 0u;
}
void SetFloatParam(GraphInstance& row, const GraphAliasTable& aliases, const ControlParameterRegistry& registry,
                    const char* name, float v) {
    const int16_t alias = ResolveAlias(aliases, registry, name);
    if (alias < 0) return;
    row.values[static_cast<size_t>(alias)].dword0 = FloatBits(v);
}
// `locomotion`: not one of the 62 compiled-in names (no known type from
// that table), so this writes per the REGISTRY's own declared type for
// the name rather than assuming - see bridge.h's own comment on this row.
void SetIntTypedParam(GraphInstance& row, const GraphAliasTable& aliases, const ControlParameterRegistry& registry,
                       const char* name, int32_t v) {
    const int16_t global = registry.resolve(name);
    if (global < 0) return;
    const int16_t alias = aliases.globalToAlias(global);
    if (alias < 0) return;
    uint32_t bits = static_cast<uint32_t>(v);
    if (const ParamRecord* rec = registry.record(global)) {
        if (rec->type == ParamType::Float) bits = FloatBits(static_cast<float>(v));
        else if (rec->type == ParamType::Bool) bits = (v != 0) ? 1u : 0u;
        // Integer or None (no declared type observed): raw value as-is.
    }
    row.values[static_cast<size_t>(alias)].dword0 = bits;
}

}  // namespace

void WriteHumanWriterParams(GraphInstance& row, const GraphAliasTable& aliases, const ControlParameterRegistry& registry,
                             const PlayerMovement& pm, const AnimParamWorld& animWorld, const AnimParamSnapshot& prev) {
    const auto& st = pm.st;
    const auto& world = pm.world;
    const auto& g = pm.g;

    // Rows 1-2 / the climb writer: +0xccd-gated; never reached in this
    // project's on-foot scope (world.ccd stays 0 - SeedOnFootCommonCase).
    // Genuinely unimplemented if a caller ever does drive ccd there.
    const int32_t ccd = world.ccd.get();
    if (ccd == 4) {
        throw OpenStateError(
            "climb/dive writer 0x009896d0 and Jump Dive Bool/Offhand Grenade Bool (+0xccd == 4)",
            "spec-anim-runtime.md Sec4.2 rows 1-2");
    }
    if (ccd == 5) {
        throw OpenStateError("climb writer 0x009896d0 (+0xccd == 5)", "spec-anim-runtime.md Sec4.2 (climb writer row)");
    }

    SetBoolParam(row, aliases, registry, "dead", world.dead.get());                              // row 3
    SetBoolParam(row, aliases, registry, "combat ready", animWorld.combatReady.get());            // row 4
    SetBoolParam(row, aliases, registry, "Supported By Ground", world.supportedByGround.get());   // row 5

    // row 6: CONFIRMED local-player branch (spec-player-runtime.md Sec5.3 row 6).
    SetFloatParam(row, aliases, registry, "desired speed", static_cast<float>(Length(st.desiredMove)));

    SetFloatParam(row, aliases, registry, "Vertical Speed", static_cast<float>(prev.velocityD04.y));  // row 7

    // rows 8-11: CHOSEN deviation from +0xd40 (OPEN 38) - see bridge.h's own
    // WriteHumanWriterParams() comment for why.
    constexpr double kAxisEps = 1e-4;
    const bool left = g.publishedA < -kAxisEps;
    const bool right = g.publishedA > kAxisEps;
    const bool back = g.publishedB < -kAxisEps;
    const bool forward = !(left || right || back);
    SetBoolParam(row, aliases, registry, "move forward bool", forward);
    SetBoolParam(row, aliases, registry, "move left bool", left);
    SetBoolParam(row, aliases, registry, "move right bool", right);
    SetBoolParam(row, aliases, registry, "move back bool", back);

    const uint32_t d40 = world.movementFlagsD40.get();
    SetBoolParam(row, aliases, registry, "backpedal bool", (d40 & 0x2000u) != 0);          // row 12
    SetBoolParam(row, aliases, registry, "alternate strafe bool", (d40 & 0x0200u) != 0);   // row 13

    SetBoolParam(row, aliases, registry, "walk bool", (st.b1d94 & sr3player::stance::kWalk) != 0);      // row 14
    SetBoolParam(row, aliases, registry, "run bool", st.locomotionState == sr3player::kLocoRun);        // row 15
    SetBoolParam(row, aliases, registry, "sprint bool", (st.b1d94 & sr3player::stance::kSprint) != 0);  // row 16
    SetBoolParam(row, aliases, registry, "crouch bool", st.crouched);                                    // row 17
    SetBoolParam(row, aliases, registry, "aim bool", animWorld.aimBool.get());                           // row 18
    SetBoolParam(row, aliases, registry, "fine aim bool", st.fineAim);                                   // row 19

    SetFloatParam(row, aliases, registry, "current speed", static_cast<float>(prev.speedD1c));  // row 20

    if (animWorld.combatReady.get()) {
        throw OpenStateError("direction/desired direction, combat-ready branch (0x009ba300, rows 21a/21b)",
                             "spec-anim-runtime.md Sec4.2 rows 21a/21b");
    }
    // row 21c (the only reachable branch in this project's scope).
    {
        const double headingD = sr3player::HeadingFromForward(st.desiredMove);
        const double headingF = sr3player::HeadingFromForward(world.orientation.get().forward);
        SetFloatParam(row, aliases, registry, "direction",
                      static_cast<float>(sr3player::WrapAngle(headingD - headingF)));
    }

    if (world.freeFall.get()) {
        throw OpenStateError("Free Fall Vert Control +0xaf4", "spec-anim-runtime.md Sec4.2 row 22");
    }
    SetFloatParam(row, aliases, registry, "Free Fall Vert Control", 0.0f);  // row 22

    SetBoolParam(row, aliases, registry, "Parachute Open Bool", animWorld.parachuteOpen.get());  // row 23

    SetFloatParam(row, aliases, registry, "Speed Along rvec",
                  static_cast<float>(Dot(world.orientation.get().right, prev.velocity1cec)));  // row 24

    SetFloatParam(row, aliases, registry, "turn angle", static_cast<float>(st.turnAngleA68));  // row 25

    SetFloatParam(row, aliases, registry, "Aim Pitch", animWorld.aimPitch.get());       // row 26
    SetFloatParam(row, aliases, registry, "Aim Heading", animWorld.aimHeading.get());   // row 27
    SetBoolParam(row, aliases, registry, "fire bool", animWorld.fireBool.get());        // row 28

    const bool sprintingNotRiding = st.locomotionState == sr3player::kLocoSprint && !world.riding.get();
    SetBoolParam(row, aliases, registry, "Weapon Sprint Bool", sprintingNotRiding && (st.b1d97 & 0x20) != 0);  // row 29

    SetFloatParam(row, aliases, registry, "vehicle surf lean", animWorld.vehicleSurfLean.get());  // row 30
    SetBoolParam(row, aliases, registry, "Reloading Bool", animWorld.reloading.get());             // row 31

    // Outside Sec4.2's table: `locomotion`, the real player_state_machine
    // graph's own idle<->moving gate (see bridge.h's comment on this row).
    SetIntTypedParam(row, aliases, registry, "locomotion", st.locomotionState);
}

// ---------------------------------------------------------------------------
// Root motion seam
// ---------------------------------------------------------------------------
Vec3 OpenClipRootMotionSource::translationPerSecond(int32_t record, bool) {
    throw OpenStateError("per-clip root motion (record " + std::to_string(record) + ") - no keyframe/track sampler",
                         "spec-player-runtime.md Sec5.4.3 step 3 (sr3anim does not decode the .anim_pc payload)");
}

// ---------------------------------------------------------------------------
// The post step
// ---------------------------------------------------------------------------
PostStepResult PostOutputList(const GraphOutputList& out, GraphRuntimeServices& services, ClipRootMotionSource& motion,
                               double dt) {
    PostStepResult r;
    Vec3 tSum{};
    Vec3 eSum{};
    double eWeight = 0.0;
    for (const OutputEntry& e : out.entries()) {
        // CHOSEN: slot weight = 1.0 (a single continuously-running graph
        // instance has no cross-fade slot array to scale by - see bridge.h).
        const float w = e.weight;
        const bool zeroedInTransition = (e.flags & kOutZeroedInTransition) != 0;
        if (w < 0.0001f && !zeroedInTransition) continue;  // Sec8.9 item 6
        r.hadAnyOutput = true;

        const bool loop = (e.flags & kOutLoop) != 0;
        PostedClip pc;
        pc.record = e.record;
        pc.weight = w;
        pc.loop = loop;
        pc.clipTime = loop ? 0.0f : e.clipTime;
        pc.duration = services.recordDuration(e.record);
        r.clips.push_back(pc);

        const Vec3 delta = motion.translationPerSecond(e.record, loop) * dt;
        if (loop) {
            tSum = tSum + delta * static_cast<double>(w);  // un-normalised
        } else {
            eSum = eSum + delta * static_cast<double>(w);
            eWeight += w;
        }
    }
    r.T = tSum;
    r.E = (eWeight > 0.0) ? eSum * (1.0 / eWeight) : Vec3{};
    return r;
}

// ---------------------------------------------------------------------------
// Playback rate
// ---------------------------------------------------------------------------
void PlaybackRateTracker::update(double desiredSpeed, double tMagnitude, double dt) {
    if (!(desiredSpeed > 1e-4) || !(dt > 0.0)) return;  // CHOSEN: hold - see bridge.h
    const double q = desiredSpeed / (tMagnitude / dt + 1e-4);
    const bool below1 = rate_ < 1.0;
    if (rate_ < q) {
        const double rise = below1 ? 2.0 * dt : dt;
        rate_ = std::min(q, rate_ + rise);
    } else if (rate_ > q) {
        double fall = below1 ? 2.8 * dt : 1.4 * dt;
        if (rate_ > 2.0 * q) fall += dt;
        rate_ = std::max(q, rate_ - fall);
    }
    rate_ = std::clamp(rate_, 0.1, 1.5);
}

// ---------------------------------------------------------------------------
// PlayerGraphServices
// ---------------------------------------------------------------------------
int32_t PlayerGraphServices::recordForTag(const GraphInstance& row, int32_t tagIndex) {
    if (tagToRecord) return tagToRecord(tagIndex);
    return GraphRuntimeServices::recordForTag(row, tagIndex);
}
int32_t PlayerGraphServices::recordForClipName(const std::string& clipName) {
    if (clipNameToRecord) return clipNameToRecord(clipName);
    return GraphRuntimeServices::recordForClipName(clipName);
}
float PlayerGraphServices::recordDuration(int32_t record) {
    if (duration) return duration(record);
    return GraphRuntimeServices::recordDuration(record);
}

// ---------------------------------------------------------------------------
// PlayerAnimBridge
// ---------------------------------------------------------------------------
PlayerAnimBridge::PlayerAnimBridge(const NodeGraphLibrary& library, int32_t graphIndex,
                                   const ControlParameterRegistry& registry, GraphInstancePool& pool,
                                   GraphRuntimeServices& services, ClipRootMotionSource& motion,
                                   const ControlFilterTable& filters, int32_t instanceHandle)
    : pool_(pool), lib_(library), graphIndex_(graphIndex), registry_(registry), services_(services), motion_(motion),
      runtime_(library, registry, filters) {
    const GraphDef* def = lib_.graph(graphIndex_);
    if (!def) throw std::out_of_range("PlayerAnimBridge: graph index out of range");
    row_ = pool_.instantiate(graphIndex_, instanceHandle, &def->aliases, registry_);
    GraphInstance* r = pool_.row(row_);
    // CHOSEN non-zero sentinel: Sec6.10 says evaluation refuses while this is
    // 0 ("the copy" that normally sets it needs a Controller environment
    // this unit does not have). Nothing else reads its value.
    r->animGroupTable30 = 1;
    SeedPlayerAnimCommonCase(animWorld);
}

void PlayerAnimBridge::stepFrame(PlayerMovement& pm, const MoveFrameInput& baseFrame) {
    // 1. Controls - mirrors PlayerMovement::runFrame()'s own early section
    //    (src/player_locomotion.cpp) exactly; called individually (not
    //    through runFrame()) so the graph's own E can feed THIS frame's
    //    composer, same as the real engine (see bridge.h's file banner).
    if (pm.world.readControlsDisabled()) {
        pm.writeNoIntent(baseFrame);
    } else {
        pm.crouchRoutine(baseFrame);
        pm.sprintRoutine(baseFrame);
        if (baseFrame.controlsRanThisFrame) pm.moveSlot(baseFrame);
    }

    // 2. "Last frame's" published state (spec-player-runtime.md Sec5.9 item
    //    4) - this frame's compose/integrate/publish have not run yet, so
    //    these three fields still hold whatever the PREVIOUS stepFrame()
    //    call (or construction) last published.
    AnimParamSnapshot prev;
    prev.velocity1cec = pm.st.controllerVelocity1cec;
    prev.velocityD04 = pm.st.velocityD04;
    prev.speedD1c = pm.st.speedD1c;

    // 3. Parameter write + graph evaluation (both passes).
    GraphInstance* row = pool_.row(row_);
    WriteHumanWriterParams(*row, lib_.graph(graphIndex_)->aliases, registry_, pm, animWorld, prev);

    row->timeStep3c = static_cast<float>(baseFrame.dt) * row->rate40;  // Sec6.10 step 3 (rate40 == 1.0 here)
    lastOutput_.clear();
    runtime_.evaluate(*row, 0, lastOutput_, services_);
    runtime_.evaluate(*row, 1, lastOutput_, services_);

    // 4. The (scoped-down) post step.
    lastPost_ = PostOutputList(lastOutput_, services_, motion_, baseFrame.dt);
    rateTracker_.update(pm.st.desiredSpeedA60, Length(lastPost_.T), baseFrame.dt);

    // 5. Compose / integrate / publish, THIS frame's E feeding THIS frame's
    //    composer (same-frame, exactly like 0x0099d1b0 consuming
    //    0x004c58c0's output).
    MoveFrameInput f2 = baseFrame;
    f2.animationTranslationE = lastPost_.E;
    pm.composeVelocity(f2);
    pm.integrate(f2);
    pm.publish();
}

const GraphInstance& PlayerAnimBridge::graphInstance() const { return *pool_.row(row_); }

// ---------------------------------------------------------------------------
// Camera hookup
// ---------------------------------------------------------------------------
Orientation OrientationFromFacing(Vec3 facingFlat) {
    const double heading = sr3player::HeadingFromForward(facingFlat);
    const sr3player::CameraRotation rot = sr3player::RotationFromAngles(0.0, 0.0, heading);
    return Orientation{rot.right, rot.up, rot.forward};
}

void DriveCameraTarget(sr3player::CameraSystem& cam, const PlayerMovement& pm, uint64_t targetId) {
    cam.target.valid = true;
    cam.target.id = targetId;
    cam.target.isVehicle = false;
    cam.target.isHuman = true;
    cam.target.referencePoint = pm.st.position;
    const Orientation ori = pm.world.orientation.get();
    cam.target.orientation = ori;
    cam.target.heading = sr3player::HeadingFromForward(ori.forward);
}

}  // namespace sr3playeranim
