// Synthetic tests (2026-10-09) for the live respect/progression runtime
// state added to include/sr3luahost/engine_state.h:
// spec-tables-progression.md Sec5.4's award path (awardRespect), level-up
// loop (applyRespectLevelUp, reusing respectLevelCost() == the SAME
// 0x0060eb40/0x0060eb60 accessor Sec5.2 already names for the save-shaped
// path), unlock consumption (consumeRespectLevelUnlocks, with the
// "HomieSlot1" special case), the level cap at kRespectLevelCap (50), and
// the new decade-level (10/20/30/40/50) milestone hook
// (runRespectDecadeMilestone) - write-side only, no consumer modeled, per
// that section's own explicit "flagged OPEN" scope. Also covers the
// spec-lua-api-behaviour.md Sec16.2/Sec16.16 correction (0x006ced90/
// 0x006cede0 are a co-op progression mirror, not a UI popup) at the one
// place this project has an observable effect for it:
// respectGrantCoopMirrorAttemptCount().
//
// No game data: every per-level Respect cost and Unlockables list below is
// a small, clearly-synthetic fixture built directly through
// EngineState::respectLevelTable() (this project never reads a real
// respect_levels.xtbl here, same "tests supply the per-level records
// directly" convention the header's own top-of-file comment states).
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "sr3luahost/engine_state.h"

using namespace sr3luahost;

namespace {

int g_failures = 0;
#define CHECK(cond)                                                                           \
    do {                                                                                      \
        if (!(cond)) {                                                                        \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                     \
        }                                                                                     \
    } while (0)

// Builds a small synthetic respect_levels.xtbl-shaped table: level row i
// (0-based) costs (i+1)*100 respect and, for decade rows only (index 9 =
// "row 10"), carries a couple of named unlockables including the
// "HomieSlot1" special case at row 9 (so levelling from 9->10 exercises it).
std::vector<sr3tables_progression::RespectLevel> makeFixtureTable(size_t rows) {
    std::vector<sr3tables_progression::RespectLevel> t;
    t.reserve(rows);
    for (size_t i = 0; i < rows; ++i) {
        sr3tables_progression::RespectLevel row;
        // CONFIRMED real-world shape (Sec5.4/Sec14.5): the shipped table's
        // LAST row has its own Respect = 0 (L50 = 0) - the lifetime cap
        // formula (Sigma(all rows) - last row's own Respect) and the
        // "instant, same-call" final level-up both depend on this, so the
        // fixture mirrors it on its own last row rather than extending the
        // uniform (i+1)*100 progression all the way through.
        row.respect.value = (i + 1 == rows) ? 0 : static_cast<int32_t>((i + 1) * 100);
        row.respect.present = true;
        if (i == 0) {
            row.unlockables = {"Outfit_Street"};
        } else if (i == 9) {
            row.unlockables = {"HomieSlot1", "Weapon_Unlock_Pistol"};
        } else {
            row.unlockables = {"Unlock_" + std::to_string(i)};
        }
        t.push_back(row);
    }
    return t;
}

} // namespace

int main() {
    // --- (a) Sub-threshold awards just increase banked amount; total also
    // climbs; level stays put; no unlock/hint/milestone side effects fire. -
    {
        EngineState es;
        es.respectLevelTable() = makeFixtureTable(50);
        es.awardRespect(50); // row 0 costs 100 - below threshold
        CHECK(es.respectTotal() == 50);
        CHECK(es.respectBanked() == 50);
        CHECK(es.respectLevel() == 0);
        CHECK(es.statRecordOpenCount(EngineState::kRespectAwardStatId) == 1); // stat pipeline still fires on every award
        CHECK(es.respectDecadeMilestone().fireCount == 0);
        CHECK(es.respectUnlockApplied("Outfit_Street") == false);

        es.awardRespect(40); // 50+40=90, still < 100
        CHECK(es.respectTotal() == 90);
        CHECK(es.respectBanked() == 90);
        CHECK(es.respectLevel() == 0);
        CHECK(es.statRecordOpenCount(EngineState::kRespectAwardStatId) == 2);
    }

    // --- (b) Crossing a level threshold increments level, carries the
    // remainder, and consumes that level's unlocks (row 0 -> level 1). -----
    {
        EngineState es;
        es.respectLevelTable() = makeFixtureTable(50);
        es.awardRespect(130); // row 0 costs 100 -> level 1, 30 banked carried over
        CHECK(es.respectTotal() == 130);
        CHECK(es.respectLevel() == 1);
        CHECK(es.respectBanked() == 30);
        CHECK(es.respectUnlockApplied("Outfit_Street") == true); // row 0's own unlock, consumed at the level-up that paid row 0's cost
        CHECK(es.respectHomieSlot1UnlockCount() == 0); // not the HomieSlot1 row
        CHECK(es.respectDecadeMilestone().fireCount == 0); // level 1 is not a decade level

        // A second award that does NOT cross another threshold just banks.
        es.awardRespect(50); // banked 30+50=80, row 1 costs 200 - stays level 1
        CHECK(es.respectLevel() == 1);
        CHECK(es.respectBanked() == 80);
    }

    // Multi-level crossing in one award: from level 0, a big award should
    // cross several thresholds in one call, each consuming its OWN row's
    // unlocks, carrying the remainder through every step.
    {
        EngineState es;
        es.respectLevelTable() = makeFixtureTable(50);
        // Costs: row0=100, row1=200, row2=300. 100+200+300=600; +50 leftover.
        es.awardRespect(650);
        CHECK(es.respectLevel() == 3);
        CHECK(es.respectBanked() == 50);
        CHECK(es.respectUnlockApplied("Outfit_Street"));
        CHECK(es.respectUnlockApplied("Unlock_1"));
        CHECK(es.respectUnlockApplied("Unlock_2"));
        CHECK(es.respectUnlockApplied("Unlock_3") == false); // row 3 (level 4's cost row) not yet paid
    }

    // --- (c) "HomieSlot1" is distinguishable from an ordinary unlock. ------
    {
        EngineState es;
        es.respectLevelTable() = makeFixtureTable(50);
        // Sum of rows 0..8 costs = 100+...+900 = 4500. Exactly reaching that
        // pays row 9's cost too? No: reaching level 9 consumes ROWS 0..8 and
        // leaves level at 9 with 0 banked; row 9 (HomieSlot1's row) is only
        // consumed when the level-up that PAYS row 9's cost (1000) happens,
        // i.e. going from level 9 to level 10.
        int32_t sumRows0to8 = 0;
        for (int i = 0; i < 9; ++i) sumRows0to8 += static_cast<int32_t>((i + 1) * 100);
        es.awardRespect(sumRows0to8);
        CHECK(es.respectLevel() == 9);
        CHECK(es.respectHomieSlot1UnlockCount() == 0);
        CHECK(es.respectUnlockApplied("HomieSlot1") == false);

        es.awardRespect(1000); // row 9 costs (9+1)*100 = 1000 -> level 10, HomieSlot1 row consumed
        CHECK(es.respectLevel() == 10);
        CHECK(es.respectUnlockApplied("HomieSlot1") == true);
        CHECK(es.respectHomieSlot1UnlockCount() == 1); // the DISTINCT counter, separate from the generic flag
        CHECK(es.respectUnlockApplied("Weapon_Unlock_Pistol") == true); // ordinary unlock on the SAME row
        // Re-awarding past an already-unlocked level never double-counts
        // the distinct HomieSlot1 notification ("not yet applied" gate).
        EngineState es2;
        es2.respectLevelTable() = makeFixtureTable(50);
        es2.awardRespect(sumRows0to8 + 1000 + 50); // cross into level 10 then bank 50 more
        CHECK(es2.respectHomieSlot1UnlockCount() == 1);
        es2.awardRespect(1); // still level 10, no new level-up, no re-consumption
        CHECK(es2.respectHomieSlot1UnlockCount() == 1);
    }

    // --- (d) Level cap at 50 AND the real lifetime-total cap (Sec5.4,
    // 2026-10-09 WAVE 10 follow-up) are both enforced; a huge award cannot
    // push past either, and the cost accessor's own "22000 for n >= loaded
    // count" fallback is honored once the table itself is exhausted. ------
    {
        EngineState es;
        es.respectLevelTable() = makeFixtureTable(50); // exactly 50 rows, indices 0..49
        CHECK(es.respectLevelCost(49) == 0);           // last real row's own Respect is 0 (mirrors the real shipped table's L50=0, Sec14.5)
        CHECK(es.respectLevelCost(50) == 22000);        // past the loaded table - CONFIRMED fallback (Sec5.2)

        // Sum of rows 0..48: 100+200+...+4900 = 100*(1+...+49) = 100*1225 = 122500;
        // row 49 contributes 0. CONFIRMED (Sec5.4): the lifetime cap is
        // Sigma(all rows) - last row's own Respect = 122500 - 0 = 122500.
        CHECK(es.respectLifetimeCap() == 122500);
        es.awardRespect(122500 + 999999); // absurdly large - the grant itself must CLAMP to the cap, not just the level
        CHECK(es.respectTotal() == 122500); // CLAMPED grant (cap - 0), not the raw huge amount
        CHECK(es.respectLevel() == 50);      // row 49's own 0 cost lets the SAME call's level-up loop run straight through to 50
        CHECK(es.respectLevel() <= EngineState::kRespectLevelCap);

        // A further award once the lifetime cap is reached is a FULL skip,
        // not a bank (CONFIRMED, Sec5.4: "grants nothing at all... a full
        // skip, not a 0-clamp") - total/banked/stat-count all stay exactly
        // as they were.
        uint32_t totalAtCap = es.respectTotal();
        uint32_t bankedAtCap = es.respectBanked();
        uint64_t statCountAtCap = es.statRecordOpenCount(EngineState::kRespectAwardStatId);
        es.awardRespect(12345);
        CHECK(es.respectLevel() == 50);
        CHECK(es.respectTotal() == totalAtCap);
        CHECK(es.respectBanked() == bankedAtCap);
        CHECK(es.statRecordOpenCount(EngineState::kRespectAwardStatId) == statCountAtCap);
    }

    // A non-positive award is a guarded no-op (HOST-SAFETY), not a crash or
    // a silent negative total.
    {
        EngineState es;
        es.respectLevelTable() = makeFixtureTable(50);
        es.awardRespect(0);
        es.awardRespect(-500);
        CHECK(es.respectTotal() == 0);
        CHECK(es.respectBanked() == 0);
        CHECK(es.respectAwardNonPositiveGuardCount() == 2);
        CHECK(es.statRecordOpenCount(EngineState::kRespectAwardStatId) == 0); // guarded before the stat pipeline
    }

    // --- (e) The decade-level milestone hook fires only on 10/20/30/40/50,
    // and only inside the open-world zone; it stashes the event id + world
    // position, write-side only (no consumer invented). -------------------
    {
        EngineState es;
        es.respectLevelTable() = makeFixtureTable(50);
        es.respectMilestonePlayerPosition() = WorldPosition{111.0, 222.0, 333.0};

        // Sum of rows 0..8 = 4500 (level 9, NOT a decade level).
        int32_t sumRows0to8 = 0;
        for (int i = 0; i < 9; ++i) sumRows0to8 += static_cast<int32_t>((i + 1) * 100);
        es.awardRespect(sumRows0to8);
        CHECK(es.respectLevel() == 9);
        CHECK(es.respectDecadeMilestone().fireCount == 0);
        CHECK(es.respectDecadeMilestone().lastEventId == 0);

        es.awardRespect(1000); // -> level 10, a decade level
        CHECK(es.respectLevel() == 10);
        CHECK(es.respectDecadeMilestone().fireCount == 1);
        CHECK(es.respectDecadeMilestone().lastEventId == 0x22);
        CHECK(es.respectDecadeMilestone().lastPosition.x == 111.0);
        CHECK(es.respectDecadeMilestone().lastPosition.y == 222.0);
        CHECK(es.respectDecadeMilestone().lastPosition.z == 333.0);

        // Level 50 specifically uses event id 0x23, not 0x22.
        EngineState es50;
        es50.respectLevelTable() = makeFixtureTable(50);
        es50.respectMilestonePlayerPosition() = WorldPosition{1.0, 2.0, 3.0};
        es50.awardRespect(122500 + 1); // reach exactly level 50 (see test (d) above: cap=122500, the surplus is clamped away)
        CHECK(es50.respectLevel() == 50);
        CHECK(es50.respectDecadeMilestone().lastEventId == 0x23);
        CHECK(es50.respectDecadeMilestone().fireCount == 5); // levels 10,20,30,40,50 each fired once

        // Outside the open-world zone, the hook never writes, even across a
        // decade level - the gate is CONFIRMED real (Sec5.4: "only inside
        // the sr3_city open-world zone"), not a no-op stand-in.
        EngineState esIndoor;
        esIndoor.respectLevelTable() = makeFixtureTable(50);
        esIndoor.respectDecadeMilestone().inOpenWorldZone = false;
        esIndoor.awardRespect(sumRows0to8 + 1000); // crosses into level 10
        CHECK(esIndoor.respectLevel() == 10);
        CHECK(esIndoor.respectDecadeMilestone().fireCount == 0);
        CHECK(esIndoor.respectDecadeMilestone().lastEventId == 0);

        // Non-decade levels in between a decade crossing never fire either
        // (checked above at level 9); levels 11..19 likewise should not.
        es.awardRespect(1100 /* row10 cost */); // -> level 11, not a decade level
        CHECK(es.respectLevel() == 11);
        CHECK(es.respectDecadeMilestone().fireCount == 1); // unchanged since level 10
    }

    // --- Level-up notification reuses the EXISTING 210-message hint/
    // tutorial dispatcher (hint id 0xa9), not a bespoke mechanism. ---------
    {
        EngineState es;
        es.respectLevelTable() = makeFixtureTable(50);
        CHECK(es.tutorialAdvanceCount(EngineState::kRespectLevelUpHintId) == 0);
        es.awardRespect(100); // row 0 cost exactly -> one level-up
        CHECK(es.respectLevel() == 1);
        CHECK(es.tutorialAdvanceCount(EngineState::kRespectLevelUpHintId) == 1);
        es.awardRespect(200); // row 1 cost exactly -> a second level-up
        CHECK(es.respectLevel() == 2);
        CHECK(es.tutorialAdvanceCount(EngineState::kRespectLevelUpHintId) == 2);
    }

    // --- Sec16.2/Sec16.16 correction: 0x006ced90 is a co-op progression
    // mirror attempt, not a UI popup - this project counts the resolved
    // call reaching it (notifyRequested==true) exactly as it already did,
    // now correctly documented and exposed for a test to observe. --------
    {
        EngineState es;
        CHECK(es.respectGrantCoopMirrorAttemptCount() == 0);
        es.recordRespectGrant(/*targeted=*/false, /*notifyRequested=*/true);
        CHECK(es.respectGrantCoopMirrorAttemptCount() == 1);
        es.recordRespectGrant(/*targeted=*/true, /*notifyRequested=*/false);
        CHECK(es.respectGrantCoopMirrorAttemptCount() == 1); // unchanged - no notify requested this time
        CHECK(es.respectGrantCount() == 2);
    }

    if (g_failures == 0) std::cout << "ALL respect-progression TESTS PASSED\n";
    return g_failures == 0 ? 0 : 1;
}
