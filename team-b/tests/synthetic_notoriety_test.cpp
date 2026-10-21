// Synthetic tests for the shared notoriety tables, spec-ai-runtime.md Sec19
// (include/sr3airuntime/notoriety.h): the heat-code table's TRACK-indexing and
// the real shipped level limits / decay rates (spec-tables-progression.md
// Sec14.6). Gain/decay behaviour is covered by the NotorietyEscalation tests
// (Sec19.8); the old NotorietyDriver tests were removed with that class.

#include <iostream>

#include "sr3airuntime/notoriety.h"

using namespace sr3airuntime;

namespace {

int g_failures = 0;
#define CHECK(cond)                                                                           \
    do {                                                                                      \
        if (!(cond)) {                                                                        \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                     \
        }                                                                                     \
    } while (0)

// The heat-code table is TRACK-indexed, not level-indexed (Sec19.4 "two
// further corrections").
void testHeatCodeTableValuesByTrack() {
    CHECK(HeatCodeForTrack(NotorietyTrack::kLuchadores) == 1);
    CHECK(HeatCodeForTrack(NotorietyTrack::kDeckers) == 2);
    CHECK(HeatCodeForTrack(NotorietyTrack::kMorningstar) == 3);
    CHECK(HeatCodeForTrack(NotorietyTrack::kPolice) == 5);
}

void testRealLevelLimitsAndSpansForGangAndPolice() {
    // Sec14.6 real cumulative limits.
    CHECK(kGangLevelTable.levelLimit[1] == 75 && kPoliceLevelTable.levelLimit[1] == 75);
    CHECK(kGangLevelTable.levelLimit[2] == 225 && kPoliceLevelTable.levelLimit[2] == 300);
    CHECK(kGangLevelTable.levelLimit[3] == 500 && kPoliceLevelTable.levelLimit[3] == 700);
    CHECK(kGangLevelTable.levelLimit[4] == 1200 && kPoliceLevelTable.levelLimit[4] == 1200);
    CHECK(kGangLevelTable.levelLimit[5] == 2500 && kPoliceLevelTable.levelLimit[5] == 2500);

    // Decay rates diverge at every level 1-5 (Sec14.6: "set 1 decays faster").
    CHECK(kPoliceLevelTable.decayRate[1] > kGangLevelTable.decayRate[1]);
    CHECK(kPoliceLevelTable.decayRate[2] > kGangLevelTable.decayRate[2]);
    CHECK(kPoliceLevelTable.decayRate[5] > kGangLevelTable.decayRate[5]);

    CHECK(NotorietyLevelSpan(kGangLevelTable, 0) == 75);
    CHECK(NotorietyLevelSpan(kPoliceLevelTable, 1) == 225);  // 300 - 75
}

}  // namespace

void run(const char* name, void (*fn)()) {
    try {
        fn();
    } catch (const std::exception& ex) {
        std::cerr << "CHECK FAILED: " << name << " threw: " << ex.what() << "\n";
        ++g_failures;
    }
}

int main() {
    run("testHeatCodeTableValuesByTrack", testHeatCodeTableValuesByTrack);
    run("testRealLevelLimitsAndSpansForGangAndPolice", testRealLevelLimitsAndSpansForGangAndPolice);

    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed.\n";
        return 1;
    }
    std::cout << "All synthetic notoriety tests passed.\n";
    return 0;
}
