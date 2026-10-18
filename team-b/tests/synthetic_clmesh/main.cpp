// main.cpp - the test runner, test order unchanged.
//
// Split out of tests/synthetic_clmesh_test.cpp (Team C refactor, 2026-10-17); the code
// moved verbatim. Same executable, same test order (main.cpp).

#include "common.h"

namespace {

void run(const char* name, void (*fn)()) {
    int before = g_failures;
    fn();
    std::cout << (g_failures == before ? "[ok]   " : "[FAIL] ") << name << "\n";
}

} // namespace

int main() {
    run("testHeaderLocatedAfterMandatoryPad", testHeaderLocatedAfterMandatoryPad);
    run("testHeaderMandatoryPadWhenAlreadyAligned", testHeaderMandatoryPadWhenAlreadyAligned);
    run("testHeaderRejections", testHeaderRejections);

    run("testHeadTwoSlotArraysAreContiguous", testHeadTwoSlotArraysAreContiguous);
    run("testHeadMutationNoIndexListPayloads", testHeadMutationNoIndexListPayloads);
    run("testHeadIndexListPayloadsAreWalked", testHeadIndexListPayloadsAreWalked);
    run("testHeadMutationMeshStepRoundUp16", testHeadMutationMeshStepRoundUp16);
    run("testHeadMutationRealignBetweenSlotArrays", testHeadMutationRealignBetweenSlotArrays);
    run("testHeadEmptyWhenBothCountsZero", testHeadEmptyWhenBothCountsZero);

    run("testTailAllCountsZeroLandsExactly", testTailAllCountsZeroLandsExactly);
    run("testTailFiftySixByteArray", testTailFiftySixByteArray);
    run("testTailMutationEmptyArraysAlign", testTailMutationEmptyArraysAlign);
    run("testTailFiftyTwoAndNinetySixByteArrays", testTailFiftyTwoAndNinetySixByteArrays);
    run("testTailNestedGroupAtEightyEight", testTailNestedGroupAtEightyEight);
    run("testGroup88PositionsDecodeAsFloatTriples", testGroup88PositionsDecodeAsFloatTriples);
    run("testGroup88PositionsMutationWrongStrideThrows", testGroup88PositionsMutationWrongStrideThrows);
    run("testTailDEightAndEEightPairRealigns", testTailDEightAndEEightPairRealigns);
    run("testTailMutationNoRealignBeforeEEight", testTailMutationNoRealignBeforeEEight);
    run("testTailEEightGatedOnDEightNotItsOwnCount", testTailEEightGatedOnDEightNotItsOwnCount);
    run("testTailMutationProseTrailingGroupOrder", testTailMutationProseTrailingGroupOrder);
    run("testTailMutationTwelveByteTrailingInnerElements",
        testTailMutationTwelveByteTrailingInnerElements);
    run("testTailVariableInnerCountsAndEightAlignment",
        testTailVariableInnerCountsAndEightAlignment);

    run("testGatedPairSentinelWithFlagBitClearConsumesNothing",
        testGatedPairSentinelWithFlagBitClearConsumesNothing);
    run("testGatedPairRealValues", testGatedPairRealValues);
    run("testGatedPairMutationListBlockCostsZero", testGatedPairMutationListBlockCostsZero);
    run("testGatedPairCollisionHull", testGatedPairCollisionHull);
    run("testGatedPairMutationHullGateBitCleared", testGatedPairMutationHullGateBitCleared);
    run("testGatedPairMutationSentinelBranchConsumesBytes",
        testGatedPairMutationSentinelBranchConsumesBytes);

    run("testResolvedReferenceIsAMeshSubBlock", testResolvedReferenceIsAMeshSubBlock);
    run("testResolvedReferenceChainSurvivesNonEightAlignedSecondBlock",
        testResolvedReferenceChainSurvivesNonEightAlignedSecondBlock);
    run("testResolvedReferencesEmptyWhenCountIsZero", testResolvedReferencesEmptyWhenCountIsZero);
    run("testFinalizationArithmetic", testFinalizationArithmetic);

    run("testOpenArraysStayOpaque", testOpenArraysStayOpaque);
    run("testMiddleIsComputedNotSearched", testMiddleIsComputedNotSearched);
    run("testMiddleTrailingRecordArraysAndTheirAsymmetry",
        testMiddleTrailingRecordArraysAndTheirAsymmetry);
    run("testMiddleRecordContentDecoding", testMiddleRecordContentDecoding);
    run("testGroupFeatureIndicesMutationWrongStrideThrows",
        testGroupFeatureIndicesMutationWrongStrideThrows);
    run("testMiddleMutationNameTableNulByte", testMiddleMutationNameTableNulByte);
    run("testMiddleMutationOneRenderGroup", testMiddleMutationOneRenderGroup);
    run("testMiddleMutationNoLookupArray", testMiddleMutationNoLookupArray);
    run("testMiddleControlsAreScorableOneTermAtATime", testMiddleControlsAreScorableOneTermAtATime);
    run("testHeaderFieldAccessorRejectsOutOfRange", testHeaderFieldAccessorRejectsOutOfRange);

    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed.\n";
        return 1;
    }
    std::cout << "All synthetic clmesh-format tests passed.\n";
    return 0;
}
