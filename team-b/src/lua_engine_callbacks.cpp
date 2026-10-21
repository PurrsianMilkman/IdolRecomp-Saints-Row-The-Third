// Engine -> Lua callback firing. See include/sr3luahost/engine_callbacks.h and
// spec-lua-api-behaviour.md, "Engine callback dispatch: death and destruction
// (2026-10-14)" (A-H) and "Engine callback dispatch: vehicle entry/exit, phone
// calls, triggers, group vehicles (2026-10-14)" (V.0-V.5), as finalised by
// spec syncs bfe0542 and 98375ea (the e903f59 draft is superseded).
//
// What is implemented here, per spec section:
//  A   fireEngineCallback: deferred script thread, silent loss on missing
//      global / full table; persistent registrations (nothing here clears a slot).
//  B   tickCharacterDeaths: the per-human-update finalizer. on_human_killed(team,
//      killer, victim) first, then on_death(victim, killer), then the latch.
//      recordDamageAttacker: killer = attacker of the latest application,
//      frozen once the victim is dying.
//  C   on_human_killed global registration (the native stores the callback name).
//  D   notifyVehicleDestroyed: vehicle slot 0 else player slot 0x12; before the
//      occupants die. (The 10 s sinking path needs a water/depth model and the
//      activity-routine path 0x0069f8e0 is OPEN in the spec; neither is modelled.)
//  E   notifyMoverDestroyed: callable; no producer is wired (whether
//      mesh_mover_kill reaches the mover damage handler is OPEN in the spec).
//  V.1 commitVehicleSeat / commitVehicleExit: vehicle hook, then occupant hook,
//      three arguments (occupant_name, vehicle_name, seat), seat 0 = driver.
//  V.3 mission cellphone call lifecycle: cb1 on answer, cb2 on end, removal never
//      fires cb2. The dialogue duration source is OPEN in the spec, so nothing
//      here decides when a call ends; the caller (engine event or harness) does.
//  V.4 trigger enable/disable transitions (the overlap pass lives in the viewer).
#include "sr3luahost/engine_state.h"

#include <algorithm>

#include "sr3weapons/damage_model.h"

namespace sr3luahost {

namespace {

bool isPlayerName(const std::string& n) { return n.rfind("#PLAYER", 0) == 0; }

// "#PLAYER#" is the generic player sentinel; the local player object is "#PLAYER1#". Registrations made with
// either spelling land on the local player's hook record.
const char* const kLocalPlayer = "#PLAYER1#";
const char* const kGenericPlayer = "#PLAYER#";

constexpr size_t kEngineCallbackLogCap = 8192;
constexpr double kPlayerRespawnDelaySeconds = 4.0;  // respawn_delay_ms = 4000 (spec B, 2026-10-14 RESOLVED)

// The name lookup is case-insensitive (spec 0x004588f0), but this project's maps are keyed by the exact string a
// script used. A hook registered as "Start_Truck" must still fire for an event on "start_truck": take the exact
// entry's value when non-empty, else the first case-folded peer's.
template <class Map, class Getter>
std::string foldedHook(const Map& m, const std::string& name, Getter get) {
    auto it = m.find(name);
    if (it != m.end()) {
        std::string s = get(it->second);
        if (!s.empty()) return s;
    }
    const std::string lower = AsciiLowerFold(name);
    for (const auto& kv : m) {
        if (kv.first == name || AsciiLowerFold(kv.first) != lower) continue;
        std::string s = get(kv.second);
        if (!s.empty()) return s;
    }
    return {};
}

// A player hook: the local player's record, else the generic-sentinel record.
template <class Map, class Getter>
std::string playerHook(const Map& m, const std::string& name, Getter get) {
    std::string s = foldedHook(m, name, get);
    if (s.empty()) s = foldedHook(m, name == kLocalPlayer ? kGenericPlayer : kLocalPlayer, get);
    return s;
}

}  // namespace

bool EngineState::fireEngineCallback(const std::string& callbackName, std::vector<CallbackArg> args, const char* source) {
    if (callbackName.empty()) return false;  // an empty slot: nothing to fire
    ++engineCallbackCounters_.fired;
    PendingCallback cb;
    cb.name = callbackName;
    cb.args = std::move(args);
    cb.source = source ? source : "";
    bool accepted;
    if (callbackSpawner_) {
        accepted = callbackSpawner_(cb);
    } else {
        pendingEngineCallbacks_.push_back(cb);
        accepted = true;
    }
    if (accepted) ++engineCallbackCounters_.accepted; else ++engineCallbackCounters_.lost;
    if (engineCallbackLog_.size() < kEngineCallbackLogCap) engineCallbackLog_.push_back({std::move(cb), accepted});
    return accepted;
}

bool VehicleState::seat0HoldsCharacter(const std::string& character) const {
    if (character.empty()) return false;
    const std::string want = AsciiLowerFold(character);
    if (!seat0Claim.empty() && AsciiLowerFold(seat0Claim) == want) return true;
    if (!seat0Pending.empty() && AsciiLowerFold(seat0Pending) == want) return true;
    auto it = seatOccupants.find(0);
    return it != seatOccupants.end() && !it->second.empty() && AsciiLowerFold(it->second) == want;
}

bool EngineState::characterReadsDead(const CharacterState& c) {
    if (c.isDeadHighConfidence) return true;
    if (c.lifeState.known() && c.lifeState.get() == 5) return true;
    if (c.killFinalizedFlagE3.known() && c.killFinalizedFlagE3.get()) return true;
    return false;
}

void EngineState::recordDamageAttacker(const std::string& victimName, const std::string& attackerName) {
    CharacterState& c = getOrCreateCharacter(victimName);
    if (characterReadsDead(c)) return;  // once the dying bit is set the sink bails out: later hits cannot overwrite
    c.lastAttackerName = attackerName;
}

void EngineState::armPlayerDeathTimer(const std::string& name) {
    if (!isPlayerName(name)) return;
    CharacterState& c = getOrCreateCharacter(name);
    if (c.playerDeathTimerArmedAtSeconds < 0.0) c.playerDeathTimerArmedAtSeconds = syncedActionNowSeconds();
}

// Design notes moved verbatim from EngineState::tickScriptedOrders's declaration in engine_state.h
// (refactor step 3a):
// Scripted-order mode tick (spec-ai-runtime.md Sec22.11, mode 5 action 24 `move scripted`), run from the
// per-human update above. For every character whose kind-3 ticket is pending on a scripted move: the order
// completes on the first update where the character is within the 1.414 m box test of the destination
// (ScriptedMoveArrived; point objects), otherwise it moves toward the destination in a straight line at a
// CHOSEN walking/running pace (the locomotion layer is animation driven and not specified; there is no navmesh
// movement in this host). Nothing to measure (an unknown position) completes at once, as before the ticket
// existed; a character that is dead drops its order (CHOSEN: no AI is left to retry it).
void EngineState::tickScriptedOrders() {
    const double now = syncedActionNowSeconds();
    double dt = lastScriptedOrderTickSeconds_ < 0.0 ? 0.0 : now - lastScriptedOrderTickSeconds_;
    lastScriptedOrderTickSeconds_ = now;
    if (dt < 0.0) dt = 0.0;
    constexpr int kMoveScriptedAction = 24;  // `move scripted` (action table id), category mode 5 Combat Movement
    const size_t n = characterOrder_.size();
    for (size_t i = 0; i < n; ++i) {
        const std::string name = characterOrder_[i];
        auto it = characters_.find(name);
        if (it == characters_.end()) continue;
        CharacterState& c = it->second;
        if (!c.scriptedMoveActive) continue;
        if (!c.scriptedOrderTicket.pending()) {
            c.scriptedMoveActive = false;
            continue;
        }
        if (characterReadsDead(c)) {  // CHOSEN: a dead character's order is dropped, never left blocking a script
            c.scriptedOrderTicket.clear();
            c.scriptedMoveActive = false;
            continue;
        }
        const std::string dest = c.pendingScriptedMoveDestination;
        {
            // Sec22.11 port rule (AI-14): a destination that fails the gate (no longer resolves) or is dead skips arrival test
            // A and the order ENDS WITHOUT completing: the ticket stays pending (ai_scripted_action_is_complete stays false)
            // until a new order replaces it, and the AI decision step takes the character back. No timeout is invented.
            bool destGone = objectResolves().known(dest) && !objectResolves().get(dest);
            auto dit = characters_.find(dest);
            if (dit != characters_.end() && characterReadsDead(dit->second)) destGone = true;
            if (destGone) {
                c.scriptedMoveActive = false;
                continue;
            }
        }
        WorldPosition from{}, to{};
        bool haveFrom = tryResolveWorldPositionFromZoneData(name, c.worldPosition);
        if (haveFrom) from = c.worldPosition.get();
        bool haveTo = false;
        if (haveFrom) {
            if (hasVehicle(dest) && !hasCharacter(dest)) {
                VehicleState& v = getOrCreateVehicle(dest);
                haveTo = tryResolveWorldPositionFromZoneData(dest, v.worldPosition);
                if (haveTo) to = v.worldPosition.get();
            } else {
                CharacterState& d = getOrCreateCharacter(dest);  // same record get_dist reads for this name
                haveTo = tryResolveWorldPositionFromZoneData(dest, d.worldPosition);
                if (haveTo) to = d.worldPosition.get();
            }
        }
        // CHOSEN (labelled): the destination resolves and is alive per the host, but this host has no position for the
        // character or the destination, so the arrival test cannot be measured at all: complete (the pre-ticket behaviour).
        // The spec's failed-gate / dead-destination cases were handled above and do NOT complete.
        if (!haveFrom || !haveTo) {
            c.scriptedOrderTicket.finish(kMoveScriptedAction);
            c.scriptedMoveActive = false;
            continue;
        }
        const double dx = to.x - from.x, dy = to.y - from.y, dz = to.z - from.z;
        // Arrival test A, point-like destination: the box is a point, so the local offset is the world offset.
        if (sr3airuntime::combat::ScriptedMoveArrived(-dx, -dy, -dz, 0, 0, 0, 0, 0, 0)) {
            c.scriptedOrderTicket.finish(kMoveScriptedAction);
            c.scriptedMoveActive = false;
            continue;
        }
        const double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        const double step = std::min(dist, dt * (c.scriptedMoveRun ? kScriptedMoveRunMetresPerSecond
                                                                    : kScriptedMoveWalkMetresPerSecond));
        if (step > 0.0 && dist > 0.0)
            c.worldPosition.set(WorldPosition{from.x + dx / dist * step, from.y + dy / dist * step, from.z + dz / dist * step});
    }
}

// Design notes moved verbatim from EngineState::tickCharacterDeaths's declaration in engine_state.h
// (refactor step 3a):
// on_human_killed (spec C): ONE global registration (last call wins, "" clears); the argument is the
// callback NAME, not a filter.
// (storage and accessor: onHumanKilledCallback(), further down - set by the on_human_killed native.)

// The per-human update's death finalizer (spec B, FINAL): called once per frame BEFORE the script pump.
// For every character that is dead and whose death-callback latch is clear it queues, in character
// creation order: on_human_killed(team|nil, killer|nil, victim) (non-player NPCs, if registered), then
// on_death(victim, killer|nil) (if registered), then sets the latch. A character that is no longer dead
// (respawn / revive) has its latch cleared. Players: on_death only, the frame after death.
void EngineState::tickCharacterDeaths() {
    tickDownedExpiry();
    tickScriptedOrders();  // the mode tick of the per-human update (spec Sec22.11), before the death finalizer
    tickNpcAi();           // the per-NPC AI loop (no-op until a host calls enableNpcAi)
    // Index-list order of the per-human pass. The list can only grow through callbacks that create characters,
    // which this pass never runs, so a snapshot of the size is enough.
    const size_t n = characterOrder_.size();
    for (size_t i = 0; i < n; ++i) {
        const std::string name = characterOrder_[i];
        auto it = characters_.find(name);
        if (it == characters_.end()) continue;
        CharacterState& c = it->second;
        const bool dead = characterReadsDead(c);
        if (!dead) {
            c.deathCallbackLatch = false;  // respawn / revive clears the latch
            c.playerDeathTimerArmedAtSeconds = -1.0;  // ... and resets the player death timer
            continue;
        }
        if (c.deathCallbackLatch) continue;
        const bool player = isPlayerName(name);
        if (player) {
            // Spec B "Firing for players" (2026-10-14 RESOLVED): the finalizer arms a timer and the gameplay frame
            // update queues on_death once respawn_delay_ms (4000 ms, gameplay_constants.xtbl Death_and_busted) has
            // expired - about 4 s after the death, not on the next frame. The busted path (same delay) is not modelled.
            const double now = syncedActionNowSeconds();
            if (c.playerDeathTimerArmedAtSeconds < 0.0) {
                c.playerDeathTimerArmedAtSeconds = now;
                continue;
            }
            if (now - c.playerDeathTimerArmedAtSeconds < kPlayerRespawnDelaySeconds) continue;
        }
        const std::string killer = c.lastAttackerName;
        if (!player) {
            // (i) the global hook, for every locally-owned NPC that dies (named or ambient).
            if (!onHumanKilledCallback_.empty()) {
                fireEngineCallback(onHumanKilledCallback_,
                                   {CallbackArg::strOrNil(c.teamName), CallbackArg::strOrNil(killer), CallbackArg::str(name)},
                                   "on_human_killed");
            }
        }
        // (ii) slot 0 of the character's record: on_death(victim, killer|nil).
        const auto getDeath = [](const CharacterState& cs) { return cs.onDeathCallback; };
        const std::string cb = player ? playerHook(characters_, name, getDeath) : foldedHook(characters_, name, getDeath);
        fireEngineCallback(cb, {CallbackArg::str(name), CallbackArg::strOrNil(killer)}, "on_death");
        // (iii) the latch.
        c.deathCallbackLatch = true;
    }
}

void EngineState::fireCharacterHitCallbacks(const std::string& victimName, const std::string& attackerName,
                                            double oldFraction, double newFraction, uint32_t flags) {
    const bool player = isPlayerName(victimName);
    // on_take_damage (spec F): every application unless flag 0x200; before the death chain, so the lethal hit
    // still produces it with hitpoints <= 0. Player victims use the player slot, scripted NPCs the record slot.
    if (!(flags & 0x200u)) {
        const auto getTd = [](const CharacterState& cs) { return cs.onTakeDamageCallback; };
        const std::string cb = player ? playerHook(characters_, victimName, getTd) : foldedHook(characters_, victimName, getTd);
        fireEngineCallback(cb,
                           {CallbackArg::str(victimName), CallbackArg::strOrNil(attackerName), CallbackArg::num(newFraction),
                            CallbackArg::boolean_((flags & 0x8u) != 0), CallbackArg::boolean_((flags & 0x1u) != 0),
                            CallbackArg::boolean_((flags & 0x4000u) != 0)},
                           "on_take_damage");
    }
    // on_damage (spec F): no arguments, on the crossing from above the threshold T to at or below it in one
    // application. Whether flag 0x200 also suppresses it is OPEN in the spec; it is not suppressed here.
    const auto getOd = [](const CharacterState& cs) { return cs.onDamageCallback; };
    const std::string odCb = player ? playerHook(characters_, victimName, getOd) : foldedHook(characters_, victimName, getOd);
    if (!odCb.empty()) {
        double threshold = 0.0;
        auto it = characters_.find(victimName);
        if (it != characters_.end()) threshold = it->second.onDamageThreshold;
        if (oldFraction > threshold && newFraction <= threshold) fireEngineCallback(odCb, {}, "on_damage");
    }
}

EngineState::DamageModelResult EngineState::applyDamageModelHit(const std::string& victimName, const std::string& attackerName,
                                                                float amount, uint32_t flags) {
    DamageModelResult out;
    CharacterState& c = getOrCreateCharacter(victimName);
    const int32_t maxHp = drawOrGetMaxHitPoints(victimName);
    const int32_t curHp = drawOrGetCurrentHitPoints(victimName);
    if (!characterReadsDead(c) && c.lifeState.known() && c.lifeState.get() == 6) {
        // Sec21.10.1: a downed human does not take the normal HP path (the HP setter cannot kill it). Types 0/2/3/4 ignore
        // the hit; type 1 runs its overkill buffer. The amount is scaled by the player-attacker difficulty multiplier only.
        const double dmg = static_cast<double>(amount) *
                           (isPlayerName(attackerName) ? static_cast<double>(damageDealtDifficultyMultiplier()) : 1.0);
        recordDamageAttacker(victimName, attackerName);
        absorbDamageWhileDowned(c, victimName, dmg);
        out.newHp = curHp;
        out.downed = true;
        return out;
    }
    sr3weapons::SinkInput in;
    in.dmg = amount;
    in.flags = flags;
    in.victimDead = characterReadsDead(c);
    in.victimIsPlayer = isPlayerName(victimName) || victimName == "player";  // "player" = the free-roam playtest character (CHOSEN alias)
    in.victimFollowsLocalPlayer = !in.victimIsPlayer && isFollowerOfLocalPlayer(victimName);
    in.damageReceivedMult = damageReceivedDifficultyMultiplier();  // Sec18.3 item 4 (PV-6), applied inside ApplyDamageSink
    in.playerImmune = playerFullRestoreArmed(victimName);  // Sec18.13 / step 10d
    in.attackerIsPlayer = isPlayerName(attackerName);
    in.attackerIsVictim = !attackerName.empty() && attackerName == victimName;
    in.curHp = static_cast<float>(curHp);
    in.maxHp = static_cast<float>(maxHp);
    in.damageDealtMult = damageDealtDifficultyMultiplier();
    // Step 13: scripted invulnerability (+0x1c98 bit 0x20), with the F 0x100 exception resolved here.
    in.scriptedInvulnerable = c.forceFlags1c98.known(CharacterState::kInvulnerableBit1c98) &&
                              c.forceFlags1c98.get(CharacterState::kInvulnerableBit1c98) != 0 && !(flags & 0x100u);
    // Every other input keeps the spec default (burning, stunned, squad, avatar, operator and victim/attacker
    // multipliers): this project does not model those world facts.
    const sr3weapons::SinkResult r = sr3weapons::ApplyDamageSink(in);
    // Spec B: the attacker pair is rewritten on every application (frozen once dying - recordDamageAttacker).
    recordDamageAttacker(victimName, attackerName);
    out.applied = r.applied;
    out.damage = r.damage;
    out.newHp = curHp;
    if (!r.applied) return out;
    const int32_t newHp = static_cast<int32_t>(std::lround(r.newHp));
    c.currentHitPoints.set(newHp);
    c.lastDamageMagnitude = std::max(static_cast<double>(curHp - newHp), 0.0);
    if (isLocalPlayerName(victimName) && r.damage > 0.0f) armHealthRegenWait(c);  // Sec18.3 item 1
    out.newHp = newHp;
    // Review N-9: every applied non-0x200 hit fires, also when max hit points read 0 (fraction 0 then).
    fireCharacterHitCallbacks(victimName, attackerName, maxHp > 0 ? static_cast<double>(curHp) / maxHp : 0.0,
                              maxHp > 0 ? static_cast<double>(newHp) / maxHp : 0.0, flags);
    if (newHp <= 0) {
        if (tryEnterDownedOnLethalHit(c, victimName, static_cast<double>(curHp) - static_cast<double>(r.damage))) {
            out.downed = true;  // Sec21.10.1: down instead of dying (selector + owner predicate passed)
        } else {
            out.killed = true;
            finalizeLethalCharacter(c, victimName);
        }
    }
    return out;
}

void EngineState::notifyVehicleDestroyed(const std::string& vehicleName) {
    VehicleState& v = getOrCreateVehicle(vehicleName);
    if (v.destroyedCallbackFired) return;  // one fire per vehicle (spec D, HIGH CONFIDENCE)
    v.destroyedCallbackFired = true;
    // "Script record present": a vehicle the script can name. A vehicle without one only reaches the aboard
    // player's slot 0x12.
    const bool hasRecord = objectResolves().known(vehicleName) && objectResolves().get(vehicleName);
    std::vector<std::string> occupants;
    for (const auto& name : characterOrder_) {
        auto it = characters_.find(name);
        if (it == characters_.end()) continue;
        const CharacterState& c = it->second;
        if (c.stateEnum.known() && c.stateEnum.get() == 3 && c.currentVehicleName.known() &&
            AsciiLowerFold(c.currentVehicleName.get()) == AsciiLowerFold(vehicleName))
            occupants.push_back(name);
    }
    if (hasRecord) {
        const std::string cb = foldedHook(vehicles_, vehicleName, [](const VehicleState& vs) { return vs.onVehicleDestroyedCallback; });
        fireEngineCallback(cb, {CallbackArg::str(vehicleName), CallbackArg::nil()}, "on_vehicle_destroyed");
    } else {
        for (const auto& name : occupants) {
            if (!isPlayerName(name)) continue;
            const std::string cb = playerHook(characters_, name,
                                              [](const CharacterState& cs) { return cs.onVehicleDestroyedPlayerSlotCallback; });
            fireEngineCallback(cb, {CallbackArg::str(vehicleName), CallbackArg::nil()}, "on_vehicle_destroyed");
            break;
        }
    }
    // The destruction trigger kills the occupants AFTER the vehicle callback thread exists, so the vehicle's
    // callback always runs before their on_death / on_human_killed callbacks. No attacker is recorded.
    for (const auto& name : occupants) {
        recordDamageAttacker(name, std::string());
        applyGuaranteedLethalHit(name, std::string());
        CharacterState& oc = getOrCreateCharacter(name);
        oc.isDeadHighConfidence = true;  // the lethal hit reads dead even when the target's HP is not modelled
    }
}

void EngineState::notifyMoverDestroyed(const std::string& moverName, const std::string& attackerName) {
    const std::string cb = foldedHook(placedObjects_, moverName, [](const PlacedObjectState& p) { return p.onMoverDestroyedCallback; });
    fireEngineCallback(cb, {CallbackArg::str(moverName), CallbackArg::strOrNil(attackerName)}, "on_mover_destroyed");
}

int EngineState::commitVehicleSeat(const std::string& occupantName, const std::string& vehicleName, int seat) {
    VehicleState& v = getOrCreateVehicle(vehicleName);
    // CHOSEN seat pick (the engine's seat choice for an unspecified seat is not in the spec): the lowest free
    // index, so the first occupant takes the driver seat (0 = driver, passenger1 = 1, per the script docs).
    if (seat < 0) {
        seat = 0;
        while (v.seatOccupants.count(seat) && v.seatOccupants[seat] != occupantName) ++seat;
    }
    v.seatOccupants[seat] = occupantName;
    // This seat record IS the seat-0 occupant fact (Sec9.10/Sec46.4 read seat 0 as the driver slot): seat 0's seated
    // handle is v.seatOccupants[0] (VehicleState::seat0OccupantPresent / seat0HoldsCharacter read it). Seating
    // completes any claim or pending reservation this character held on the seat.
    if (seat == 0) {
        if (v.seat0Claim == occupantName) v.seat0Claim.clear();
        if (v.seat0Pending == occupantName) v.seat0Pending.clear();
    }
    const std::vector<CallbackArg> args = {CallbackArg::strOrNil(occupantName), CallbackArg::strOrNil(vehicleName),
                                           CallbackArg::num(static_cast<double>(seat))};
    // 1. vehicle record slot 7.
    fireEngineCallback(foldedHook(vehicles_, vehicleName, [](const VehicleState& vs) { return vs.onVehicleEnterCallback; }),
                       args, "on_vehicle_enter");
    // 2. the occupant's hook: the player's slot 0x16, or the NPC record's slot 0x17. The registration native's
    //    default player-class resolution accepts any non-vehicle-looking name, so an NPC registration can sit in
    //    either field; the NPC-record slot wins when both are set.
    getOrCreateCharacter(occupantName);
    std::string cb;
    if (isPlayerName(occupantName)) {
        cb = playerHook(characters_, occupantName, [](const CharacterState& cs) { return cs.onVehicleEnterGenericSlotCallback; });
    } else {
        cb = foldedHook(characters_, occupantName, [](const CharacterState& cs) { return cs.onVehicleEnterCharacterSlotCallback; });
        if (cb.empty())
            cb = foldedHook(characters_, occupantName, [](const CharacterState& cs) { return cs.onVehicleEnterGenericSlotCallback; });
    }
    fireEngineCallback(cb, args, "on_vehicle_enter");
    return seat;
}

void EngineState::commitVehicleExit(const std::string& occupantName, const std::string& vehicleName) {
    VehicleState& v = getOrCreateVehicle(vehicleName);
    int seat = 0;
    for (const auto& kv : v.seatOccupants)
        if (kv.second == occupantName) { seat = kv.first; break; }
    const std::vector<CallbackArg> args = {CallbackArg::strOrNil(occupantName), CallbackArg::strOrNil(vehicleName),
                                           CallbackArg::num(static_cast<double>(seat))};
    // 1. vehicle record slot 6 (fires before the seat is released, like the engine's slot release).
    // Registrations are kept by target name (setOnVehicleExitHook): the vehicle's by its name...
    const auto exitHook = [this](const std::string& target) {
        return foldedHook(onVehicleExitByTarget_, target, [](const std::string& s) { return s; });
    };
    fireEngineCallback(exitHook(vehicleName), args, "on_vehicle_exit");
    // 2. ...then the local player's slot 0x17 (an exiting player; either sentinel spelling), or the NPC's slot 0x18.
    std::string cb;
    if (isPlayerName(occupantName)) {
        cb = exitHook(kLocalPlayer);
        if (cb.empty()) cb = exitHook(kGenericPlayer);
    } else {
        cb = exitHook(occupantName);
    }
    fireEngineCallback(cb, args, "on_vehicle_exit");
    for (auto it = v.seatOccupants.begin(); it != v.seatOccupants.end();) {
        if (it->second == occupantName) {
            it = v.seatOccupants.erase(it); // a seat-0 occupant this host seated just left: seat0OccupantPresent() reads false
        } else {
            ++it;
        }
    }
}

void EngineState::beginMissionCellphoneCall(const std::string& callId) {
    MissionCellphoneAudio& r = missionCellphoneAudio_[callId];
    r.live = true;
    r.answered = false;
    r.ended = false;
    r.removed = false;
}

bool EngineState::answerMissionCellphoneCall(const std::string& callId) {
    auto it = missionCellphoneAudio_.find(callId);
    if (it == missionCellphoneAudio_.end() || !it->second.live || it->second.answered) return false;
    it->second.answered = true;
    return fireEngineCallback(it->second.hookSlot1, {}, "cellphone_cb1");  // arg 4 fires on answer/connect
}

bool EngineState::endMissionCellphoneCall(const std::string& callId) {
    auto it = missionCellphoneAudio_.find(callId);
    if (it == missionCellphoneAudio_.end() || !it->second.live || it->second.ended) return false;
    it->second.ended = true;
    it->second.live = false;
    return fireEngineCallback(it->second.hookSlot2, {}, "cellphone_cb2");  // arg 5, zero arguments
}

void EngineState::removeMissionCellphoneCall(const std::string& callId) {
    auto it = missionCellphoneAudio_.find(callId);
    if (it == missionCellphoneAudio_.end()) return;
    // audio_remove_mission_cellphone clears both slots and never calls the fire helper.
    it->second.hookSlot1.clear();
    it->second.hookSlot2.clear();
    it->second.live = false;
    it->second.removed = true;
}

void EngineState::resetMissionCellphoneCalls() {
    std::vector<std::string> ids;
    for (const auto& kv : missionCellphoneAudio_)
        if (kv.second.live) ids.push_back(kv.first);
    std::sort(ids.begin(), ids.end());
    for (const auto& id : ids) endMissionCellphoneCall(id);
}

void EngineState::applyTriggerEnableTransition(const std::string& triggerName, bool enabled) {
    // The enable state BEFORE this call: unknown reads as enabled (the zone default is not modelled).
    const bool wasEnabled = !triggerEnableKnown(triggerName) || triggerEnableValue(triggerName);
    bool& inside = triggerCurrentlyInside(triggerName);
    if (!enabled && wasEnabled) {
        // Destroying the phantom emits EXIT for every tracked overlap, during the disable call.
        if (inside) {
            inside = false;
            fireEngineCallback(onTriggerExitCallbackValue(triggerName),
                               {CallbackArg::str(kLocalPlayer), CallbackArg::str(triggerName)}, "on_trigger_exit");
        }
        triggerOverlapBaselined(triggerName) = true;
    } else if (enabled && !wasEnabled) {
        // A new phantom starts with an empty tracked set: whoever is already inside enters on the next pass.
        inside = false;
        triggerOverlapBaselined(triggerName) = true;
    }
    // Enabling an already-enabled trigger does nothing.
}

}  // namespace sr3luahost

namespace sr3luahost {
void EngineState::tickDownedExpiry() {
    const double now = syncedActionNowSeconds();
    const size_t n = characterOrder_.size();
    for (size_t i = 0; i < n; ++i) {
        const std::string name = characterOrder_[i];
        auto it = characters_.find(name);
        if (it == characters_.end()) continue;
        CharacterState& c = it->second;
        if (c.downedDeadlineSeconds < 0.0) continue;
        if (characterReadsDead(c) || !c.lifeState.known() || c.lifeState.get() != 6) {  // dead, or left the downed stance by another route (death, revive)
            c.downedDeadlineSeconds = -1.0;
            continue;
        }
        if (now < c.downedDeadlineSeconds) continue;
        c.downedDeadlineSeconds = -1.0;
        ++c.downedMarkerReleasedCount;
        if (c.downedType == 0) {
            // Sec21.10.1 (FINAL): type-0 expiry is a bleed-out DEATH (0x009723f0 -> 0x009a7610 -> finalizer, stance 5). The
            // team-percent heal belongs to the get-up path and to the type-1 expiry only.
            killDownedCharacter(c, name);
            continue;
        }
        c.lifeState.set(0);
        ++c.downedEndedAtExpiryCount;
        if (c.downedType == 2 && downedType2MissionCountersEqual_.known() && downedType2MissionCountersEqual_.get()) {
            // The Killbane fight hook: the character's on_take_damage slot when it is m21_killbane_damaged. The arguments
            // (name, attacker nil, health fraction, explosion false, melee false) are CHOSEN: the spec does not give them.
            const std::string cb = foldedHook(characters_, name, [](const CharacterState& cs) { return cs.onTakeDamageCallback; });
            if (AsciiLowerFold(cb) == "m21_killbane_damaged") {
                const int32_t maxHp = drawOrGetMaxHitPoints(name);
                const int32_t curHp = drawOrGetCurrentHitPoints(name);
                fireEngineCallback(cb,
                                   {CallbackArg::str(name), CallbackArg::nil(), CallbackArg::num(maxHp > 0 ? double(curHp) / maxHp : 0.0),
                                    CallbackArg::boolean_(false), CallbackArg::boolean_(false)},
                                   "downed_type2_expiry");
            }
        }
        // Get-up body 0x009a78d0 (types 1 and 2 "gets up healed", after the type-2 hook): HP is SET to int(max) x pct; pct 1.0
        // when the team byte is 0, else 0.25. Type 3 (+30 points of percent only while the attack flag is set, no host
        // source: OPEN) and type 4 (no heal) stand with the HP they have.
        if (c.downedType == 1 || c.downedType == 2) {
            const int32_t maxHp = drawOrGetMaxHitPoints(name);
            const bool enemySide = !isPlayerName(name) && teamIdOfCharacter(c) != 0;
            const float heal = sr3airuntime::combat::DownedGetUpHeal(maxHp, enemySide);  // D-27: float, not truncated
            const float whole = std::floor(heal);
            c.currentHitPoints.set(static_cast<int32_t>(whole));
            c.currentHitPointsFraction = heal - whole;
        }
    }
}
}  // namespace sr3luahost
