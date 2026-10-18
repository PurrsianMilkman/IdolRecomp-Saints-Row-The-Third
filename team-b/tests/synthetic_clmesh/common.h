// common.h - shared harness, fixtures and test declarations.
//
// Split out of tests/synthetic_clmesh_test.cpp (Team C refactor, 2026-10-17); the code
// moved verbatim. Same executable, same test order (main.cpp).

#pragma once

#include <cmath>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include "sr3clmesh/level_mesh.h"
#include "sr3geometry/material_block.h"

extern int g_failures;


#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::cerr << "CHECK FAILED: " #cond " at " __FILE__ ":"          \
                      << __LINE__ << "\n";                                   \
            ++g_failures;                                                    \
        }                                                                    \
    } while (0)

#define CHECK_THROWS(expr)                                                   \
    do {                                                                     \
        bool threw = false;                                                  \
        try { expr; }                                                        \
        catch (const std::exception&) { threw = true; }                      \
        if (!threw) {                                                        \
            std::cerr << "CHECK FAILED (expected throw): " #expr             \
                      << " at " __FILE__ ":" << __LINE__ << "\n";            \
            ++g_failures;                                                    \
        }                                                                    \
    } while (0)


struct ClmeshOptions {
    std::vector<std::string> materialNames = {"prop_a_d.tga", "prop_a_n.tga"};
    uint32_t version = sr3clmesh::kLevelMeshVersion;

    uint32_t countRefA = 0;
    uint32_t countRefB = 0;
    int32_t pairFirst = sr3clmesh::kPairSentinel;
    int32_t pairSecond = sr3clmesh::kPairSentinel;
    uint32_t count70 = 0;
    uint32_t count88 = 0;
    std::vector<uint32_t> inner88; // one per `+0x88` outer record
    uint32_t count98 = 0;
    uint32_t countA8 = 0;
    uint32_t countD8 = 0;
    uint32_t countE8 = 0;
    uint32_t countFc = 3;                        // 3 in every shipped file
    std::vector<uint32_t> innerFc = {1, 1, 1};   // one per trailing-group entry

    // The flags byte at header `+0x08`. Bit 2 (mask 0x04) is what selects
    // 0x00749ba0's collision-hull parse when `+0x78` is the sentinel.
    uint32_t headerFlags = 0;

    // --- the HEAD's own payloads ------------------------------------------
    // Each count(+0x48) entry is an embedded Mesh sub-block; each
    // count(+0x58) entry is a 0x00e401d0 index-list block laid down AFTER
    // all of them (spec-physics-format.md Sec4.4.6's correction to Sec4.4.1).
    uint32_t headIndexCount = 1;    // elements in each array-2 index list
    // When non-empty, the HEAD's Mesh sub-blocks are built as REAL g-backed
    // blocks (so sr3mesh::MeshBlock has something to resolve) instead of
    // c-side-only ones.
    std::vector<std::vector<std::array<float, 3>>> headRealMeshes;

    // --- the MIDDLE (0x00e3e590), built for real ------------------
    // Sec4.4.6(b): a 0x50-byte record, a material-set block, the
    // materialCount x 8 lookup table, then two render-mesh groups.
    // The name table's TOTAL byte length decides whether the leading-NUL
    // rule is observable at all: the very next step is an align-to-8, so a
    // one-byte shift is invisible unless the table ends on an 8-boundary.
    // These two names are 8 bytes each with their terminators, i.e. 16 - and
    // that is exactly why the same control only scores 29% on the shipped
    // population (Sec4.4.6(e): 71% still land without the NUL).
    std::vector<std::string> midMaterialNames = {"mat_aaa", "mat_bbb"};
    std::vector<uint32_t> midRecordSizes = {24, 40}; // each record's own `+0x00`
    uint32_t midIndexCountA = 1;    // render group 1's index list
    uint32_t midIndexCountB = 2;    // render group 2's
    std::vector<uint32_t> midGroupASub;  // R[+0x20] records, each count x 0x20
    std::vector<uint32_t> midGroupBSub;  // R[+0x30] records, each count x 4
    uint32_t midTrailer34 = 0;           // R[+0x48] x 0x34

    // 0x00748c70's list block (non-sentinel `+0x78` only): its own declared
    // total byte size at `+0x08`.
    uint32_t gatedListBlockSize = 32;
    // 0x00749ba0's collision hull (sentinel `+0x78` + flags bit 2 only).
    uint32_t hullVertexCount = 3;
    uint32_t hullIndexCount = 3;
    uint32_t hullBlobLength = 20;

    // Mutations. Each one reverts a measured rule back to what a naive
    // reading of the spec would produce; the reader must then stop landing
    // exactly on end-of-file.
    bool mutateRealignBetweenHeadArrays = false;
    bool mutateEmptyArraysAlign = false;
    bool mutateNoRealignBeforeE8 = false;
    bool mutateProseTrailingGroupOrder = false;
    bool mutateTwelveByteTrailingInner = false;
    bool mutateSentinelBranchConsumesBytes = false;
    bool mutateHeadNoIndexLists = false;      // Sec4.4.1's published (incomplete) head formula
    bool mutateMeshStepRoundUp16 = false;     // 16-align between consecutive Mesh sub-blocks
    bool mutateMiddleNoNulByte = false;       // name table without its leading NUL
    bool mutateMiddleOneRenderGroup = false;  // only one render group in the file
    bool mutateMiddleNoLookupArray = false;   // no materialCount x 8 array at R[+0x08]
};

// Offsets the builder hands back so tests can assert against absolute
// positions rather than recomputing the layout a second time.
struct ClmeshLayout {
    size_t headerAt = 0;
    size_t bodyAt = 0;
    size_t refAAt = 0;
    size_t refBAt = 0;
    size_t meshBlockStart = 0;
    std::vector<size_t> atHeadMesh;
    std::vector<size_t> atHeadIndexList;
    size_t headEnd = 0;
    size_t middleAt = 0, middleRecordAt = 0, matSetAt = 0, middleEnd = 0;
    std::vector<size_t> atRenderGroup;
    size_t atGatedBlock = 0, atHull = 0;
    size_t tailAt = 0;
    size_t at70 = 0, at88Outer = 0, at98 = 0, atA8 = 0, atD8 = 0, atE8 = 0;
    size_t atFcPrimary = 0, atFcParallel = 0;
    std::vector<size_t> atFcInner;
};

void appendU16(std::vector<uint8_t>& b, uint16_t v);
void appendU32(std::vector<uint8_t>& b, uint32_t v);
void appendCString(std::vector<uint8_t>& b, const std::string& s);
void putU16At(std::vector<uint8_t>& b, size_t off, uint16_t v);
void putU32At(std::vector<uint8_t>& b, size_t off, uint32_t v);
void putF32At(std::vector<uint8_t>& b, size_t off, float v);
size_t alignUp(size_t v, size_t a);
size_t mandatoryPad16(size_t end);
vpp::ByteView view(const std::vector<uint8_t>& b);
size_t appendMeshCBlock(std::vector<uint8_t>& b, uint32_t checkValue, size_t payloadBytes);
size_t appendIndexList(std::vector<uint8_t>& b, uint32_t count);
std::vector<uint8_t> buildClmesh(const ClmeshOptions& opt, ClmeshLayout& out,
                                 std::vector<uint8_t>* gOut = nullptr);
void appendMeshSubBlock(std::vector<uint8_t>& c, std::vector<uint8_t>& g, uint32_t checkValue,
                        const std::vector<std::array<float, 3>>& positions);
void testHeaderLocatedAfterMandatoryPad();
void testHeaderMandatoryPadWhenAlreadyAligned();
void testHeaderRejections();
void testHeadTwoSlotArraysAreContiguous();
void testHeadMutationNoIndexListPayloads();
void testHeadIndexListPayloadsAreWalked();
void testHeadMutationMeshStepRoundUp16();
void testHeadMutationRealignBetweenSlotArrays();
void testHeadEmptyWhenBothCountsZero();
void testTailAllCountsZeroLandsExactly();
void testTailFiftySixByteArray();
void testTailMutationEmptyArraysAlign();
void testTailFiftyTwoAndNinetySixByteArrays();
void testTailNestedGroupAtEightyEight();
void testGroup88PositionsDecodeAsFloatTriples();
void testGroup88PositionsMutationWrongStrideThrows();
void testTailDEightAndEEightPairRealigns();
void testTailMutationNoRealignBeforeEEight();
void testTailEEightGatedOnDEightNotItsOwnCount();
void testTailMutationProseTrailingGroupOrder();
void testTailMutationTwelveByteTrailingInnerElements();
void testTailVariableInnerCountsAndEightAlignment();
void testGatedPairSentinelWithFlagBitClearConsumesNothing();
void testGatedPairRealValues();
void testGatedPairMutationListBlockCostsZero();
void testGatedPairCollisionHull();
void testGatedPairMutationHullGateBitCleared();
void testGatedPairMutationSentinelBranchConsumesBytes();
void testResolvedReferenceIsAMeshSubBlock();
void testResolvedReferenceChainSurvivesNonEightAlignedSecondBlock();
void testResolvedReferencesEmptyWhenCountIsZero();
void testFinalizationArithmetic();
void testOpenArraysStayOpaque();
void testMiddleIsComputedNotSearched();
void testMiddleTrailingRecordArraysAndTheirAsymmetry();
void testMiddleRecordContentDecoding();
void testGroupFeatureIndicesMutationWrongStrideThrows();
void testMiddleMutationNameTableNulByte();
void testMiddleMutationOneRenderGroup();
void testMiddleMutationNoLookupArray();
void testMiddleControlsAreScorableOneTermAtATime();
void testHeaderFieldAccessorRejectsOutOfRange();
