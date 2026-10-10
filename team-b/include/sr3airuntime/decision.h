#pragma once

// sr3airuntime/decision.h - AI runtime unit A1: the decision step (gate,
// goal/action chooser, the 17 goal tests, the action table) and the
// perception-boundary read side.
//
// Source: spec-ai-runtime.md Sec9, specifically Sec9.1-Sec9.4 (the step,
// the gate, the chooser, the precondition test), Sec9.5 (the goal and
// action tables), Sec9.6 (the 17 goal tests), Sec9.7 (action -> mode / per-
// action data), Sec9.8 (the perception-boundary read side - resolved
// against sr3airuntime::PerceptionState, A2), Sec9.9 (the 41 force flags)
// and Sec9.10 (corrections/port notes this file folds in directly rather
// than re-deriving). Every behaviour below is CONFIRMED in that text unless
// a comment says otherwise.
//
// SCOPE. This unit owns: the gate, the chooser, the 17 goal tests, the
// action table's one CONFIRMED static column (category) and the one
// CONFIRMED image constant (the keep-current-exemption byte). No_interrupt_
// ms and pre_flags are combat_actions.xtbl data this unit does not load -
// see HumanAiState::noInterruptMs/preFlags, per-instance arrays a host
// seeds from real table data. It also owns the behaviour-record fields the
// decision layer reads and the 41 force flags. It explicitly does NOT
// implement: the 70 action routines' bodies (A3, actions.h calls back into
// this unit's types), the weapon-upkeep helper 0x00600720 (HYPOTHESIS, not
// read by the spec, no action routine calls it either), or any xtbl
// loading (ai_goals.xtbl/ai_behavior.xtbl/combat_actions.xtbl are a
// different, data-side spec); this unit's own defaults are the documented
// ABSENT-row defaults, never a guess at real tuning.
//
// THE NULL-ROUTINE GUARD (Sec9.5, Sec11.1). Actions 24 (move scripted) and
// 49 (vehicle enter scpt) have a null routine in the executable; the goal
// loop there calls it WITHOUT a null check - a latent hazard with modified
// data (Sec11.1: "one goal-list entry plus one ability_elm would be enough
// to make the goal loop call address 0"). This project's own data defaults
// (empty goalActionLists, zero pre_flags) already make them unreachable,
// but Chooser() ALSO refuses them explicitly via HasNullRoutine() - a port-
// level safety fix the task's own scope section asks for ("a port must
// still treat 24 and 49 as unchoosable by goals"), verified by this unit's
// own tests regardless of what data a host later wires in.

#include <array>
#include <cstdint>
#include <functional>
#include <string>

#include "sr3airuntime/common.h"
#include "sr3airuntime/mode_table.h"
#include "sr3airuntime/perception.h"
#include "sr3luahost/open_state.h"

namespace sr3airuntime {

using sr3luahost::OpenValue;

// --- goal ids (Sec9.5's goal table 0x012e22f0, 17 rows) ---------------------------
namespace goal {
inline constexpr int ChangeMode = 0;
inline constexpr int AvoidBrute = 1;
inline constexpr int KillNearPlayer = 2;
inline constexpr int Survive = 3;
inline constexpr int Retreat = 4;
inline constexpr int Group = 5;
inline constexpr int Suppress = 6;
inline constexpr int Cover = 7;
inline constexpr int TakingDamage = 8;
inline constexpr int Advance = 9;
inline constexpr int Reload = 10;
inline constexpr int KillEnemy = 11;
inline constexpr int FlushOut = 12;
inline constexpr int Frustrated = 13;
inline constexpr int AcquireTarget = 14;
inline constexpr int Investigate = 15;
inline constexpr int Idle = 16;
inline constexpr int kCount = 17;
}  // namespace goal

// --- action ids (Sec9.5's action table 0x012e1c60, 70 rows) -----------------------
namespace action {
inline constexpr int Fire = 0, FireSweep = 1, FireSuppress = 2, FireChaos = 3, ThrowGrenade = 4, ThrowWeapon = 5,
                      Melee = 6, MeleeVehicle = 7, CoverTakeDef = 8, CoverTakeOff = 9, CoverAdvance = 10,
                      CoverStayDown = 11, CoverFire = 12, CoverPopout = 13, HoldPosition = 14, MeleeStandBack = 15,
                      MoveRetreat = 16, MoveRegroup = 17, MoveAdvance = 18, MoveChase = 19, AvatarChase = 20,
                      MoveSurround = 21, MoveRush = 22, MoveRushToMelee = 23, MoveScripted = 24, MoveToNavmesh = 25,
                      MoveSidestep = 26, MoveToPostCombat = 27, MoveToPostIdle = 28, MoveCloseIn = 29,
                      FollowMakeWay = 30, FollowAvoidLOF = 31, FollowFormation = 32, FollowAcquire = 33,
                      FollowPlayer = 34, FollowTeleport = 35, FollowLemming = 36, FollowToCar = 37,
                      FollowOtherCar = 38, FollowCarjack = 39, FollowBrowse = 40, InvestigateMove = 41,
                      InvestigateLook = 42, InvestigatePeek = 43, Reload = 44, PickupWeapon = 45,
                      HumanShieldGrab = 46, Taunt = 47, VehiclePassenger = 48, VehicleEnterScpt = 49,
                      VehicleExtract = 50, BruteThrowProp = 51, BruteCarFlip = 52, BruteBullRush = 53,
                      BruteQtePlayer = 54, BruteAttackBrute = 55, BruteGunFinisher = 56, AvatarShockwave = 57,
                      AvatarStomp = 58, AvatarTaunt = 59, AvatarFireballs = 60, AvatarTeleportStomp = 61,
                      AvatarBullrush = 62, QuickKill = 63, ZombieEat = 64, ZombieExplode = 65, KillbaneChase = 66,
                      KillbaneStrafe = 67, RollerBladerChange = 68, Idle = 69;
inline constexpr int kCount = 70;
}  // namespace action

// The two null-routine actions (Sec9.5, Sec11.1). See this header's own
// "THE NULL-ROUTINE GUARD" note above.
inline constexpr std::array<int, 2> kNullRoutineActions = {action::MoveScripted, action::VehicleEnterScpt};
inline bool HasNullRoutine(int actionId) {
    return actionId == action::MoveScripted || actionId == action::VehicleEnterScpt;
}

// --- the action table's CONFIRMED static columns (Sec9.5, Sec9.7) ----------------
struct ActionTableRow {
    const char* name;
    int32_t categoryMode;         // +0x04 (Sec9.5's per-category list; CONFIRMED for all 70)
    // +0x0c Min_repeat_time and +0x0e Auto_abort_ms / +0x12
    // refresh_desire_interval have no reader in this unit (Sec9.7) and are
    // not modelled. +0x10 No_interrupt_ms IS read (Sec9.1) but its VALUES
    // are combat_actions.xtbl data this unit does not load here - see
    // HumanAiState::noInterruptMs, a per-instance array defaulting to 0
    // (never waits) until a host seeds real values.
    uint8_t keepCurrentExemption;  // +0x14: CONFIRMED image data - 1 only for CoverStayDown(11), else 0 (Sec9.4 step 5)
    // +0x15 pre_flags is NOT a column here: Sec9.4 confirms "all 70 masks
    // are 0 in the file image", so there is no per-action DATA to
    // tabulate - see HumanAiState::preFlags, a per-instance array (same
    // reasoning as noInterruptMs below) a host seeds from real
    // combat_actions.xtbl medium bits.
};

// Transcribed from Sec9.5's full action table (category column) and Sec9.4
// step 5 (keep-current exemption: only action 11 is 1). preFlags default 0
// matches the CONFIRMED file-image state exactly (not a guess): until a
// host seeds real data, no action can pass the precondition test, which is
// the documented, safe default (Sec9.4: "an action is choosable only after
// combat_actions.xtbl has given it at least one medium bit").
inline constexpr std::array<ActionTableRow, action::kCount> kActionTable = {{
    /*0  fire               */ {"fire", mode::FireSpray, 0},
    /*1  fire sweep         */ {"fire sweep", mode::FireSpray, 0},
    /*2  fire suppress      */ {"fire suppress", mode::FireSpray, 0},
    /*3  fire chaos         */ {"fire chaos", mode::FireSpray, 0},
    /*4  throw grenade      */ {"throw grenade", mode::GrenadeThrow, 0},
    /*5  throw weapon       */ {"throw weapon", mode::WeaponThrow, 0},
    /*6  melee              */ {"melee", mode::Melee, 0},
    /*7  melee vehicle      */ {"melee vehicle", mode::Melee, 0},
    /*8  cover take def     */ {"cover take def", mode::CoverStay, 0},
    /*9  cover take off     */ {"cover take off", mode::CoverStay, 0},
    /*10 cover advance      */ {"cover advance", mode::CoverStay, 0},
    /*11 cover stay down    */ {"cover stay down", mode::CoverStay, 1},
    /*12 cover fire         */ {"cover fire", mode::CoverStay, 0},
    /*13 cover popout       */ {"cover popout", mode::CoverPopout, 0},
    /*14 hold position      */ {"hold position", mode::CombatMovement, 0},
    /*15 melee stand back   */ {"melee stand back", mode::CombatMovement, 0},
    /*16 move retreat       */ {"move retreat", mode::CombatMovement, 0},
    /*17 move regroup       */ {"move regroup", mode::CombatMovement, 0},
    /*18 move advance       */ {"move advance", mode::CombatMovement, 0},
    /*19 move chase         */ {"move chase", mode::CombatMovement, 0},
    /*20 avatar chase       */ {"avatar chase", mode::CombatMovement, 0},
    /*21 move surround      */ {"move surround", mode::CombatMovement, 0},
    /*22 move rush          */ {"move rush", mode::CombatMovement, 0},
    /*23 move rush to melee */ {"move rush to melee", mode::CombatMovement, 0},
    /*24 move scripted      */ {"move scripted", mode::CombatMovement, 0},  // NULL ROUTINE (Sec9.5/Sec11.1)
    /*25 move to navmesh    */ {"move to navmesh", mode::CombatMovement, 0},
    /*26 move sidestep      */ {"move sidestep", mode::CombatMovement, 0},
    /*27 move to post combat*/ {"move to post combat", mode::CombatMovement, 0},
    /*28 move to post idle  */ {"move to post idle", mode::CombatMovement, 0},
    /*29 move close in      */ {"move close in", mode::CombatMovement, 0},
    /*30 follow make way    */ {"follow make way", mode::Follow, 0},
    /*31 follow avoid LOF   */ {"follow avoid LOF", mode::Follow, 0},
    /*32 follow formation   */ {"follow formation", mode::Follow, 0},
    /*33 follow acquire     */ {"follow acquire", mode::Follow, 0},
    /*34 follow player      */ {"follow player", mode::Follow, 0},
    /*35 follow teleport    */ {"follow teleport", mode::Follow, 0},
    /*36 follow lemming     */ {"follow lemming", mode::Follow, 0},
    /*37 follow to car      */ {"follow to car", mode::Follow, 0},
    /*38 follow other car   */ {"follow other car", mode::Follow, 0},
    /*39 follow carjack     */ {"follow carjack", mode::Hijack, 0},
    /*40 follow browse      */ {"follow browse", mode::Follow, 0},
    /*41 investigate move   */ {"investigate move", mode::Investigate, 0},
    /*42 investigate look   */ {"investigate look", mode::Investigate, 0},
    /*43 investigate peek   */ {"investigate peek", mode::Investigate, 0},
    /*44 reload             */ {"reload", mode::Reload, 0},
    /*45 pickup weapon      */ {"pickup weapon", mode::PickupItem, 0},
    /*46 human shield grab  */ {"human shield grab", mode::HumanShield, 0},
    /*47 taunt              */ {"taunt", mode::Taunt, 0},
    /*48 vehicle passenger  */ {"vehicle passenger", mode::Passenger, 0},
    /*49 vehicle enter scpt */ {"vehicle enter scpt", mode::Hijack, 0},  // NULL ROUTINE (Sec9.5/Sec11.1)
    /*50 vehicle extract    */ {"vehicle extract", mode::Hijack, 0},
    /*51 brute throw prop   */ {"brute throw prop", mode::BruteMelee, 0},
    /*52 brute car flip     */ {"brute car flip", mode::BruteMelee, 0},
    /*53 brute bull rush    */ {"brute bull rush", mode::BruteMelee, 0},
    /*54 brute qte player   */ {"brute qte player", mode::BruteMelee, 0},
    /*55 brute attack brute */ {"brute attack brute", mode::BruteMelee, 0},
    /*56 brute gun finisher */ {"brute gun finisher", mode::BruteMelee, 0},
    /*57 avatar shockwave   */ {"avatar shockwave", mode::BruteMelee, 0},
    /*58 avatar stomp       */ {"avatar stomp", mode::BruteMelee, 0},  // shared return-false stub (Sec11.3 A)
    /*59 avatar taunt       */ {"avatar taunt", mode::Taunt, 0},
    /*60 avatar fireballs   */ {"avatar fireballs", mode::BruteMelee, 0},
    /*61 avatar teleport st.*/ {"avatar teleport stomp", mode::BruteMelee, 0},
    /*62 avatar bullrush    */ {"avatar bullrush", mode::BruteMelee, 0},
    /*63 quick kill         */ {"quick kill", mode::Commando, 0},
    /*64 zombie eat         */ {"zombie eat", mode::ZombieEat, 0},
    /*65 zombie explode     */ {"zombie explode", mode::ZombieEat, 0},
    /*66 killbane chase     */ {"killbane chase", mode::CombatMovement, 0},
    /*67 killbane strafe    */ {"killbane strafe", mode::CombatMovement, 0},
    /*68 roller blader chg. */ {"roller blader change", mode::RollerBlader, 0},
    /*69 idle                */ {"idle", mode::Idle, 0},
}};

// --- the behaviour record (ai_behavior.xtbl per-character data; Sec9.7, Sec9.9) --
// Defaults are the documented ABSENT-row defaults from the loader
// (spec-tables-traffic-ai.md Sec10, quoted in include/sr3tables_trafficai/
// tables.h's own comments) - never a guessed TUNING value, only the
// engine's own "this field was not authored" fallback.
struct AbilityEntry {
    int32_t repeatMinMs = 0;
    int32_t repeatMaxMs = 0;
    bool enabled = false;
};
struct BehaviorRecord {
    std::array<uint8_t, goal::kCount> goalEnabled{};   // +0x1c6..+0x1d6 (Sec9.4 step 6, Sec9.5)
    std::array<AbilityEntry, action::kCount> abilities{};  // +0x22+6*a (Sec9.4 step 6, Sec9.7)

    double lowHealthPct = 0;          // +0x1d8 Survive/low_health_pct
    double clipPct = 0;               // +0x1e4 Reload/clip_pct
    double seperatedDistance = 1e30;        // +0x1dc (absent -> FLT_MAX, loader default)
    double seperatedDistanceInterior = 1e30; // +0x1e0 (absent -> seperatedDistance)
    int64_t abandonCoverTimeMs = 30000;     // +0x1e8 (loader default)
    int64_t findNewCoverTimeMs = 30000;     // +0x1ea
    int64_t advanceCoverTimeMs = 30000;     // +0x1ec
    int64_t stayDownTimeMs = 1000;          // +0x1ee
    int64_t popUpTimeMs = 30000;             // +0x1f0
    int64_t suppressLengthMs = 0;            // +0x1f2 Suppress/length
    int32_t squadCoverPct = 100;             // +0x1fc (no reader anywhere, Sec11.5)
    uint8_t dodgeChance = 0;                 // +0x1fd Options/dodge_chance (0..100)
    int32_t combatAggression = 1;            // +0x1fe (behaviour-selection key, Sec11.5; not read by A1 itself)
    int64_t killEnemySeenInLastMs = 0;       // +0x1f4
    int64_t flushOutSeenInLastMs = 0;        // +0x1f6
    int64_t acquireTargetSeenInLastMs = 0;   // +0x1f8
    int64_t investigateSeenInLastMs = 0;     // +0x1fa
    bool goalsCanProcessEarly = false;       // +0x1ff bit 2
    bool takeCoverWhenDamaged = false;       // +0x1ff bit 7
    bool takeCoverWhenShotAt = false;        // +0x1ff bit 6
    bool takeCoverWhenAimedAt = false;       // +0x1ff bit 5
};

// --- the 41 force flags (+0x2b8..+0x2bd; Sec9.9's table, engine data strings) -----
struct ForceFlags {
    // +0x2b8
    bool sidewalking = false, wandering = false, sidewalkLeashed = false, attackPedestriansOnSight = false,
         combatDisabled = false, fleeAfterAttackNotWorking = false, stayInPlayersViewNotWorking = false,
         alwaysSeesPlayer = false;
    // +0x2b9
    bool attackPlayerOnSight = false, attackNonPlayerEnemiesOnSight = false, dontReactToPlayer = false,
         ignoreAttacks = false, deaf = false, dontObserve = false, neverTurnOnPlayer = false, alwaysCower = false;
    // +0x2ba
    bool alwaysFollowInAlternateVehicle = false, canLeaveBehind = false, cantAttack = false, cantCower = false,
         cantEnterVehicle = false, cantFlee = false, cantGrab = false, cantPickupWeapons = false;
    // +0x2bb
    bool disallowPersonaLines = false, dontAccelerateWhenParachuting = false,
         dontAutoEquipOrUnequipWeapons = false, dontFleePostVehicleExit = false,
         dontTeleportToFollowVehicle = false, dontUseSidewalk = false, driver = false;
    // +0x2bc
    bool inScriptedCover = false, ignoreAi = false, ignoreNotoriety = false, squadLeader = false,
         useLethalWeapons = false, moveAlwaysWalk = false, moveAlwaysRun = false, moveAlwaysSprint = false;
    // +0x2bd
    bool streakerShock = false, vehicleEnterGroupStarted = false;
};

// --- external facts the decision layer needs but this unit does not own ----------
// (object/weapon/world state - Sec9.8, Sec9.10, Sec11.6). One struct per
// human, refreshed by the host each decision call; see this header's SCOPE
// note on why these are plain fields rather than an interface: none of them
// is a multi-operation subsystem, each is a single fact Sec9.8/Sec11.6
// names explicitly.
struct WorldFacts {
    // Sec9.4 state mask / Sec9.3 gate:
    bool inVehicle = false;             // 0x009b9160 || 0x009b91e0
    int32_t healthState = 0;            // +0xcc8 (0/7 normal-ish, 3 water, 5/6 down, 7 shielded, 14 free-fall)
    bool isDeadFlagE3Bit0 = false;      // +0xe3 bit 0 (Sec9.3 test 4, Sec1.4's 0x0096f4f0 pairing)
    bool heldInGrab = false;            // 0x009aa360(human,...) (Sec9.3 test 5, Sec11.6)
    bool isPlayerClass = false;         // 0x00971cf0(human) non-null (Sec9.3 test 6)

    // Sec9.8 live-target test 0x004e4e50:
    bool targetHandleSet = false;
    uint64_t targetHandle = 0;
    bool targetResolvesToUsableBaseTypeObject = false;
    bool targetIsDownOrDead = false;         // 0x0096f4f0
    bool targetFlag3bBit0 = false;           // 0x008cc4d0
    bool targetActiveFlag33Bit3 = true;

    // Sec9.8 weapon ranges 0x004dd530 (a/b, already resolved by the host -
    // this project has no weapon/inventory runtime, Sec9.10 item 1):
    double weaponRangeA = 0;    // unarmed default (Sec9.8)
    double weaponRangeB = 1.5;  // unarmed default; 2.4 for the brute bit (Sec9.8)
    bool hasHeldItem = false;   // 0x00941f40 non-null (Sec9.2 step 3, Sec9.10 item 1)
    bool heldItemIsRanged = false;         // 0x009ba7e0(human) (Sec9.6 `advance` step 3, `acquire target`, `flush out`)
    bool targetHoldsRangedWeapon = false;  // 0x009ba7e0(T) (Sec9.6 cover rule 7)
    bool isBruteWeaponInfo12 = false;      // 0x0052a2e0(human) (Sec9.6 `advance` step 3)
    bool isFollower = false;               // 0x0097e280(human) non-null (Sec9.6 investigation candidate, `advance`)

    // Sec9.6 `group`:
    bool hasGroupLeader = false;
    bool groupLeaderIsPlayer = false;
    bool groupLeaderHasSquadLeaderFlag = false;
    bool isHumanTyped = false;        // type-descriptor bit 2 (Sec1.2)
    bool flag1e01Bit0 = false;        // +0x1e01 bit 0

    bool flag317Bit0 = false;  // +0x317 bit 0: selects seperated_distance vs _interior (Sec9.7/Sec11.5; actions 17, 46)

    // Sec9.6 `avoid brute`: per-perceived-entity type classification
    // (0x0052a230, "is-a brute") - a callback since several list-0 entries
    // may need testing, not just the live target. Default: nothing is a
    // brute (safe - the goal simply never fires rather than guessing).
    std::function<bool(uint64_t handle)> isBruteFn;

    // Sec9.6 `suppress`/`cover`/`advance` cover-posture family (0x004dd640:
    // +0xccd==9 with +0xcce in {15,16,18} for `kill enemy`, {17,19} for
    // `suppress`):
    int32_t stateCcd = 0;   // +0xccd
    int32_t stateCce = 0;   // +0xcce
    bool coverSuppressHelper = false;  // 0x00540a70(human,0,0x024fb1c0) variant (Sec9.6 `suppress`; not read)

    // Sec9.6 `cover`:
    bool hasCoverObject = false;
    Vec3 coverObjectPosition;
    bool flag76fBit3 = false;
    bool targetAimingAtHuman = false;  // 0x0050cdb0(T,human,...) (Sec11.6; cone tunables are OPEN zero-fill)
    bool combatAggressionIsTwo = false;  // 0x00507ee0 (Sec9.8's personality read)

    // Sec9.6 `advance`:
    bool isAggressivePersonality = false;  // same test redundantly named for readability at advance's call sites
    int32_t targetLevelDifference = 0;     // 0x004df250 (Sec11.6; navmesh cell data out of scope - 0 = same level default)
    bool isDownOrRagdoll = false;          // Sec11.6's 0x004e68e0 edge case
    bool is0x009422dClass = false;         // Sec11.6's vehicle-geometry tail gate
    bool vehicleGeometryClearTail = true;  // Sec11.6's point-to-box clearance result (simplified; default "clear")
    bool targetInVehicleState3 = false;    // T's +0x16d4==3 (Sec9.6 `advance` step 7)

    // Sec9.6 `reload`:
    bool canReload = false;     // 0x009c05b0(human,0)'s full result (Sec11.6; weapon/anim state out of scope)
    double clipFraction = 1.0;  // 0x00b81cb0 (full = never needs reload, safe default)

    // Sec9.6 `kill enemy`/`flush out`/`acquire target`:
    bool allyLinkResolvesToLiveObject = false;  // !0x004fe910(+0x250) (Sec9.6 `flush out`)

    // Sec11.5's one personality value A3's `move advance` reads (kept here
    // since it is the same personality lookup Sec9.8 already names):
    int32_t personalityCombatAggression = 1;  // 0 / 1 / 2 (Sec11.5; 1 = neither special case)
};

// --- per-human AI state (A1's own fields, composing A0 and A2) -------------------
// The action routine invoker lets the chooser call "the action's own
// routine" (Sec9.4 step 6, Sec9.4 step 3) without decision.h depending on
// actions.h (which conversely depends on decision.h's types) - the host
// wires HumanAiState::actionRoutine to the real A3 registry.
using ActionRoutineInvoker = std::function<bool(int actionId, ActionBlock& block)>;

class HumanAiState {
public:
    HumanAiState() {
        for (auto& list : goalActionLists) list.fill(ActionBlock::kNoAction);
    }

    // A truly-unknown engine global (Sec9.6 goal 2, Sec8 item 6): never
    // written anywhere per the spec's own OPEN item, so OpenValue rather
    // than an invented default (house convention - see open_state.h).
    OpenValue<double> killNearPlayerRadius{"kill near player radius 0x0135bfc4",
                                            "spec-ai-runtime.md Sec9.6 goal 2 / Sec8 item 6"};

    ModeMachine mode;                   // A0
    PerceptionState perception;         // A2
    BehaviorRecord behavior;
    ForceFlags forceFlags;
    WorldFacts world;

    Vec3 position;              // +0x40
    double health = 0;           // +0x1cb8
    double maxHealth = 0;        // +0x1cac

    // +0x320+4*a: per-action repeat timers (Sec9.7). "Armed and expired"
    // (0x00d9e1e0) gates the chooser; an unarmed timer blocks its action.
    std::array<EngineTimer, action::kCount> actionRepeatTimers;

    // Cover timers (Sec9.8, Sec11.6): +0x964 find-new-cover, +0x968
    // stay-down, +0x96c time-in-cover.
    EngineTimer coverFindNewTimer;
    EngineTimer coverStayDownTimer;
    EngineTimer coverTimeInCoverTimer;

    // ai_goals.xtbl's order_of_actions per goal (Sec9.4 step 6, Sec9.5):
    // NOT loaded by this unit (data-side, out of scope) - defaults to
    // "empty" (every slot ActionBlock::kNoAction), the documented effect of
    // an unauthored goal list. A host seeds real data per goal.
    std::array<std::array<uint8_t, 24>, goal::kCount> goalActionLists;

    ActionRoutineInvoker actionRoutine;  // host-wired to the real A3 registry (actions.h)
    std::function<void()> weaponUpkeepHook;  // Sec9.2 step 3; HYPOTHESIS, no-op by default

    // +0x10 No_interrupt_ms per action (Sec9.1, Sec9.7): read by the
    // decision step, but its VALUES are combat_actions.xtbl data this unit
    // does not load (kActionTable's own comment). Kept per-instance (not in
    // the constexpr kActionTable) so a host can seed real values; defaults
    // to 0 (never waits) for every action - the safe "no timing data" case.
    std::array<int64_t, action::kCount> noInterruptMs{};

    // +0x15 pre_flags per action (Sec9.4's precondition test): CONFIRMED
    // "all 70 masks are 0 in the file image" - that exact default, kept
    // per-instance (combat_actions.xtbl data this unit does not load) so a
    // host/test can seed real medium bits. With every entry 0, no action
    // can ever pass PreconditionTest, matching the documented behaviour
    // exactly (Sec9.4: "an action is choosable only after combat_actions.
    // xtbl has given it at least one medium bit").
    std::array<uint32_t, action::kCount> preFlags{};
};

// --- Sec9.4 step 2: the state mask / precondition test ----------------------------
uint32_t BuildStateMask(const HumanAiState& h);
bool PreconditionTest(uint32_t preFlags, uint32_t stateMask);

struct LiveTargetResult {
    bool has = false;
    uint64_t handle = 0;
};
// 0x004e4e50 (Sec9.8).
LiveTargetResult LiveTarget(const HumanAiState& h);
// 0x004e50e0 (Sec9.6 `group`; also read by A3's actions 27/28, Sec11.3 B).
bool Agree0x004e50e0(const HumanAiState& h);
// 0x004f6310(kind, ...) (Sec9.6 `suppress`; also read by A3's action 40, Sec11.3 G).
bool HasQueuedEventKind(const ModeState& st, int32_t kind);

// --- Sec9.3: the decision gate ----------------------------------------------------
// May call h.mode.request() (the Skydiving forced request, Sec9.3 test 7).
bool Gate(HumanAiState& h, int64_t nowMs);

// --- Sec9.6: the 17 goal tests -----------------------------------------------------
using GoalTestFn = bool (*)(const HumanAiState&, int64_t nowMs);
extern const std::array<GoalTestFn, goal::kCount> kGoalTests;
// Individually exposed (used directly by Sec9.2 step 3's weapon-upkeep
// check, and convenient for tests): same functions as kGoalTests[goal::X].
bool GoalChangeMode(const HumanAiState&, int64_t nowMs);
bool GoalAvoidBrute(const HumanAiState&, int64_t nowMs);
bool GoalKillNearPlayer(const HumanAiState&, int64_t nowMs);
bool GoalSurvive(const HumanAiState&, int64_t nowMs);
bool GoalRetreat(const HumanAiState&, int64_t nowMs);
bool GoalGroup(const HumanAiState&, int64_t nowMs);
bool GoalSuppress(const HumanAiState&, int64_t nowMs);
bool GoalCover(const HumanAiState&, int64_t nowMs);
bool GoalTakingDamage(const HumanAiState&, int64_t nowMs);
bool GoalAdvance(const HumanAiState&, int64_t nowMs);
bool GoalReload(const HumanAiState&, int64_t nowMs);
bool GoalKillEnemy(const HumanAiState&, int64_t nowMs);
bool GoalFlushOut(const HumanAiState&, int64_t nowMs);
bool GoalFrustrated(const HumanAiState&, int64_t nowMs);
bool GoalAcquireTarget(const HumanAiState&, int64_t nowMs);
bool GoalInvestigate(const HumanAiState&, int64_t nowMs);
bool GoalIdle(const HumanAiState&, int64_t nowMs);

// --- Sec9.4: the chooser -----------------------------------------------------------
// Returns an action id, or ActionBlock::kNoAction ("keep the current
// action"). reasonOut/blockOut are Sec9.2 step 5's local block/reason.
uint8_t Chooser(HumanAiState& h, uint8_t& reasonOut, ActionBlock& blockOut, int64_t nowMs);

// --- Sec9.2: the decision step ------------------------------------------------------
void DecisionStep(HumanAiState& h, int64_t nowMs);

// --- Sec4.1 wrapper: the finish routine plus A1's own cool-down roll --------------
// rollFn mirrors 0x00dab660(lo,hi) (Sec4.1 step 1); defaults to the
// deterministic midpoint of [Repeat_min, Repeat_max] (a test-friendly
// placeholder, not a recovered engine value - see ModeMachine's own
// randomRangeMs for the same convention). Returns whatever
// ModeMachine::finishAction returns (did the ordered-completion step run).
bool FinishAction(HumanAiState& h, int64_t nowMs,
                   const std::function<int64_t(int64_t lo, int64_t hi)>& rollFn = nullptr);

}  // namespace sr3airuntime
