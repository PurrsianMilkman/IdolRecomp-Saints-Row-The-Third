// tests_header_head_tail.cpp
//
// Split out of tests/synthetic_clmesh_test.cpp (Team C refactor, 2026-10-17); the code
// moved verbatim. Same executable, same test order (main.cpp).

#include "common.h"

void testHeaderLocatedAfterMandatoryPad() {
    ClmeshOptions opt;
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(b));
    CHECK(m.headerOffset() == lay.headerAt);
    CHECK(m.version() == 20);
    CHECK(m.bodyStart() == lay.headerAt + 0x140);
    CHECK(m.bodyStart() == lay.bodyAt);
    // The pad really is a minimum gap, not a round-up: the material block in
    // this fixture ends unaligned, but the header still sits at
    // end/16*16 + 16 rather than at end rounded up.
    CHECK(m.headerOffset() % 16 == 0);
}

void testHeaderMandatoryPadWhenAlreadyAligned() {
    // Zero material names makes the block exactly 0x20 bytes - already
    // 16-aligned - and the header must STILL sit a full 16 further out.
    ClmeshOptions opt;
    opt.materialNames.clear();
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    CHECK(lay.headerAt == sr3geometry::kMaterialBlockHeaderSize + 16);
    sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(b));
    CHECK(m.headerOffset() == lay.headerAt);
}

void testHeaderRejections() {
    {   // wrong magic
        ClmeshOptions opt;
        ClmeshLayout lay;
        std::vector<uint8_t> b = buildClmesh(opt, lay);
        putU32At(b, lay.headerAt + 0x00, 0x424BD00Du); // `.ccmesh_pc`'s magic
        CHECK_THROWS(sr3clmesh::LevelMesh::parse(view(b)));
    }
    {   // version 19 and 21 - the requirement is EXACTLY 20, not a range
        for (uint32_t v : {19u, 21u, 0u}) {
            ClmeshOptions opt;
            opt.version = v;
            ClmeshLayout lay;
            std::vector<uint8_t> b = buildClmesh(opt, lay);
            CHECK_THROWS(sr3clmesh::LevelMesh::parse(view(b)));
        }
    }
    {   // truncated before the fixed header ends
        ClmeshOptions opt;
        ClmeshLayout lay;
        std::vector<uint8_t> b = buildClmesh(opt, lay);
        b.resize(lay.headerAt + 0x100);
        CHECK_THROWS(sr3clmesh::LevelMesh::parse(view(b)));
    }
    {   // parseAt pointed somewhere with no magic
        ClmeshOptions opt;
        ClmeshLayout lay;
        std::vector<uint8_t> b = buildClmesh(opt, lay);
        CHECK_THROWS(sr3clmesh::LevelMesh::parseAt(view(b), lay.headerAt + 16));
    }
}

void testHeadTwoSlotArraysAreContiguous() {
    ClmeshOptions opt;
    opt.countRefA = 1;
    opt.countRefB = 1;
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(b));

    CHECK(m.referenceArrayA().count == 1);
    CHECK(m.referenceArrayA().stride == 8);
    CHECK(m.referenceArrayB().count == 1);
    CHECK(m.referenceArrayA().offset == m.bodyStart());
    // The load-bearing bit: array B starts 8 bytes after array A, NOT at the
    // next 16-byte boundary.
    CHECK(m.referenceArrayB().offset == m.bodyStart() + 8);
    // The embedded Mesh sub-blocks start immediately after BOTH arrays...
    CHECK(m.meshBlockStart() == m.bodyStart() + 16);
    CHECK(m.meshBlockStart() == lay.meshBlockStart);
    CHECK(m.headMeshBlocks().size() == 1);
    // ... and array 2's index-list blocks come after those, not before.
    CHECK(m.headIndexLists().size() == 1);
    if (!m.headMeshBlocks().empty() && !m.headIndexLists().empty()) {
        CHECK(m.headIndexLists()[0].offset >= m.headMeshBlocks()[0].end);
        CHECK(m.headIndexLists()[0].offset == lay.atHeadIndexList[0]);
        CHECK(m.headIndexLists()[0].count == 1);
    }
    CHECK(m.headEnd() == lay.headEnd);
    CHECK(m.landsOnEof());
}

// Sec4.4.6's correction to Sec4.4.1: the published head formula
// (`8*count(+0x48) + 8*count(+0x58) + sum of Mesh sub-block sizes`) is
// INCOMPLETE - array 2's own 0x00e401d0 index-list payloads follow every
// Mesh sub-block and this reader shipped without them. Building the file the
// way the published formula describes must cost the exact landing.
void testHeadMutationNoIndexListPayloads() {
    ClmeshOptions opt;
    opt.countRefA = 1;
    opt.countRefB = 2;
    opt.headIndexCount = 3;
    opt.mutateHeadNoIndexLists = true;
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(b));
    CHECK(!m.landsOnEof());
}

// The same rule the other way round: with the payloads present, the walk
// lands, and each block's count comes out of its own `+0x08`.
void testHeadIndexListPayloadsAreWalked() {
    ClmeshOptions opt;
    opt.countRefA = 2;
    opt.countRefB = 2;
    opt.headIndexCount = 3;
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(b));
    CHECK(m.landsOnEof());
    CHECK(m.headMeshBlocks().size() == 2);
    CHECK(m.headIndexLists().size() == 2);
    if (m.headMeshBlocks().size() == 2) {
        // Consecutive Mesh sub-blocks abut with ZERO padding - the c-side
        // step is the declared c-length exactly.
        CHECK(m.headMeshBlocks()[1].offset == m.headMeshBlocks()[0].end);
        CHECK(m.headMeshBlocks()[0].offset == lay.atHeadMesh[0]);
        CHECK(m.headMeshBlocks()[1].offset == lay.atHeadMesh[1]);
    }
    if (m.headIndexLists().size() == 2) {
        CHECK(m.headIndexLists()[0].count == 3);
        CHECK(m.headIndexLists()[1].count == 3);
        CHECK(m.headIndexLists()[1].offset == alignUp(m.headIndexLists()[0].end, 8));
    }
}

// MUTATION of the same step: 16-align between consecutive Mesh sub-blocks,
// the natural guess that is wrong and that is SILENT on any file with only
// one block (HANDOFF Sec9.59 - it cost every reference after the first).
void testHeadMutationMeshStepRoundUp16() {
    ClmeshOptions opt;
    opt.countRefA = 2;
    opt.countRefB = 0;
    opt.mutateMeshStepRoundUp16 = true;
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(b));
    CHECK(!m.landsOnEof());
    CHECK(m.headMeshBlocks().size() < 2);
}

void testHeadMutationRealignBetweenSlotArrays() {
    // MUTATION: build the file the way a per-array 16-byte align would. The
    // reader must then report a headEnd that does NOT match where the fixture
    // actually put the end of array B - the exact failure that hid a real bug
    // in this reader until the Mesh-sub-block resolution stopped working.
    ClmeshOptions opt;
    opt.countRefA = 1;
    opt.countRefB = 1;
    opt.mutateRealignBetweenHeadArrays = true;
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(b));
    CHECK(lay.refBAt == m.bodyStart() + 16); // the fixture really did realign
    CHECK(m.referenceArrayB().offset != lay.refBAt);
    CHECK(m.headEnd() != lay.headEnd);
}

void testHeadEmptyWhenBothCountsZero() {
    ClmeshOptions opt; // both counts default to 0
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(b));
    CHECK(m.referenceArrayA().empty());
    CHECK(m.referenceArrayB().empty());
    CHECK(m.headEnd() == m.bodyStart());
}

void testTailAllCountsZeroLandsExactly() {
    ClmeshOptions opt;
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(b));
    CHECK(m.landsOnEof());
    CHECK(m.tailEnd() == b.size());
    CHECK(m.groupFcPrimary().count == 3);
    CHECK(m.groupFcPrimary().stride == 8);
    CHECK(m.groupFcParallel().count == 3);
    CHECK(m.groupFcParallel().stride == 4);
    // Primary FIRST, parallel SECOND - the spec's own output-slot order, not
    // its prose order.
    CHECK(m.groupFcPrimary().offset < m.groupFcParallel().offset);
    CHECK(m.groupFcParallel().offset == m.groupFcPrimary().offset + 3 * 8);
    CHECK(m.groupFc().size() == 3);
    for (const sr3clmesh::NestedEntry& e : m.groupFc()) {
        CHECK(e.innerCount == 1);
        CHECK(e.innerStride == 4);
        CHECK(e.innerOffset % 8 == 0);
    }
}

void testTailFiftySixByteArray() {
    ClmeshOptions opt;
    opt.count70 = 1;
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(b));
    CHECK(m.landsOnEof());
    CHECK(m.tailEnd() == b.size());
    CHECK(m.array70().count == 1);
    CHECK(m.array70().stride == 56);
    CHECK(m.array70().offset == lay.at70);
    CHECK(m.array70().offset % 16 == 0);
    // 56 is 8 mod 16, so the empty arrays that follow leave the cursor where
    // the 56-byte array ended - the "empty aligns nothing" rule, live.
    CHECK(m.groupFcPrimary().offset == lay.at70 + 56);
    // The array's bytes come back raw, uninterpreted.
    vpp::ByteView raw = m.arrayBytes(m.array70(), view(b));
    CHECK(raw.size() == 56);
    CHECK(raw.at(0) == 0x70);
}

void testTailMutationEmptyArraysAlign() {
    // MUTATION: the fixture pads every empty array up to 16, which is what
    // Sec4.1's align-before-count-read would imply. The reader must then fail
    // to land on EOF from the real tail start.
    ClmeshOptions opt;
    opt.count70 = 1;
    opt.mutateEmptyArraysAlign = true;
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    std::vector<uint32_t> words(sr3clmesh::kLevelMeshHeaderSize / 4);
    for (size_t i = 0; i < words.size(); ++i) {
        words[i] = view(b).readU32LE(lay.headerAt + i * 4);
    }
    sr3clmesh::TailWalk w = sr3clmesh::walkTail(view(b), lay.tailAt, words.data(), b.size());
    CHECK(!(w.ok && w.end == b.size()));
}

void testTailFiftyTwoAndNinetySixByteArrays() {
    ClmeshOptions opt;
    opt.count98 = 2;
    opt.countA8 = 1;
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(b));
    CHECK(m.landsOnEof());
    CHECK(m.tailEnd() == b.size());
    CHECK(m.array98().count == 2);
    CHECK(m.array98().stride == 52);
    CHECK(m.array98().offset == lay.at98);
    CHECK(m.arrayA8().count == 1);
    CHECK(m.arrayA8().stride == 96);
    CHECK(m.arrayA8().offset == lay.atA8);
    CHECK(m.arrayA8().offset % 16 == 0);
}

void testTailNestedGroupAtEightyEight() {
    ClmeshOptions opt;
    opt.count88 = 2;
    opt.inner88 = {3, 1};
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(b));
    CHECK(m.landsOnEof());
    CHECK(m.tailEnd() == b.size());
    CHECK(m.group88Outer().count == 2);
    CHECK(m.group88Outer().stride == 16);
    CHECK(m.group88Outer().offset == lay.at88Outer);
    CHECK(m.group88().size() == 2);
    if (m.group88().size() == 2) {
        CHECK(m.group88()[0].innerCount == 3);
        CHECK(m.group88()[0].innerStride == 12);
        CHECK(m.group88()[1].innerCount == 1);
        // Inner lists follow the whole outer array, in order, back to back.
        CHECK(m.group88()[0].innerOffset == lay.at88Outer + 2 * 16);
        CHECK(m.group88()[1].innerOffset == m.group88()[0].innerOffset + 3 * 12);
    }
}

// HANDOFF Sec9.60: the `+0x88` group's 12-byte inner element decodes as a
// little-endian float32 (x,y,z) triple - CONFIRMED against the whole
// shipped population, not merely a guess. This pins the load-bearing rule:
// slot order (x=bytes 0-3, y=bytes 4-7, z=bytes 8-11) and little-endian
// float decoding, exactly the shape a real closed loop like
// `fountain_a_large_01`'s 25-point circle needs to reproduce.
void testGroup88PositionsDecodeAsFloatTriples() {
    ClmeshOptions opt;
    opt.count88 = 1;
    opt.inner88 = {3};
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(b));
    CHECK(m.landsOnEof());
    CHECK(m.group88().size() == 1);
    if (m.group88().size() != 1) return;
    const sr3clmesh::NestedEntry& e = m.group88()[0];
    CHECK(e.innerCount == 3);
    // Overwrite the 0x88 filler bytes buildClmesh() wrote with a known
    // triangle-ish point set - two points sharing x (a real degenerate-edge
    // shape this project's own population actually ships, see
    // int_hwy_2wayb~l1 in HANDOFF Sec9.60), one point negative on every axis.
    putF32At(b, e.innerOffset + 0 * 12 + 0, 11.3937f);
    putF32At(b, e.innerOffset + 0 * 12 + 4, -1.70738f);
    putF32At(b, e.innerOffset + 0 * 12 + 8, -13.7225f);
    putF32At(b, e.innerOffset + 1 * 12 + 0, 11.3937f);
    putF32At(b, e.innerOffset + 1 * 12 + 4, -1.73575f);
    putF32At(b, e.innerOffset + 1 * 12 + 8, -0.650024f);
    putF32At(b, e.innerOffset + 2 * 12 + 0, -5.2727f);
    putF32At(b, e.innerOffset + 2 * 12 + 4, -0.0055f);
    putF32At(b, e.innerOffset + 2 * 12 + 8, -0.0f);

    std::vector<sr3clmesh::Vec3> pts = m.group88Positions(e, view(b));
    CHECK(pts.size() == 3);
    if (pts.size() != 3) return;
    CHECK(pts[0].x == 11.3937f && pts[0].y == -1.70738f && pts[0].z == -13.7225f);
    CHECK(pts[1].x == 11.3937f && pts[1].y == -1.73575f && pts[1].z == -0.650024f);
    CHECK(pts[2].x == -5.2727f && pts[2].y == -0.0055f);
    // Deliberately-wrong-value check: a slot-swapped read (z where y should
    // be) must NOT match - proves the test can actually catch a field-order
    // bug rather than passing regardless of it.
    CHECK(!(pts[0].y == -13.7225f));
}

// Mutation case: group88() and groupFc() share NestedEntry but have
// different inner strides (12 vs 4). Handing a groupFc()-shaped entry to
// group88Positions() must throw rather than silently misdecode - the same
// "wrong stride is a real bug, not a shrug" discipline the rest of this
// reader already applies (kStride88Inner vs kStrideFcInner never used
// interchangeably in src/level_mesh.cpp).
void testGroup88PositionsMutationWrongStrideThrows() {
    ClmeshOptions opt;
    opt.count88 = 1;
    opt.inner88 = {2};
    opt.countFc = 3;
    // group 0 has plenty of REAL bytes after it (groups 1 and 2 together
    // contribute 8 more 4-byte elements) so that a wrongly-strided 12-byte
    // read starting at group 0's inner offset lands entirely inside the
    // buffer instead of running past EOF. That matters: an earlier version
    // of this test used {1, 0, 0} here, where group 0 sits right at the
    // tail's real end - the wrong-stride read then ran past EOF and
    // ByteView's own bounds check threw first, which made the test pass
    // for the WRONG reason (an accidental out-of-range read, not the
    // explicit stride guard group88Positions() is supposed to have) -
    // caught by temporarily deleting that guard and confirming the test
    // still (wrongly) reported [ok]. This shape closes that gap: with the
    // guard removed, the call below now returns silently instead of
    // throwing, and CHECK_THROWS genuinely fails, proving the guard - not
    // an incidental bounds check - is what this test exercises.
    opt.innerFc = {1, 4, 4};
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(b));
    CHECK(m.landsOnEof());
    CHECK(m.group88().size() == 1);
    CHECK(m.groupFc().size() == 3);
    if (m.group88().empty() || m.groupFc().empty()) return;
    // The real group88 entry decodes fine...
    (void)m.group88Positions(m.group88()[0], view(b));
    // ...but a groupFc entry (4-byte inner stride) must be rejected, not
    // silently read as if it were a group88 (12-byte stride) entry.
    CHECK_THROWS(m.group88Positions(m.groupFc()[0], view(b)));
}

void testTailDEightAndEEightPairRealigns() {
    // count(+0xd8) = 3 leaves the cursor 24 bytes in, i.e. 8 mod 16, so the
    // realign before the `+0xe8` array is observable here.
    ClmeshOptions opt;
    opt.countD8 = 3;
    opt.countE8 = 2;
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(b));
    CHECK(m.landsOnEof());
    CHECK(m.tailEnd() == b.size());
    CHECK(m.arrayD8().count == 3);
    CHECK(m.arrayD8().stride == 8);
    CHECK(m.arrayE8().count == 2);
    CHECK(m.arrayE8().stride == 16);
    CHECK(m.arrayD8().offset % 16 == 0);
    CHECK(m.arrayE8().offset == alignUp(m.arrayD8().offset + 24, 16));
    CHECK(m.arrayE8().offset == m.arrayD8().offset + 32); // NOT +24
}

void testTailMutationNoRealignBeforeEEight() {
    ClmeshOptions opt;
    opt.countD8 = 3;
    opt.countE8 = 2;
    opt.mutateNoRealignBeforeE8 = true;
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    std::vector<uint32_t> words(sr3clmesh::kLevelMeshHeaderSize / 4);
    for (size_t i = 0; i < words.size(); ++i) {
        words[i] = view(b).readU32LE(lay.headerAt + i * 4);
    }
    sr3clmesh::TailWalk w = sr3clmesh::walkTail(view(b), lay.tailAt, words.data(), b.size());
    CHECK(!(w.ok && w.end == b.size()));
}

void testTailEEightGatedOnDEightNotItsOwnCount() {
    // Sec4.2: when count(+0xd8) is 0 the function skips straight to its final
    // section, so a non-zero count(+0xe8) must be ignored entirely.
    ClmeshOptions opt;
    opt.countD8 = 0;
    opt.countE8 = 4; // present in the header, absent from the file
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(b));
    CHECK(m.landsOnEof());
    CHECK(m.tailEnd() == b.size());
    CHECK(m.arrayD8().empty());
    CHECK(m.arrayE8().empty());
}

void testTailMutationProseTrailingGroupOrder() {
    ClmeshOptions opt;
    opt.innerFc = {5, 2, 7}; // unequal, so the 12-byte offset really matters
    opt.mutateProseTrailingGroupOrder = true;
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    std::vector<uint32_t> words(sr3clmesh::kLevelMeshHeaderSize / 4);
    for (size_t i = 0; i < words.size(); ++i) {
        words[i] = view(b).readU32LE(lay.headerAt + i * 4);
    }
    sr3clmesh::TailWalk w = sr3clmesh::walkTail(view(b), lay.tailAt, words.data(), b.size());
    CHECK(!(w.ok && w.end == b.size()));
}

void testTailMutationTwelveByteTrailingInnerElements() {
    ClmeshOptions opt;
    opt.innerFc = {5, 2, 7};
    opt.mutateTwelveByteTrailingInner = true;
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    std::vector<uint32_t> words(sr3clmesh::kLevelMeshHeaderSize / 4);
    for (size_t i = 0; i < words.size(); ++i) {
        words[i] = view(b).readU32LE(lay.headerAt + i * 4);
    }
    sr3clmesh::TailWalk w = sr3clmesh::walkTail(view(b), lay.tailAt, words.data(), b.size());
    CHECK(!(w.ok && w.end == b.size()));
}

void testTailVariableInnerCountsAndEightAlignment() {
    ClmeshOptions opt;
    opt.innerFc = {5, 2, 7}; // 20, 8 and 28 bytes -> two of the three need a pad
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(b));
    CHECK(m.landsOnEof());
    CHECK(m.tailEnd() == b.size());
    CHECK(m.groupFc().size() == 3);
    if (m.groupFc().size() == 3) {
        CHECK(m.groupFc()[0].innerCount == 5);
        CHECK(m.groupFc()[1].innerCount == 2);
        CHECK(m.groupFc()[2].innerCount == 7);
        CHECK(m.groupFc()[0].innerOffset == lay.atFcInner[0]);
        CHECK(m.groupFc()[1].innerOffset == lay.atFcInner[1]);
        CHECK(m.groupFc()[2].innerOffset == lay.atFcInner[2]);
        CHECK(m.groupFc()[1].innerOffset == alignUp(m.groupFc()[0].innerOffset + 20, 8));
    }
}
