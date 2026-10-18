// Shared declarations for the synthetic_ai_runtime_test split (one executable, several
// translation units): the failure counter, CHECK, the per-section test entry points and the
// helpers shared across sections. Moved verbatim from the former single-file test.
#pragma once

#include <iostream>
#include <string>

#include "sr3airuntime/actions.h"
#include "sr3airuntime/decision.h"
#include "sr3airuntime/mode_table.h"
#include "sr3airuntime/perception.h"
#include "sr3luahost/open_state.h"

using namespace sr3airuntime;
using sr3luahost::OpenStateError;
#define CHECK(cond)                                                                           \
    do {                                                                                      \
        if (!(cond)) {                                                                        \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                     \
        }                                                                                     \
    } while (0)

namespace airtest {

extern int g_failures;

void testModeTableFillCounts();
void testAcceptanceTest();
void testRequestLayer();
void testTransitionPrimitive();
void testApplyDispatch();
void testA5BeginFunctions();
void testEventPump();
void testFinishAndOrderSeam();
void testBuildStateMaskAndPrecondition();
void testLiveTargetAndGate();
void testGoalTestsSample();
void testChooserFallbackAndKeep();
void testDecisionStepIntegration();
void testNullRoutineGuard();
void testNonPureRoutineDiscipline();
void testActionRoutineSample();
void testThrowTrajectoryOracle();
void testPerceptionListMechanics();

HumanAiState MakeBaseline();
void MakeActionHost(HumanAiState& h, ActionRegistry& registry, ActionWorldFacts& facts, int64_t nowMs);
uint8_t RunViaGoalLoop(HumanAiState& h, int actionId, int64_t nowMs);

}  // namespace airtest
