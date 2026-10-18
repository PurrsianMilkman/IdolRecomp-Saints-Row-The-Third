// EngineState slice: respect, achievements, unlocks, DLC counters, stats.
//
// Split out of engine_state.h (refactor step 3a, 2026-10-17) as a zero-behaviour-change move: every
// type, field, initialiser and comment below moved verbatim (namespace-scope types re-indented one
// level). Types that were nested in EngineState are now namespace-scope types in sr3luahost;
// EngineStateProgressionData re-exports each one under its old name, so `EngineState::Type` still
// resolves. EngineState inherits EngineStateProgressionData publicly, so `state.member`,
// `EngineState::kConstant` and `state.accessor()` are unchanged for every caller. Members that
// were private in EngineState are `protected` here (the only access change). Method
// declarations stay in EngineState (engine_state.h); bodies are in src/*.cpp.
#pragma once

#include "sr3luahost/engine_state_common.h"
#include "sr3progression/respect.h"

namespace sr3luahost {

// The decade-level (10/20/30/40/50) milestone hook, `0x00bd3150`
// (Sec5.4, new finding this pass): fires on every level crossing, but
// only WRITES when the newly reached level is a decade level AND the
// player is inside the `sr3_city` open-world zone - stashing a distinct
// event-state id (0x22, or 0x23 specifically for level 50) plus the
// player's own world position into a pair of globals. Write-side only,
// per this task's own brief: the actual consumer (a cutscene, a radio
// line, a scripted encounter) was never traced in the spec pass and is
// deliberately NOT invented here.
struct RespectDecadeMilestone {
    // CHOSEN: no live "current zone" tracker exists anywhere else in
    // this project to read instead (checked: no hit for "current
    // zone"/"open world" state in this header set) - a minimal,
    // test-settable stand-in, default true (this project's own
    // MainMenuNewGame::worldName already hardcodes "sr3_city" as the
    // CONFIRMED default world, Sec41.2, so "in the open world" is the
    // natural default here too, not a guess at the real initial
    // value).
    bool inOpenWorldZone = true;
    int32_t lastEventId = 0;      // 0 = never fired; else 0x22 or 0x23 (level 50)
    WorldPosition lastPosition;   // player's own world position at the moment of the write
    uint64_t fireCount = 0;       // how many times the write actually happened (not every level crossing)
};

// --- dlc3_m03_punch_load / _unload (Sec55.5) ---------------------------
// "load/unload a UI document... by a bitmask argument (bit 0 = local,
// bit 1 = broadcast)." CONFIRMED logic defect: "_load overwrites its
// stored document-id global without checking it was already set" -
// modelled as a plain unconditional overwrite (the "leak" IS this
// absence of a check, not a separate thing to simulate).
struct Dlc3Punch {
    bool loaded = false;
    uint64_t loadCount = 0;
    uint64_t unloadCount = 0;
    uint64_t broadcastCount = 0; // bit 1 of either mask argument
};

struct EngineStateProgressionData {
    // Former EngineState nested types (defined above at namespace scope), re-exported so
    // `EngineState::Type` keeps naming them.
    using RespectDecadeMilestone = sr3luahost::RespectDecadeMilestone;
    using Dlc3Punch = sr3luahost::Dlc3Punch;

public:

    // Sec28.10 `dlc3_m03_set_sprint_waning`: 0x0263ae3a, CONFIRMED zero at start.
    bool& dlc3SprintWaning() { return dlc3SprintWaning_; }

    // dlc2_m02_clapboards_set/_get/_reset (Sec55.5, tranche 22). CONFIRMED:
    // a 10-byte flag array plus a count, file-default count -1 ("so every
    // index fails the upper bound too" before the first _reset call). Only
    // _get and _reset show a real mission-drive hit this pass; _set does
    // not and stays a generic stub per this pass's own scope rule, so
    // _set's own separate out-of-bounds WRITE shape is deliberately not
    // reproduced or guarded here. _get's CONFIRMED upper-bound check (out
    // of range -> "no value at all", not nil/false pushed, not even a
    // boolean) is modeled directly; _get's own missing LOWER bound (any
    // argument <= 0 is the real arbitrary-read defect) is a HOST-SAFETY
    // DEVIATION refused here instead of reproduced - this project does not
    // simulate crashes/arbitrary reads (the established group_get_next_npc
    // / Sec28.5 precedent this task's own instructions name directly) -
    // counted separately from the ordinary upper-bound "out of range" case.
    int dlc2ClapboardCount() const { return dlc2ClapboardCount_; }
    int dlc2ClapboardsGetNegativeIndexGuardCount() const { return dlc2ClapboardsGetNegativeIndexGuardCount_; }

    // mission_unlock (Sec15.9) / mission_set_completed (Sec15.10): both
    // resolve their mission-name argument via objectResolves() (this
    // project's flat Sec29 stand-in for the newly-identified mission
    // resolver 0x005f4c30 - structurally identical to the vehicle resolver,
    // Sec15.9's own cross-reference).
    //
    // Sec21.27b (2026-10-08) FOUND the real writer of the already-
    // documented completion flag: `0x006d6870`, a single unconditional
    // "byte = (byte & 0xf7) | 0x44" on the mission handle's own `+0x88`
    // byte (clears bit 0x8, sets bits 0x4 "completed" and 0x40 together,
    // always, no gating) - and CONFIRMED, via mission_unlock's own bit-0x1
    // "unlocked" setter testing/setting a DIFFERENT bit of the SAME byte,
    // that `+0x88` is one shared packed multi-bit flags field, not three
    // independent facts. This project previously modeled mission_unlock
    // (missionUnlocked_), mission_set_completed (missionCompletedMap_) and
    // cell_is_mission_complete/mission_is_complete (missionComplete_, an
    // OpenValueMap, Sec27.2/Sec20.14) as THREE SEPARATE, never-cross-wired
    // maps standing in for that one real byte - exactly the kind of
    // invented-parallel-field duplication this header's own top note warns
    // against. Fixed here: missionFlagBits_ is the one real packed-bits
    // store; writeMissionCompletionFlagBits()/writeMissionUnlockedFlagBit()
    // are its only two writers (0x006d6870's own real shape, and
    // mission_unlock's own bit, respectively), and BOTH also update the
    // pre-existing per-native facades below them so every one of this
    // header's own already-shipped tests (which read/write those facades
    // directly) keeps working unchanged - missionComplete_ in particular
    // keeps its own pre-existing "OPEN/throws until written" semantics for
    // a name NOTHING has ever completed (Sec27.2's own test coverage
    // relies on exactly that); it is simply now also .set() by the real
    // writers below, not only by a test. missionEndDispatch() above
    // (Sec21.27b's type-1/2 branches) is the OTHER real production caller
    // of writeMissionCompletionFlagBits()/writeMissionUnlockedFlagBit().
    static constexpr uint8_t kMissionFlagUnlocked = 0x1;            // mission_unlock, Sec15.9
    uint32_t gameChoiceBitmaskValue() const { return gameChoiceBitmask_; }
    bool gameChoiceBitValue(uint8_t index) const { return index < 32 && (gameChoiceBitmask_ & (1u << index)) != 0; }
    uint64_t gameChoiceSetCount() const { return gameChoiceSetCount_; }

    // dlc_achievement_counter_set (Sec15.27). HARDENING POLICY (CHOSEN
    // hardened divergence): Sec15.27's own text is explicit - "if arg1 is
    // < 3, writes arg2 directly into a global array of exactly 3 dword
    // slots ... indexed by arg1. No bounds error" - the real check (`< 3`)
    // does NOT exclude a NEGATIVE arg1 (e.g. -5 < 3 is also true), so the
    // real binary performs an unchecked, script-controlled OOB write
    // relative to the 3-slot array for any negative index - a genuine
    // memory-safety bug, not a HYPOTHESIS. Per the orchestrator's standing
    // hardening policy: every in-range index (0/1/2) is replicated exactly
    // (direct write, no further effect); the negative-index OOB write is
    // refused (skipped, not replicated) rather than corrupting adjacent
    // host memory, since there is no safe way to know what the real
    // write's target address would have overlapped in this project's own
    // memory layout. index >= 3 is NOT a divergence - it is the real
    // binary's own CONFIRMED, correct no-op (the `< 3` check already
    // excludes it).
    static constexpr int32_t kDlcAchievementCounterSlotCount = 3;
    uint64_t dlcAchievementCounterNegativeIndexGuardCount() const { return dlcAchievementCounterNegativeIndexGuardCount_; }

    // game_award_respect (Sec16.2): the real applied-amount formula
    // (count * an unspecified global multiplier, 0x012fd1b0, then fed
    // through 2 further calls whose OWN explicit-argument call shape is
    // itself a disassembly-confirmed anomaly against their established
    // zero-argument shape elsewhere - Sec16.2's own 2 flagged OPEN items)
    // is genuinely unknown, and unlike this batch's other write-only
    // fields, the RETURN VALUE here is directly consumed by the calling
    // Lua script - a wrong number would misrepresent a CONFIRMED-vs-
    // guessed fact to that script, so this project refuses rather than
    // guesses at the multiplier (OpenValue, test-settable) while fully
    // implementing the CONFIRMED argument shape and routing/notify side
    // effects around it.
    OpenValue<double>& respectMultiplier() { return respectMultiplier_; }
    uint64_t respectGrantCount() const { return respectGrantCount_; }
    // Test/diagnostic accessor for the corrected co-op-mirror-attempt count
    // documented above (same counter recordRespectGrant increments).
    uint64_t respectGrantCoopMirrorAttemptCount() const { return respectGrantNotifyCount_; }

    // --- Sec5.4 respect/progression live runtime (spec-tables-progression.md,
    // 2026-10-09, CONFIRMED by disassembly) -------------------------------
    //
    // Real live player-object fields (global pointer 0x0262edfc, getter
    // 0x009da4e0), independently re-derived via a different accessor chain
    // than spec-save-format.md Sec10.3's own SaveGameState fields (closing a
    // cross-check that was open between the two specs - the two mirror each
    // other exactly, but are two genuinely separate live objects in the
    // real engine, so this project keeps them as two separate structs too,
    // same "don't silently alias an unconfirmed cross-reference" convention
    // as Sec4.13's own door_open/teleport precedent): `+0x1ed0` total
    // respect, `+0x1ed4` respect banked inside the current level, `+0x1ed8`
    // current level (0..50). `0x009db8b0` is a one-line accessor returning
    // `*(playerObj+0x1ed8)`.
    uint32_t respectTotal() const { return respectTotal_; }
    uint32_t respectBanked() const { return respectBanked_; }
    uint32_t respectLevel() const { return respectLevel_; }
    // Save load (spec-save-format.md A.1 entry 1): total / in-level / level are written back
    // verbatim, no recomputation. Added by Team C for the viewer's --load-save (HANDOFF-C.md
    // section 6 step 5).
    void restoreRespectFromSave(uint32_t total, uint32_t inLevel, uint32_t level) {
        respectTotal_ = total;
        respectBanked_ = inLevel;
        respectLevel_ = level;
    }

    // The loaded level count 0x0060eba0() == 50 - CONFIRMED (Sec5.2/Sec5.4),
    // never a table-size-derived value (the real loader "stops storing at
    // 50" even if a row beyond it existed).
    static constexpr uint32_t kRespectLevelCap = 50;
    // stats.xtbl stat id this project has CONFIRMED drives on every respect
    // award (Sec5.4's own new finding), Sec1.1.
    static constexpr int32_t kRespectAwardStatId = 0x44;
    // hint id 0xa9's own 0-based index into the 210-entry tutorial/hint
    // table (Sec10.4/Sec16.3's own 0x0151d600 table; Sec5.4: "the SAME
    // 210-message HUD hint/tutorial dispatcher").
    static constexpr int kRespectLevelUpHintId = 0xA9;

    // `respect_levels.xtbl`'s per-level cost/unlock table (sr3tables_
    // progression::RespectLevel, included above) - the SAME table Sec5.2
    // names for the save-shaped path and Sec5.4 confirms the live runtime
    // path reuses verbatim via 0x0060eb60 (not a second algorithm). This
    // project has no real xtbl-reading wired to this subsystem (same "out
    // of scope" footing as collectibles.xtbl's own Respect_Award fields,
    // Sec5.4's own "Open items"), so a test populates it directly.
    std::vector<sr3tables_progression::RespectLevel>& respectLevelTable() { return respectLevelTable_; }
    // RP-6 (spec-tables-progression.md B.3 item 1): feed the REAL respect_levels.xtbl rows (parsed, 50 max) here. Once set,
    // awardRespect / applyRespectLevelUp run sr3progression::AwardRespect / ApplyLevelUp over it instead of the legacy
    // test-table path. Not yet called by the viewer: the xtbl load site belongs to the host start-up (see report).
    void setRealRespectTable(const std::vector<sr3tables_progression::RespectLevel>& rows) {
        respectLevelTable_ = rows;
        realRespectTable_ = sr3progression::RespectTable::FromParsed(rows);
        realRespectTableActive_ = true;
    }
    bool realRespectTableActive() const { return realRespectTableActive_; }
    // The literal name `"HomieSlot1"` (CONFIRMED, Sec5.4) gets its own
    // distinct notification at the moment it is unlocked, separate from
    // the generic per-name flag above so a test can tell the two apart -
    // this counter increments ONLY for that literal name, never for an
    // ordinary unlock.
    uint64_t respectHomieSlot1UnlockCount() const { return respectHomieSlot1UnlockCount_; }
    void recordStatRecordOpen(int32_t statId) { ++statRecordOpenCounts_[statId]; }
    RespectDecadeMilestone& respectDecadeMilestone() { return respectDecadeMilestone_; }
    // CHOSEN: the player's own live position this hook would stash is not
    // otherwise tracked for the local player anywhere in this project
    // (CharacterState::worldPosition is keyed per-name and per-call, not a
    // standing "local player position," Sec4.2) - a test sets this directly
    // to whatever position the award happens at, same "write-only, no
    // in-scope reader to resolve a real value from" convention as
    // playerSkydiveFallSpeed_ above (Sec15.18).
    WorldPosition& respectMilestonePlayerPosition() { return respectMilestonePlayerPosition_; }

    // Award path, CONFIRMED (Sec5.4): the Lua native `game_award_respect`
    // (0x00843bf0) -> 0x009da4e0 (resolve local player) -> 0x009db8e0's own
    // caller 0x009e2440 (full body opened): cap-checks `amount`, updates
    // the running total via 0x009db8c0, calls the level-up routine below,
    // and opens a stat-system record for stat id 0x44 on every award (not
    // only a successful level-up).
    uint64_t respectAwardNonPositiveGuardCount() const { return respectAwardNonPositiveGuardCount_; }

    // hud_achievement_show_progress (Sec13.22): arg 1 is a data-table key
    // hashed via sr3save::nameHash, looked up in a small fixed achievement
    // roster this project has no real contents for - test-registrable only
    // (same "never-populated object space" precedent). An unregistered
    // name is a real, CONFIRMED no-match no-op.
    std::unordered_set<std::string>& knownAchievementsForTesting() { return knownAchievements_; }
    uint64_t& achievementToastCount() { return achievementToastCount_; }
    uint64_t& achievementReplicateCount() { return achievementReplicateCount_; }
    // current==total, 1 or 2 unidentified completion handlers fire
    // (Sec13.22's own OPEN item re: which bits of arg 4 select which) - not
    // individually counted, one shared counter.
    uint64_t& achievementCompletionCount() { return achievementCompletionCount_; }
    uint64_t dlcAchievementCounterGetNegativeIndexGuardCount() const { return dlcAchievementCounterGetNegativeIndexGuardCount_; }
    int dlc2ClapboardsSetNegativeIndexGuardCount() const { return dlc2ClapboardsSetNegativeIndexGuardCount_; }
    Dlc3Punch& dlc3Punch() { return dlc3Punch_; }

    // --- Sec41.2/Sec44.5 get_current_difficulty/game_difficulty_select: CONFIRMED
    // file-default 1; the real setter only accepts an unsigned value < 3.
    int32_t& difficultyLevel() { return difficultyLevel_; }
    OpenValue<double>& worldIncomeDollars() { return worldIncomeDollars_; }
    std::unordered_map<int32_t, int32_t>& cheatContentId() { return cheatContentId_; }

protected: // private in EngineState before the split
    bool dlc3SprintWaning_ = false; // Sec28.10, CONFIRMED zero at start
    std::array<bool, 10> dlc2ClapboardFlags_{};        // Sec55.5, zero-fill (file-default)
    int32_t dlc2ClapboardCount_ = -1;                  // Sec55.5, CONFIRMED file-default -1
    int dlc2ClapboardsGetNegativeIndexGuardCount_ = 0; // HOST-SAFETY, test-observable only
    std::unordered_map<std::string, bool> missionUnlocked_;       // Sec15.9 - legacy facade, see writeMissionUnlockedFlagBit()
    uint32_t gameChoiceBitmask_ = 0;                      // Sec15.26
    uint64_t gameChoiceSetCount_ = 0;
    uint64_t gameChoiceIndexOutOfRangeGuardCount_ = 0;    // HOST-SAFETY
    uint64_t gameChoiceIndex0NotifyCount_ = 0;
    uint64_t gameChoiceIndex1AchievementHookAttemptCount_ = 0;
    std::array<int32_t, kDlcAchievementCounterSlotCount> dlcAchievementCounters_{}; // Sec15.27
    uint64_t dlcAchievementCounterNegativeIndexGuardCount_ = 0; // CHOSEN (hardened divergence)
    OpenValue<double> respectMultiplier_{"game_award_respect multiplier global 0x012fd1b0", "spec-lua-api-behaviour.md Sec16.2"};
    uint64_t respectGrantCount_ = 0;
    uint64_t respectGrantTargetedCount_ = 0;
    uint64_t respectGrantBroadcastCount_ = 0;
    uint64_t respectGrantNotifyCount_ = 0;

    // Sec5.4 respect/progression live runtime (see the public API block
    // above, near recordRespectGrant, for every field's own doc comment).
    uint32_t respectTotal_ = 0;   // +0x1ed0
    uint32_t respectBanked_ = 0;  // +0x1ed4
    uint32_t respectLevel_ = 0;   // +0x1ed8, 0..kRespectLevelCap
    bool realRespectTableActive_ = false;
    sr3progression::RespectTable realRespectTable_;
    std::vector<sr3tables_progression::RespectLevel> respectLevelTable_;
    std::unordered_map<std::string, bool> respectUnlockablesFlag_;
    uint64_t respectHomieSlot1UnlockCount_ = 0;
    std::unordered_map<int32_t, uint64_t> statRecordOpenCounts_;
    RespectDecadeMilestone respectDecadeMilestone_;
    WorldPosition respectMilestonePlayerPosition_;
    uint64_t respectAwardNonPositiveGuardCount_ = 0;
    std::unordered_set<std::string> knownAchievements_;
    uint64_t achievementToastCount_ = 0;
    uint64_t achievementReplicateCount_ = 0;
    uint64_t achievementCompletionCount_ = 0;

    // =========================================================================
    // Wave 2, Batch W2-3 (2026-10-08), spec-lua-api-behaviour.md Sec20+Sec21 -
    // backing storage for the public accessors declared above.
    // =========================================================================
    uint64_t dlcAchievementCounterGetNegativeIndexGuardCount_ = 0;
    int dlc2ClapboardsSetNegativeIndexGuardCount_ = 0;
    Dlc3Punch dlc3Punch_;
    int32_t difficultyLevel_ = 1; // CONFIRMED file-default
    OpenValue<double> worldIncomeDollars_{"world income dollars", "spec-lua-api-behaviour.md Sec53.5"};
    std::unordered_map<int32_t, int32_t> cheatContentId_;
};

} // namespace sr3luahost
