// Synthetic tests for the Navpoint/cover_node movement-target-selection
// unit, spec-ai-runtime.md Sec15 (include/sr3airuntime/navpoint_selector.h).
//
// Covers: self-registration bucketing by navpoint_flags bits (Sec15.2),
// the annulus (two-radius) distance filter and its cap-10 behaviour
// (Sec15.3), determinism of this project's OWN injected RNG seam (NOT a
// bit-exact replay of the real engine's untraced 0x00dab660 - see
// navpoint_selector.h's own file comment for why that would not be a
// clean-room port of anything CONFIRMED), and the bridge from a real
// sr3zone::ObjectRecord (as sr3zone::ZoneObjects would hand a caller) into
// this file's own Navpoint struct.
//
// Fixtures for the ObjectRecord-bridge tests are built directly as C++
// struct literals (ObjectRecord/ObjectPropertyEntry are plain structs with
// public fields) rather than through raw bytes or sr3zone::ZoneObjects
// itself - sr3zone's own byte-level format is already covered by
// tests/synthetic_zone_objects_test.cpp; this file only needs to prove
// MakeNavpointFromRecord()/ExtractNavpoints() read the right fields off an
// already-parsed record, the same "don't re-test the layer below" split
// synthetic_ai_runtime_test.cpp already uses for its own injected facts.
#include <cstring>
#include <iostream>
#include <vector>

#include "sr3airuntime/navpoint_selector.h"
#include "sr3save/save_crc.h"

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

Navpoint MakeNp(uint64_t handleLo, Vec3 pos, uint32_t flags = 0) {
    Navpoint np;
    np.handleLo = handleLo;
    np.handleHi = 0;
    np.pos = pos;
    np.navpointFlags = flags;
    return np;
}

// ===================================================================
// Part A: self-registration bucketing by navpoint_flags bits (Sec15.2).
// ===================================================================

void testNavpointRegistrationMaskSingleBits() {
    CHECK(NavpointRegistrationMask(0x1u) == (1u << 0));
    CHECK(NavpointRegistrationMask(0x2u) == (1u << 1));
    CHECK(NavpointRegistrationMask(0x4u) == (1u << 2));
    CHECK(NavpointRegistrationMask(0x8u) == (1u << 3));
}

void testNavpointRegistrationMaskCombinedBits() {
    // A Navpoint can register into more than one array - the 4 bits are
    // independent, not an enum (Sec15.2).
    uint32_t flags = 0x1u | 0x8u;
    uint8_t mask = NavpointRegistrationMask(flags);
    CHECK((mask & (1u << 0)) != 0);
    CHECK((mask & (1u << 3)) != 0);
    CHECK((mask & (1u << 1)) == 0);
    CHECK((mask & (1u << 2)) == 0);
}

void testNavpointRegistrationMaskZeroFlagsRegistersIntoNone() {
    // Sec15.1: navpoint_flags itself is a RARE property (9.1% of
    // instances) - the common case is "no flags", which must register
    // into none of the 4 arrays, not "array 0" or some default.
    CHECK(NavpointRegistrationMask(0u) == 0u);
    for (auto arr : {NavpointArray::kBit1, NavpointArray::kBit2, NavpointArray::kBit4, NavpointArray::kBit8}) {
        CHECK(!NavpointRegistersInto(0u, arr));
    }
}

void testNavpointRegistrationMaskUnknownBitIgnored() {
    // Sec15.6: the raw bit 0x10's meaning is OPEN - it must not map to any
    // of the 4 known arrays (never guessed at).
    CHECK(NavpointRegistrationMask(0x10u) == 0u);
    CHECK(!NavpointRegistersInto(0x10u, NavpointArray::kBit1));
}

void testBuildNavpointArrayFiltersByBit() {
    std::vector<Navpoint> all = {
        MakeNp(1, Vec3{0, 0, 0}, 0x1u),        // bit 0x1 only
        MakeNp(2, Vec3{0, 0, 0}, 0x2u),        // bit 0x2 only
        MakeNp(3, Vec3{0, 0, 0}, 0x1u | 0x2u), // both
        MakeNp(4, Vec3{0, 0, 0}, 0u),          // neither (common case)
    };

    NavpointArrayView bit1 = BuildNavpointArray(all, NavpointArray::kBit1);
    CHECK(bit1.size() == 2);
    CHECK((bit1[0].handleLo == 1 && bit1[1].handleLo == 3));

    NavpointArrayView bit2 = BuildNavpointArray(all, NavpointArray::kBit2);
    CHECK(bit2.size() == 2);
    CHECK((bit2[0].handleLo == 2 && bit2[1].handleLo == 3));

    NavpointArrayView bit4 = BuildNavpointArray(all, NavpointArray::kBit4);
    CHECK(bit4.empty());
}

// ===================================================================
// Part B: the annulus (two-radius) distance filter and cap-10 (Sec15.3).
// ===================================================================

// A pickUniform that always returns index 0 - isolates the filter/cap
// behaviour from the RNG, for tests that only care which candidates
// survived.
size_t PickFirst(size_t) { return 0; }

void testAnnulusFilterInsideOutsideBoundary() {
    const Vec3 target{0, 0, 0};
    std::vector<Navpoint> all = {
        MakeNp(1, Vec3{5, 0, 0}),   // distance 5 - inside [10,20]? NO, too close
        MakeNp(2, Vec3{15, 0, 0}),  // distance 15 - inside [10,20] - YES
        MakeNp(3, Vec3{20, 0, 0}),  // distance 20 - exactly outer bound - this impl treats inclusive
        MakeNp(4, Vec3{10, 0, 0}),  // distance 10 - exactly inner bound - inclusive
        MakeNp(5, Vec3{25, 0, 0}),  // distance 25 - outside, too far
    };
    NavpointArrayView arr = all; // all register-eligible for this test's purposes

    SelectionResult r = SelectNavpointDestination(arr, target, /*inner=*/10.0, /*outer=*/20.0, PickFirst);
    CHECK(r.found);
    CHECK(r.candidates.size() == 3); // handles 2, 3, 4
    bool has2 = false, has3 = false, has4 = false, has1 = false, has5 = false;
    for (const auto& c : r.candidates) {
        if (c.handleLo == 1) has1 = true;
        if (c.handleLo == 2) has2 = true;
        if (c.handleLo == 3) has3 = true;
        if (c.handleLo == 4) has4 = true;
        if (c.handleLo == 5) has5 = true;
    }
    CHECK(has2 && has3 && has4);
    CHECK(!has1 && !has5);
}

void testAnnulusFilterNoSurvivorsFallsThrough() {
    const Vec3 target{0, 0, 0};
    std::vector<Navpoint> all = {
        MakeNp(1, Vec3{1, 0, 0}),   // too close
        MakeNp(2, Vec3{1000, 0, 0}), // too far
    };
    SelectionResult r = SelectNavpointDestination(all, target, 10.0, 20.0, PickFirst);
    // Sec15.3: "else: fall through to a separate, untraced fallback" - this
    // file reports that as found=false; it does NOT implement the
    // fallback itself (explicitly out of scope).
    CHECK(!r.found);
    CHECK(r.candidates.empty());
}

void testCap10StopsAtTenInScanOrder() {
    const Vec3 target{0, 0, 0};
    std::vector<Navpoint> all;
    for (int i = 0; i < 15; ++i) {
        // All 15 sit at distance 15 from target - all inside [10,20].
        all.push_back(MakeNp(static_cast<uint64_t>(i), Vec3{15, 0, 0}));
    }
    SelectionResult r = SelectNavpointDestination(all, target, 10.0, 20.0, PickFirst);
    CHECK(r.found);
    CHECK(r.candidates.size() == 10); // Sec15.3: "cap 10", not 15
    // The 10 kept are the first 10 in scan order (handles 0..9) - this
    // file's own documented interpretation of "cap 10" (see
    // SelectNavpointDestination's own comment: early-break vs.
    // continue-but-discard yield the identical surviving set either way).
    for (uint64_t i = 0; i < 10; ++i) {
        bool found = false;
        for (const auto& c : r.candidates) {
            if (c.handleLo == i) found = true;
        }
        CHECK(found);
    }
}

void testCandidateCapParameterOverride() {
    const Vec3 target{0, 0, 0};
    std::vector<Navpoint> all;
    for (int i = 0; i < 5; ++i) all.push_back(MakeNp(static_cast<uint64_t>(i), Vec3{15, 0, 0}));
    SelectionResult r = SelectNavpointDestination(all, target, 10.0, 20.0, PickFirst, /*candidateCap=*/3);
    CHECK(r.found);
    CHECK(r.candidates.size() == 3);
}

void testSelectedPositionMatchesPickedCandidate() {
    const Vec3 target{0, 0, 0};
    std::vector<Navpoint> all = {MakeNp(1, Vec3{15, 0, 0})};
    SelectionResult r = SelectNavpointDestination(all, target, 10.0, 20.0, PickFirst);
    CHECK(r.found);
    CHECK(r.position.x == 15.0 && r.position.y == 0.0 && r.position.z == 0.0);
    CHECK(r.pickedHandleLo == 1);
}

// pickUniform that always returns an out-of-range index - must not read
// out of bounds (defensive clamp, same discipline every other injected-fact
// seam in this project gets).
size_t PickOutOfRange(size_t) { return 9999; }

void testPickUniformOutOfRangeIsClampedNotUB() {
    const Vec3 target{0, 0, 0};
    std::vector<Navpoint> all = {MakeNp(1, Vec3{15, 0, 0}), MakeNp(2, Vec3{16, 0, 0})};
    SelectionResult r = SelectNavpointDestination(all, target, 10.0, 20.0, PickOutOfRange);
    CHECK(r.found); // must not crash; clamps to index 0
    CHECK(r.pickedHandleLo == 1);
}

// ===================================================================
// Part C: determinism of this project's OWN RNG seam (NOT a bit-exact
// replay of 0x00dab660 - see navpoint_selector.h's own file comment for
// why that would not be a clean-room port of anything CONFIRMED: Sec15.3
// cites 0x00dab660 only as evidence a uniform-random pick happens at that
// call site, not as a traced algorithm with a stated internal state/
// sequence to reproduce).
// ===================================================================

void testDeterministicRngSameSeedSamePicks() {
    const Vec3 target{0, 0, 0};
    std::vector<Navpoint> all;
    for (int i = 0; i < 10; ++i) all.push_back(MakeNp(static_cast<uint64_t>(i), Vec3{15, 0, 0}));

    DeterministicRng rngA(0xC0FFEEu);
    DeterministicRng rngB(0xC0FFEEu); // same seed, independent instance

    std::vector<uint64_t> picksA, picksB;
    for (int round = 0; round < 20; ++round) {
        SelectionResult rA = SelectNavpointDestination(all, target, 10.0, 20.0, MakePickUniform(rngA));
        SelectionResult rB = SelectNavpointDestination(all, target, 10.0, 20.0, MakePickUniform(rngB));
        CHECK(rA.found && rB.found);
        picksA.push_back(rA.pickedHandleLo);
        picksB.push_back(rB.pickedHandleLo);
    }
    // Same seed, same sequence of picks, every time - this IS the
    // determinism this file guarantees (its own RNG, not the engine's).
    CHECK(picksA == picksB);

    // Sanity: the picks are not degenerately all the same value (i.e. this
    // is actually exercising a distribution across candidates, not a
    // constant mistaken for "deterministic").
    bool sawDifferentValue = false;
    for (size_t i = 1; i < picksA.size(); ++i) {
        if (picksA[i] != picksA[0]) sawDifferentValue = true;
    }
    CHECK(sawDifferentValue);

    // Every pick must be a real candidate handle (0..9), never out of range.
    for (uint64_t v : picksA) CHECK(v <= 9);
}

void testDeterministicRngDifferentSeedsCanDiffer() {
    // Not a hard guarantee for an arbitrary PRNG (two seeds COULD coincide
    // on a short run), but splitmix64 with these two seeds over this many
    // draws diverging is what this test actually observes - documenting
    // that this file's RNG is not a constant function of its input count
    // alone, independent of seed.
    DeterministicRng rngA(1);
    DeterministicRng rngB(2);
    bool sawDifference = false;
    for (int i = 0; i < 20; ++i) {
        if (rngA.next(10) != rngB.next(10)) sawDifference = true;
    }
    CHECK(sawDifference);
}

void testDeterministicRngNeverOutOfRange() {
    DeterministicRng rng(42);
    for (int i = 0; i < 1000; ++i) {
        size_t n = static_cast<size_t>(1 + (i % 7));
        size_t v = rng.next(n);
        CHECK(v < n);
    }
}

// ===================================================================
// Part D: the sr3zone::ObjectRecord bridge (MakeNavpointFromRecord,
// ExtractNavpoints, IsNavpointRecord/IsCoverNodeRecord).
// ===================================================================

sr3zone::ObjectPropertyEntry PositionEntry(float x, float y, float z) {
    sr3zone::ObjectPropertyEntry e;
    e.kind = 2;
    e.id = sr3zone::kPositionPropertyId;
    e.data.resize(12);
    std::memcpy(e.data.data() + 0, &x, 4);
    std::memcpy(e.data.data() + 4, &y, 4);
    std::memcpy(e.data.data() + 8, &z, 4);
    e.size = static_cast<uint16_t>(e.data.size());
    return e;
}

sr3zone::ObjectPropertyEntry FlagsEntry(uint8_t flags) {
    sr3zone::ObjectPropertyEntry e;
    e.kind = 1;
    e.id = sr3zone::NavpointFlagsPropertyId();
    e.data = {flags};
    e.size = 1;
    return e;
}

void testMakeNavpointFromRecordDecodesPositionAndFlags() {
    sr3zone::ObjectRecord rec;
    rec.handleLo = 77;
    rec.handleHi = 0;
    rec.classHash = NavpointClassHash();
    rec.tailEntries.push_back(PositionEntry(1.0f, 2.0f, 3.0f));
    rec.tailEntries.push_back(FlagsEntry(0x05));

    auto np = MakeNavpointFromRecord(rec);
    CHECK(np.has_value());
    if (np) {
        CHECK(np->handleLo == 77);
        CHECK(np->pos.x == 1.0 && np->pos.y == 2.0 && np->pos.z == 3.0);
        CHECK(np->navpointFlags == 0x05u);
    }
}

void testMakeNavpointFromRecordDefaultsFlagsWhenAbsent() {
    sr3zone::ObjectRecord rec;
    rec.classHash = NavpointClassHash();
    rec.tailEntries.push_back(PositionEntry(5.0f, 6.0f, 7.0f));
    // No navpoint_flags entry - Sec15.1's documented common case (90.9%).

    auto np = MakeNavpointFromRecord(rec);
    CHECK(np.has_value());
    if (np) CHECK(np->navpointFlags == 0u);
}

void testMakeNavpointFromRecordRejectsMissingPosition() {
    sr3zone::ObjectRecord rec;
    rec.classHash = NavpointClassHash();
    rec.tailEntries.push_back(FlagsEntry(0x01)); // flags present, position absent

    CHECK(!MakeNavpointFromRecord(rec).has_value());
}

void testIsNavpointAndIsCoverNodeRecordClassHashes() {
    sr3zone::ObjectRecord navRec;
    navRec.classHash = NavpointClassHash();
    CHECK(IsNavpointRecord(navRec));
    CHECK(!IsCoverNodeRecord(navRec));

    sr3zone::ObjectRecord coverRec;
    coverRec.classHash = CoverNodeClassHash();
    CHECK(IsCoverNodeRecord(coverRec));
    CHECK(!IsNavpointRecord(coverRec));

    // Cross-check against sr3zone's own population-confirmed class table
    // (spec-zone-data-format.md Sec11.7) - Navpoint/cover_node's hashes
    // must agree between the two headers, not be two independent literals
    // that happen to look similar.
    CHECK(NavpointClassHash() == sr3save::nameHash("Navpoint"));
    CHECK(CoverNodeClassHash() == sr3save::nameHash("cover_node"));
}

void testExtractNavpointsFiltersOutCoverNodeAndOtherClasses() {
    std::vector<sr3zone::ObjectRecord> records;

    sr3zone::ObjectRecord navRec1;
    navRec1.handleLo = 1;
    navRec1.classHash = NavpointClassHash();
    navRec1.tailEntries.push_back(PositionEntry(1, 1, 1));
    records.push_back(navRec1);

    sr3zone::ObjectRecord coverRec;
    coverRec.handleLo = 2;
    coverRec.classHash = CoverNodeClassHash();
    coverRec.tailEntries.push_back(PositionEntry(2, 2, 2)); // cover_node has cover_pos too - irrelevant here
    records.push_back(coverRec);

    sr3zone::ObjectRecord otherRec;
    otherRec.handleLo = 3;
    otherRec.classHash = sr3save::nameHash("general_mover");
    otherRec.tailEntries.push_back(PositionEntry(3, 3, 3));
    records.push_back(otherRec);

    sr3zone::ObjectRecord navRecNoPos; // Navpoint-class but no decodable position
    navRecNoPos.handleLo = 4;
    navRecNoPos.classHash = NavpointClassHash();
    records.push_back(navRecNoPos);

    sr3zone::ObjectRecord navRec2;
    navRec2.handleLo = 5;
    navRec2.classHash = NavpointClassHash();
    navRec2.tailEntries.push_back(PositionEntry(5, 5, 5));
    navRec2.tailEntries.push_back(FlagsEntry(0x1));
    records.push_back(navRec2);

    std::vector<Navpoint> navs = ExtractNavpoints(records);
    // Only navRec1 and navRec2 survive: cover_node and the unrelated class
    // are excluded by class hash (Sec15.2: these arrays are
    // Navpoint-specific), and navRecNoPos is excluded for lacking a
    // decodable position.
    CHECK(navs.size() == 2);
    bool has1 = false, has5 = false;
    for (const auto& np : navs) {
        if (np.handleLo == 1) has1 = true;
        if (np.handleLo == 5) { has5 = true; CHECK(np.navpointFlags == 0x1u); }
    }
    CHECK(has1 && has5);
}

// ===================================================================
// Part E: end-to-end - real-shaped population sanity (Sec15.4: 5,982
// Navpoint objects, 1-206 per zone, mean 18.5). Not a real-data test (no
// shipped .czn_pc file is read here - that is real_zone_objects_test's own
// job), just a sanity check that this file's algorithm behaves reasonably
// at a population size in Sec15.4's own stated range.
// ===================================================================

void testEndToEndWithZoneSizedPopulation() {
    std::vector<sr3zone::ObjectRecord> records;
    // 18 Navpoints (Sec15.4's own stated mean per zone), spread so some
    // fall inside a [50,150] annulus around the origin and some don't.
    for (int i = 0; i < 18; ++i) {
        sr3zone::ObjectRecord rec;
        rec.handleLo = static_cast<uint64_t>(i);
        rec.classHash = NavpointClassHash();
        float d = static_cast<float>(10 + i * 15); // 10, 25, 40, ..., 265
        rec.tailEntries.push_back(PositionEntry(d, 0, 0));
        rec.tailEntries.push_back(FlagsEntry(0x1)); // all register into the bit-0x1 array
        records.push_back(rec);
    }
    std::vector<Navpoint> all = ExtractNavpoints(records);
    CHECK(all.size() == 18);

    NavpointArrayView bit1Array = BuildNavpointArray(all, NavpointArray::kBit1);
    CHECK(bit1Array.size() == 18);

    DeterministicRng rng(7);
    SelectionResult r =
        SelectNavpointDestination(bit1Array, Vec3{0, 0, 0}, 50.0, 150.0, MakePickUniform(rng));
    CHECK(r.found);
    // Distances 55,70,85,100,115,130,145 (i=3..9) are inside [50,150] -
    // exactly 7 candidates, well under the cap.
    CHECK(r.candidates.size() == 7);
}

void run(const char* name, void (*fn)()) {
    try {
        fn();
    } catch (const std::exception& ex) {
        std::cerr << "CHECK FAILED: " << name << " threw: " << ex.what() << "\n";
        ++g_failures;
    }
}

}  // namespace

int main() {
    run("testNavpointRegistrationMaskSingleBits", testNavpointRegistrationMaskSingleBits);
    run("testNavpointRegistrationMaskCombinedBits", testNavpointRegistrationMaskCombinedBits);
    run("testNavpointRegistrationMaskZeroFlagsRegistersIntoNone",
        testNavpointRegistrationMaskZeroFlagsRegistersIntoNone);
    run("testNavpointRegistrationMaskUnknownBitIgnored", testNavpointRegistrationMaskUnknownBitIgnored);
    run("testBuildNavpointArrayFiltersByBit", testBuildNavpointArrayFiltersByBit);

    run("testAnnulusFilterInsideOutsideBoundary", testAnnulusFilterInsideOutsideBoundary);
    run("testAnnulusFilterNoSurvivorsFallsThrough", testAnnulusFilterNoSurvivorsFallsThrough);
    run("testCap10StopsAtTenInScanOrder", testCap10StopsAtTenInScanOrder);
    run("testCandidateCapParameterOverride", testCandidateCapParameterOverride);
    run("testSelectedPositionMatchesPickedCandidate", testSelectedPositionMatchesPickedCandidate);
    run("testPickUniformOutOfRangeIsClampedNotUB", testPickUniformOutOfRangeIsClampedNotUB);

    run("testDeterministicRngSameSeedSamePicks", testDeterministicRngSameSeedSamePicks);
    run("testDeterministicRngDifferentSeedsCanDiffer", testDeterministicRngDifferentSeedsCanDiffer);
    run("testDeterministicRngNeverOutOfRange", testDeterministicRngNeverOutOfRange);

    run("testMakeNavpointFromRecordDecodesPositionAndFlags",
        testMakeNavpointFromRecordDecodesPositionAndFlags);
    run("testMakeNavpointFromRecordDefaultsFlagsWhenAbsent",
        testMakeNavpointFromRecordDefaultsFlagsWhenAbsent);
    run("testMakeNavpointFromRecordRejectsMissingPosition",
        testMakeNavpointFromRecordRejectsMissingPosition);
    run("testIsNavpointAndIsCoverNodeRecordClassHashes", testIsNavpointAndIsCoverNodeRecordClassHashes);
    run("testExtractNavpointsFiltersOutCoverNodeAndOtherClasses",
        testExtractNavpointsFiltersOutCoverNodeAndOtherClasses);

    run("testEndToEndWithZoneSizedPopulation", testEndToEndWithZoneSizedPopulation);

    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed.\n";
        return 1;
    }
    std::cout << "All synthetic navpoint-selector tests passed.\n";
    return 0;
}
