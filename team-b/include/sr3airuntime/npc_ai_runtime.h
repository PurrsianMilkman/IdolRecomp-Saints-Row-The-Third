#pragma once

// sr3airuntime/npc_ai_runtime.h - the per-NPC AI tick: perception -> decision -> mode -> action, wiring the
// existing libraries (perception.h, decision.h, mode_table.h, actions.h, combat_ai.h) into one deterministic
// loop that a host (the mission EngineState, the sr3_viewer playtest) drives once per frame.
//
// Source: spec-ai-runtime.md Sec21.1 (frame order: perception tick, event pump, decision step, apply, mode tick),
// Sec21.2/Sec21.9 (target selection and the headless recipe), Sec21.3 (cover timers), Sec21.4 (flee/cower),
// Sec10 (perception), Sec9 (decision), Sec12 (thresholds), Sec22.12 (weapon data).
//
// CONFIRMED / FINAL (read from the spec, used as written):
//   * the decision step, gate, chooser, 17 goal tests and the action routines (decision.h, actions.h);
//   * the data that drives them: ai_goals.xtbl action lists, ai_behavior.xtbl rows (goal-enabled bytes, timings,
//     the monotone seen_in_last rule), combat_actions.xtbl (No_interrupt_ms, Min_repeat_time, pre_flags) -
//     BuildNpcAiTables() below loads them exactly as the loaders do;
//   * the team/attitude scale (0 hostile, 1 unfriendly, 4 allied) and the rule that notoriety is NOT an AI input
//     (Sec21.1 negative): notoriety changes who is hostile through the team matrix flip (notoriety.h, Sec19.4),
//     which the HOST applies before it asks this runtime;
//   * the flee/cower decision (combat_ai.h FleeRatherThanFight / ChooseFleeOrCower) and the 30 m (900 squared)
//     flee stop rule;
//   * cover: a freshly spawned NPC has no cover object and both timers unarmed, so goal 7 passes and `cover take
//     def` is accepted (Sec21.3); on a failed node search the caller backs off a random 6-8 s (Sec21.3 search).
//
// CHOSEN stand-ins (labelled; the spec leaves them OPEN):
//   * first sight: the spec says no code converts a sighting into the target handle for an unprovoked NPC (Sec21.2).
//     Sec21.9's headless recipe is used: for a hostile NPC whose list 0 holds the player and whose target is
//     empty, set the target to the player and refresh it every 2000 ms (the follower-copy cadence);
//   * the sense pass: omnidirectional (the NPC's facing is not modelled, so the 150-degree cone is ignored),
//     range = the 45 m default view cone, plus the 25 m proximity notice; line of sight is a host callback;
//   * weapon ranges (the a/b band) and the fire model (burst size, delay, accuracy interpolation, damage): the
//     weapon runtime is not wired to NPCs; Sec22.12 gives the accuracy-record layout but no reader, so NpcFireModel
//     carries labelled defaults;
//   * mode ticks: the engine's per-mode tick bodies are not specified; the executors below do the minimum that the
//     confirmed state machine needs (walk to the action's destination, stand and fire, retreat from the threat).
//     Walk/run speed 1.5 / 5.0 m/s are the same CHOSEN values EngineState::tickScriptedOrders uses.

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "sr3airuntime/actions.h"
#include "sr3airuntime/combat_ai.h"
#include "sr3airuntime/common.h"
#include "sr3airuntime/decision.h"
#include "sr3airuntime/mode_table.h"
#include "sr3airuntime/navpoint_selector.h"
#include "sr3airuntime/perception.h"
#include "sr3tables_trafficai/tables.h"
#include "sr3tables_weapons/combat.h"

namespace sr3airuntime {

enum class NpcKind { Combatant, Civilian };

// ---------------------------------------------------------------------------------------------------------------
// The shared, read-only data an NPC's loop runs on.
// ---------------------------------------------------------------------------------------------------------------
struct NpcAiTables {
    BehaviorRecord combatant;   // ai_behavior.xtbl row "Gang Average" (Sec21.1), else the first row
    BehaviorRecord civilian;    // ai_behavior.xtbl row "Civilian Unarmed" (Sec21.1), else the first row
    std::array<std::array<uint8_t, 24>, goal::kCount> goalActionLists;  // ai_goals.xtbl order_of_actions
    std::array<uint32_t, action::kCount> preFlags{};      // combat_actions.xtbl precondition masks
    std::array<int64_t, action::kCount> noInterruptMs{};  // combat_actions.xtbl No_interrupt_ms
    // ai_personalities.xtbl row "civilian normal" (Sec21.4: Flee_health_pct 50): the civilian flee tweaks.
    int civilianFleeHealthPct = 0;
    uint8_t civilianFleeEvents = 0;
    bool loadedFromData = false;
    NpcAiTables() {
        for (auto& l : goalActionLists) l.fill(ActionBlock::kNoAction);
    }
};

// The behaviour record loader (spec-tables-traffic-ai.md Sec10 record rules). minRepeatMs = combat_actions.xtbl
// Min_repeat_time per action (the loader raises Repeat_min to it).
BehaviorRecord BehaviorRecordFromRow(const sr3tables_trafficai::AiBehavior& row,
                                      const std::array<int64_t, action::kCount>& minRepeatMs);

// Builds the tables from the three parsed xtbl tables (+ optional personalities). Row names default to the
// behaviours Sec21.1 names; a missing name falls back to the first row of ai_behavior.xtbl.
NpcAiTables BuildNpcAiTables(const sr3tables_trafficai::AiGoalsTable& goals,
                              const sr3tables_trafficai::AiBehaviorTable& behaviors,
                              const std::vector<sr3tables_weapons::CombatActionOverrideRow>& actions,
                              const sr3tables_trafficai::AiPersonalitiesTable* personalities = nullptr,
                              const std::string& combatantBehavior = "Gang Average",
                              const std::string& civilianBehavior = "Civilian Unarmed");

// ---------------------------------------------------------------------------------------------------------------
// Configuration - the CHOSEN stand-ins, in one place.
// ---------------------------------------------------------------------------------------------------------------
struct NpcFireModel {
    // CHOSEN (Sec22.12 gives only the record layout, no reader): bursts of burstSize shots, burstDelayMs between
    // bursts, shotGapMs between the shots of one burst; accuracy is the hit probability, interpolated linearly
    // from closeAccuracy at closeRange to farAccuracy at farRange and clamped outside (Sec22.12 CHOSEN list).
    int burstSize = 3;
    int64_t burstDelayMs = 900;
    int64_t shotGapMs = 120;
    double closeRange = 8.0, closeAccuracy = 0.7;
    double farRange = 40.0, farAccuracy = 0.15;
    double damagePerHit = 4.0;  // hit points per hit (CHOSEN; the NPC's weapon row is not resolved)
};

struct NpcAiConfig {
    int64_t senseMinMs = 650, senseMaxMs = 800;  // Sec21.1 "sense pass runs every 650-800 ms" (in combat, civilians, ...)
    // Sec10 sense pass step 7: 350-450 ms for a non-civilian below activity level 6 (not in combat).
    int64_t senseNearMinMs = 350, senseNearMaxMs = 450;
    double viewRange = 45.0;                    // Sec21.1 default view cone length (cone angles ignored: CHOSEN)
    double proximityNoticeRange = 25.0;         // Sec21.1 "within the 25.0 proximity test is noticed without sight"
    double runSpeed = 5.0, walkSpeed = 1.5;     // CHOSEN (same as EngineState::tickScriptedOrders)
    // Sec20.6.9(a) measured speeds. A fleeing civilian within 30 m of the threat uses the `cower run` clip: 4.554 m/s
    // (PEDM) / 5.745 m/s (PEDF); the flee ends beyond 30 m, so this is the only speed it ever runs here. (The beyond-30 m
    // run-variant clips 6.373 / 4.433 m/s never apply; the variant pick per ped handle is HIGH, not modelled.)
    double fleeSpeed = 4.554, fleeSpeedFemale = 5.745;
    double distantThreatSq = 400.0;              // Sec21.6 0x004eaf80: beyond this squared distance the reaction is delayed
    int64_t distantThreatDelayMinMs = 250, distantThreatDelayMaxMs = 750;  // event 67
    double weaponRangeA = 4.0, weaponRangeB = 30.0;  // CHOSEN a/b band of a combatant's rifle-class weapon
    int64_t reevaluateMs = 1000;                // CHOSEN cadence at which a running mode raises the re-evaluate bit
    int64_t cowerMs = 8000;                     // CHOSEN: a cowering NPC stands up this long after the last threat
    int64_t civilianDecisionMs = 500;           // civilians re-run their (cheap) decision step at this cadence
    double coverApproachRadius = 2.0, coverFireRadius = 2.0;  // tunables 0x024fb1c0/d0 are zero in the image: CHOSEN
    double threatRange = 30.0;                  // CHOSEN: a civilian reacts to gunfire/aiming within this range
    // Sec21.3 search (FINAL): AABB half-width 14.0 (15.0 for a brute: no brute fact in this host) and a fixed vertical
    // half-height 4.0 around the human.
    double coverSearchHalfWidth = 14.0, coverSearchHalfHeight = 4.0;
    // Heights added to the ray endpoints handed to the LOS seam (hosts with real collision set them; 0 = the raw positions).
    // CHOSEN: the NPC/player eye height for the notice/fire check, and the crouch height of a cover node for the evaluator's
    // shield stand-in (the engine's ray heights are not specified).
    double losEyeHeight = 0.0, coverShieldHeight = 0.0;
    bool fireEnabled = true;
    NpcFireModel fire;
};

// ---------------------------------------------------------------------------------------------------------------
// Host interface
// ---------------------------------------------------------------------------------------------------------------
struct NpcSpawn {
    uint64_t handle = 0;
    NpcKind kind = NpcKind::Combatant;
    Vec3 pos;
    double health = 100.0, maxHealth = 100.0;
    bool cantFlee = false, cantCower = false, alwaysCower = false;
    // Which anim set the ped runs: PEDF (female) or PEDM (male). Only the flee speed reads it (Sec20.6.9(a) measured
    // clip speeds). No host data source for it yet, so hosts leave it false (male clip speeds): CHOSEN default.
    bool female = false;
};

struct PlayerFacts {
    uint64_t handle = 1;
    Vec3 pos;
    bool alive = true;
    bool armed = true;          // holds a ranged weapon (cover rule 7); CHOSEN default
    bool firingOrAiming = false;  // the player fired or aimed this frame (the civilians' gunfire/aim threat)
};

enum class NpcThreat : uint8_t { None, Gunfire, Attacked };

struct NpcFacts {
    uint64_t handle = 0;
    Vec3 pos;
    double health = 100.0, maxHealth = 100.0;
    bool alive = true;
    bool hostileToPlayer = false;  // attitude toward the player is hostile (host resolves: team matrix + force flags)
    bool inVehicle = false;
    bool ignoreAi = false;         // force flag ignore_ai
    double damageFromPlayer = 0;   // hit points the player did to this NPC since the last tick
    bool provoked = false;         // host: this NPC has been attacked by the player at some point
    // Sec10 attitude 0x004fe0f0 toward the player (0 hostile ... 4+ allied); -1 = the host gave none: derived as 0 when
    // hostileToPlayer, else 2 (the pre-attitude behaviour of this loop). The damage reaction branches on it (AI-5).
    int attitudeToPlayer = -1;
    bool neverTurnOnPlayer = false;  // force flag never_turn_on_player (+0x2b9 bit 6): no player ever enters list 0
};

struct NpcStep {
    uint64_t handle = 0;
    Vec3 pos;
    bool moved = false;
    bool detectedPlayer = false;  // a new list-0 entry for the player was created this tick (on_detection fire site)
    int32_t mode = mode::kNone;
    int action = ActionBlock::kNoAction;
};

struct NpcShot {
    uint64_t shooter = 0;
    bool hit = false;
    double damage = 0;
    Vec3 from, to;
};

struct NpcTransitionLog {
    uint64_t handle = 0;
    int64_t nowMs = 0;
    int32_t fromMode = mode::kNone, toMode = mode::kNone;
    int action = ActionBlock::kNoAction;
    int goalReason = -1;
    double distToPlayer = 0;
};

// ---------------------------------------------------------------------------------------------------------------
// Per-NPC state
// ---------------------------------------------------------------------------------------------------------------
struct NpcState {
    uint64_t handle = 0;
    NpcKind kind = NpcKind::Combatant;
    std::unique_ptr<HumanAiState> ai;
    std::unique_ptr<DeterministicRng> rng;
    Vec3 pos;
    int64_t lastTickMs = -1;
    int64_t nextSenseMs = 0;
    int64_t nextDecisionMs = 0;
    int64_t lastReevalMs = 0;
    bool targetSet = false;
    int64_t lastTargetCopyMs = -1000000;
    bool provoked = false;
    bool cantFlee = false, cantCower = false, alwaysCower = false;
    bool female = false;
    // flee / cower
    bool fleeActive = false;
    Vec3 fleeFrom;
    int64_t cowerUntilMs = 0;
    // Sec21.6 TV4 / 0x004eaf80: a threat beyond 20 m (squared 400) is answered by the delayed event 67 (250-750 ms).
    int64_t pendingThreatAtMs = -1;
    NpcThreat pendingThreat = NpcThreat::None;
    // human +0x315 bit 7 (Sec11.6 / 0x00541600): set by the first cover entry; while clear, that entry uses the 800 ms
    // stay-down timer (when recent damage is below 20%).
    bool coverEnteredBefore = false;
    // fire
    int burstLeft = 0;
    int64_t nextShotMs = 0;
    int64_t nextBurstMs = 0;
    // cover
    bool hasCover = false;
    int coverNodeIdx = -1;  // index into the runtime's cover node pool while this NPC holds a claim (AI-7)
    // Sec10/Sec12 asynchronous line-of-sight answer: cached, re-asked every 700-4000 ms (+500 ms after a visible answer).
    bool losCached = true;
    bool playerEntryPresent = false;  // the player had a list-0 entry at the end of the last tick (on_detection edge)
    int64_t losNextMs = 0;
    Vec3 coverPos;
    int64_t coverEnteredMs = 0;
    // log bookkeeping
    int32_t loggedMode = mode::kNone;
    int loggedAction = ActionBlock::kNoAction;
    int lastActionSeen = ActionBlock::kNoAction;  // the last real action id the mode held (a finish resets currentAction)
    ActionWorldFacts actionFacts;
};

class NpcAiRuntime {
public:
    explicit NpcAiRuntime(std::shared_ptr<const NpcAiTables> tables, NpcAiConfig cfg = {}, uint64_t seed = 0x5133);
    ~NpcAiRuntime();

    void addNpc(const NpcSpawn& s, int64_t nowMs);
    void removeNpc(uint64_t handle);
    bool has(uint64_t handle) const { return npcs_.count(handle) != 0; }
    size_t count() const { return npcs_.size(); }

    // One frame for every NPC in `facts` (NPCs not yet added are ignored; added NPCs absent from `facts` are
    // skipped). Fills steps (one per ticked NPC) and shots (shots fired this frame, hit already decided).
    void tick(int64_t nowMs, const PlayerFacts& player, const std::vector<NpcFacts>& facts, std::vector<NpcStep>& steps,
              std::vector<NpcShot>& shots);

    // Diagnostics.
    int32_t modeOf(uint64_t handle) const;
    int actionOf(uint64_t handle) const;
    const NpcState* state(uint64_t handle) const;
    std::vector<NpcTransitionLog> drainLog();
    uint64_t decisionStepCount() const { return decisionSteps_; }
    // OPEN-item accounting: an action routine the spec leaves OPEN (ActionRegistry throws OpenStateError) is treated as
    // "refuses" and counted here; any other OpenStateError raised by a tick is caught, remembered (first of each distinct
    // message) and the NPC is skipped for that frame. No behaviour is invented for either.
    const std::array<uint64_t, action::kCount>& openActionRefusals() const { return openActionRefusals_; }
    const std::vector<std::string>& openErrors() const { return openErrors_; }
    uint64_t openErrorCount() const { return openErrorCount_; }

    // AI-7: the live cover_node pool (Sec21.3 / Sec15.7 gather input). Positions only: the node yaw and the four angle
    // properties are not decoded from the zone data yet, so the evaluator's shield test is a CHOSEN LOS stand-in (see
    // searchCover). Replaces any previous pool and clears every claim.
    void setCoverNodes(std::vector<Vec3> positions);
    size_t coverNodeCount() const { return coverNodes_.size(); }

    struct Stats {
        uint64_t losChecks = 0, losBlocked = 0;                         // AI-3: rays cast / answered "blocked"
        uint64_t coverSearches = 0, coverFound = 0, coverNoNode = 0;    // AI-7: Sec21.3 searches by outcome
        uint64_t coverNodesInBox = 0, coverShieldTests = 0;             // AI-7: nodes gathered by the AABB / shield rays cast
        double coverNearestNodeXZ = 1e30, coverNearestNodeDy = 0;       // AI-7 diagnostic: closest pool node to any searching NPC
    };
    const Stats& stats() const { return stats_; }

    // Host seams (all optional).
    std::function<bool(const Vec3& from, const Vec3& to)> lineOfSight;  // default: clear
    std::function<std::optional<Vec3>(const Vec3& npc, const Vec3& threat)> findCover;  // default: none found

    const NpcAiConfig& config() const { return cfg_; }
    NpcAiConfig& config() { return cfg_; }

private:
    std::shared_ptr<const NpcAiTables> tables_;
    NpcAiConfig cfg_;
    uint64_t seed_;
    std::unordered_map<uint64_t, NpcState> npcs_;
    std::vector<uint64_t> order_;  // insertion order = deterministic iteration order
    std::vector<NpcTransitionLog> log_;
    ActionRegistry registry_;
    uint64_t decisionSteps_ = 0;
    std::array<uint64_t, action::kCount> openActionRefusals_{};
    std::vector<std::string> openErrors_;
    uint64_t openErrorCount_ = 0;
    Stats stats_;
    struct CoverNodeSlot {
        Vec3 pos;
        uint64_t claimedBy = 0;  // owner handle, 0 = unclaimed (Sec21.3: compare-and-set of +0x70/+0x74)
    };
    std::vector<CoverNodeSlot> coverNodes_;
    void releaseCover(NpcState& s);
    std::optional<Vec3> searchCover(NpcState& s, const NpcFacts& f, const PlayerFacts& pl, int64_t nowMs);
    double rand01(NpcState& s);
    void tickOne(NpcState& s, const NpcFacts& f, int64_t nowMs, const PlayerFacts& player, NpcStep& step,
                 std::vector<NpcShot>& shots);
};

}  // namespace sr3airuntime
