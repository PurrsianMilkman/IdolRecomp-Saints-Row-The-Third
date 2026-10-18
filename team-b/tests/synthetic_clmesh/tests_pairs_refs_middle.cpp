// tests_pairs_refs_middle.cpp
//
// Split out of tests/synthetic_clmesh_test.cpp (Team C refactor, 2026-10-17); the code
// moved verbatim. Same executable, same test order (main.cpp).

#include "common.h"

void testGatedPairSentinelWithFlagBitClearConsumesNothing() {
    // The THIRD outcome (Sec4.4.6(f)): sentinel at `+0x78` and the flags
    // byte's bit 2 clear means nothing happens at all - not even a 16-byte
    // alignment. Asserting an alignment here costs 24 of the 4,232 DLC
    // entries their exact landing, so this is measured, not assumed.
    ClmeshOptions opt;
    opt.pairFirst = -1;
    opt.pairSecond = -1;
    opt.headerFlags = 0;
    opt.count70 = 1; // 56 bytes is 8 mod 16, so an alignment WOULD be visible
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(b));
    CHECK(m.pairIsSentinel());
    CHECK(m.pairFirst() == -1);
    CHECK(m.pairSecond() == -1);
    CHECK(m.landsOnEof());
    CHECK(m.tailEnd() == b.size());
    CHECK(m.gatedPair().branch == sr3clmesh::GatedPairBranch::None);
    CHECK(m.gatedPair().length == 0);
    CHECK(m.groupFcPrimary().offset == m.array70().offset + 56);
}

void testGatedPairRealValues() {
    {   // one value present
        ClmeshOptions opt;
        opt.pairFirst = 1;
        opt.pairSecond = -1;
        opt.gatedListBlockSize = 48;
        ClmeshLayout lay;
        std::vector<uint8_t> b = buildClmesh(opt, lay);
        sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(b));
        CHECK(!m.pairIsSentinel());
        CHECK(m.pairFirst() == 1);
        CHECK(m.pairSecond() == -1);
        CHECK(m.landsOnEof());
        // Sec4.4.6(f): the cost is the block's OWN declared size at `+0x08`,
        // per file, not a constant.
        CHECK(m.gatedPair().branch == sr3clmesh::GatedPairBranch::ListBlock);
        CHECK(m.gatedPair().offset == lay.atGatedBlock);
        CHECK(m.gatedPair().blockSize == 48);
        CHECK(m.gatedPair().blockCount == 1);
    }
    {   // two values present - Sec4.2's "validity requires the returned count
        // to be under 3"
        ClmeshOptions opt;
        opt.pairFirst = 2;
        opt.pairSecond = 3;
        opt.gatedListBlockSize = 80;
        ClmeshLayout lay;
        std::vector<uint8_t> b = buildClmesh(opt, lay);
        sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(b));
        CHECK(!m.pairIsSentinel());
        CHECK(m.pairFirst() == 2);
        CHECK(m.pairSecond() == 3);
        CHECK(m.landsOnEof());
        CHECK(m.gatedPair().blockSize == 80);
        CHECK(m.gatedPair().blockCount == 2);
    }
}

// MUTATION of the same term: the reader told to cost the list block at ZERO
// bytes - exactly the `kGatedPairBytes = 0` placeholder this reader shipped
// before Sec4.4.6(f) replaced it with the block's own size field. On the
// shipped population that placeholder costs 11.1% of all landings.
void testGatedPairMutationListBlockCostsZero() {
    ClmeshOptions opt;
    opt.pairFirst = 1;
    opt.pairSecond = -1;
    opt.gatedListBlockSize = 48;
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    sr3clmesh::WalkOptions o;
    o.gatedPairZeroCost = true;
    sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(b), o);
    CHECK(!m.landsOnEof());
}

// Sec4.4.6(f)'s REFUTATION of Sec4.2's `0x00749ba0` row: the sentinel
// branch does not build a default out of two hard-coded constants, it parses
// a real file-resident collision hull - a vertex list, a u16 index list, an
// opaque blob and two vec3s - in 18,706 of the 100,384 shipped entries.
void testGatedPairCollisionHull() {
    ClmeshOptions opt;
    opt.pairFirst = -1;
    opt.pairSecond = -1;
    opt.headerFlags = sr3clmesh::kHullGateMask; // bit 2 of the flags byte
    opt.hullVertexCount = 4;
    opt.hullIndexCount = 6;
    opt.hullBlobLength = 28;
    opt.count70 = 1;
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(b));
    CHECK(m.landsOnEof());
    CHECK(m.pairIsSentinel());
    CHECK(m.gatedPair().branch == sr3clmesh::GatedPairBranch::CollisionHull);
    CHECK(m.gatedPair().offset == lay.atHull);
    const sr3clmesh::CollisionHull& h = m.gatedPair().hull;
    CHECK(h.vertexCount == 4);
    CHECK(h.indexCount == 6);
    CHECK(h.blobLength == 28);
    CHECK(h.blobOffset % 16 == 0);
    CHECK(h.vec3bOffset == h.vec3aOffset + 12);

    std::vector<sr3clmesh::Vec3> verts = m.hullVertices(view(b));
    CHECK(verts.size() == 4);
    if (verts.size() == 4) {
        CHECK(verts[0].x == 1.0f && verts[0].y == 0.0f && verts[0].z == 7.25f);
        CHECK(verts[3].x == 4.0f && verts[3].y == -7.5f);
        // Deliberately-wrong-value check: a slot swap must NOT match.
        CHECK(!(verts[3].y == 4.0f));
    }
    std::vector<uint16_t> idx = m.hullIndices(view(b));
    CHECK(idx.size() == 6);
    if (idx.size() == 6) {
        CHECK(idx[0] == 100 && idx[5] == 105);
    }
}

// The flags bit is the whole gate: the same file with bit 2 CLEAR must be
// read as consuming nothing, which (the hull bytes still being present) must
// then miss end-of-file. This is what makes kHullGateMask load-bearing rather
// than decorative - and the mask itself was settled by measurement, mask 0x04
// landing 4,232/4,232 DLC entries where mask 0x02 landed 3,436.
void testGatedPairMutationHullGateBitCleared() {
    ClmeshOptions opt;
    opt.pairFirst = -1;
    opt.pairSecond = -1;
    opt.headerFlags = sr3clmesh::kHullGateMask;
    opt.hullVertexCount = 4;
    opt.hullIndexCount = 6;
    opt.hullBlobLength = 28;
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    // Clear the bit in the built file's header, leaving the hull bytes there.
    putU32At(b, lay.headerAt + sr3clmesh::kFlagsByte, 0);
    sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(b));
    CHECK(!m.landsOnEof());
    CHECK(m.gatedPair().branch == sr3clmesh::GatedPairBranch::None);
}

void testGatedPairMutationSentinelBranchConsumesBytes() {
    // MUTATION: the branch order read backwards - the sentinel case made to
    // consume cursor bytes. The tail must then miss its landing.
    ClmeshOptions opt;
    opt.pairFirst = -1;
    opt.pairSecond = -1;
    opt.count70 = 1;
    opt.mutateSentinelBranchConsumesBytes = true;
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    std::vector<uint32_t> words(sr3clmesh::kLevelMeshHeaderSize / 4);
    for (size_t i = 0; i < words.size(); ++i) {
        words[i] = view(b).readU32LE(lay.headerAt + i * 4);
    }
    sr3clmesh::TailWalk w = sr3clmesh::walkTail(view(b), lay.tailAt, words.data(), b.size());
    CHECK(!(w.ok && w.end == b.size()));
}

void testResolvedReferenceIsAMeshSubBlock() {
    ClmeshOptions opt;
    opt.countRefA = 1;
    opt.countRefB = 1;
    // A REAL g-backed Mesh sub-block in the HEAD, so sr3mesh::MeshBlock has
    // something to resolve rather than a c-side-only stand-in.
    opt.headRealMeshes = {{{{-2.0f, -4.0f, -6.0f}}, {{2.0f, 4.0f, 6.0f}}, {{0.0f, 0.0f, 0.0f}}}};
    ClmeshLayout lay;
    std::vector<uint8_t> g;
    std::vector<uint8_t> c = buildClmesh(opt, lay, &g);

    sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(c));
    CHECK(m.landsOnEof());
    CHECK(m.headEnd() == lay.headEnd);
    CHECK(m.meshBlockStart() == lay.meshBlockStart);
    CHECK(view(c).readU32LE(m.meshBlockStart()) == sr3mesh::kMeshVersion);

    std::vector<sr3mesh::MeshBlock> meshes = m.resolveReferencedMeshes(view(c), view(g));
    CHECK(meshes.size() == 1);
    if (meshes.size() == 1) {
        CHECK(meshes[0].checkValue() == 0xC0FFEE11u);
        CHECK(meshes[0].channels().size() == 1);
        CHECK(meshes[0].bulkInGFile());

        sr3clmesh::BoundingVolume bv = sr3clmesh::computeBoundingVolume(meshes[0]);
        CHECK(bv.valid);
        CHECK(bv.vertexCount == 3);
        CHECK(std::fabs(bv.min[0] - (-2.0f)) < 1e-6f);
        CHECK(std::fabs(bv.max[2] - 6.0f) < 1e-6f);
        CHECK(std::fabs(bv.halfExtent[0] - 2.0f) < 1e-6f);
        CHECK(std::fabs(bv.halfExtent[1] - 4.0f) < 1e-6f);
        CHECK(std::fabs(bv.halfExtent[2] - 6.0f) < 1e-6f);
        CHECK(std::fabs(bv.center[0]) < 1e-6f);
        // sqrt(4 + 16 + 36) = sqrt(56)
        CHECK(std::fabs(bv.radius - std::sqrt(56.0f)) < 1e-5f);
    }
}

void testResolvedReferenceChainSurvivesNonEightAlignedSecondBlock() {
    // Real bug, found and fixed 2026-09-14 (HANDOFF Sec9.66): a single
    // bulk-in-g Mesh sub-block's own `cLength` (156 = 0x9C here) is not a
    // multiple of 8, so a SECOND back-to-back reference lands its own
    // pre-header quad ending on a `cursor % 8 == 4` position - one more
    // conditional align8 pad is needed before its header proper (flags,
    // channelCount, ...) begins, exactly the rule already established and
    // measured for `.gzn_pc` in `src/zone_geometry.cpp`. Before the fix,
    // `resolveReferencedMeshes()` used an unconditional `+0x10` and misread
    // the second block's flags/channelCount as zero, then failed either the
    // bulk-segment-length or check-value test - real, named files
    // (`hotel_complex`, `bank_floor4_destroy`) hit exactly this.
    ClmeshOptions opt;
    opt.countRefA = 2;
    opt.countRefB = 0;
    opt.headRealMeshes = {
        {{{-1.0f, -1.0f, -1.0f}}, {{1.0f, 1.0f, 1.0f}}},
        {{{-5.0f, -5.0f, -5.0f}}, {{5.0f, 5.0f, 5.0f}}, {{0.0f, 0.0f, 0.0f}}},
    };
    ClmeshLayout lay;
    std::vector<uint8_t> g;
    std::vector<uint8_t> c = buildClmesh(opt, lay, &g);
    CHECK(lay.atHeadMesh.size() == 2);
    // The whole point of this fixture: block 2 must actually land on a
    // non-8-aligned cursor, or this test cannot distinguish the fix from
    // the bug it replaces.
    CHECK(lay.atHeadMesh[1] % 8 != 0);

    sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(c));
    CHECK(m.landsOnEof());

    std::vector<sr3mesh::MeshBlock> meshes = m.resolveReferencedMeshes(view(c), view(g));
    CHECK(meshes.size() == 2);
    if (meshes.size() == 2) {
        CHECK(meshes[0].checkValue() == 0xC0FFEE11u);
        CHECK(meshes[1].checkValue() == 0xC0FFEE12u);
        CHECK(meshes[0].bulkInGFile());
        CHECK(meshes[1].bulkInGFile());
        CHECK(meshes[0].channels().size() == 1);
        CHECK(meshes[1].channels().size() == 1);

        sr3clmesh::BoundingVolume bv1 = sr3clmesh::computeBoundingVolume(meshes[1]);
        CHECK(bv1.valid);
        CHECK(bv1.vertexCount == 3);
        CHECK(std::fabs(bv1.min[0] - (-5.0f)) < 1e-6f);
        CHECK(std::fabs(bv1.max[0] - 5.0f) < 1e-6f);
    }
}

void testResolvedReferencesEmptyWhenCountIsZero() {
    ClmeshOptions opt;
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(b));
    std::vector<uint8_t> g;
    CHECK(m.resolveReferencedMeshes(view(b), view(g)).empty());
}

void testFinalizationArithmetic() {
    // 0x0074a110's three outputs, hand-computed, with no Mesh sub-block in
    // the way. Half-extent (max-min)/2, centre min+half-extent, radius the
    // length of the half-extent vector.
    sr3clmesh::BoundingVolume bv =
        sr3clmesh::boundingVolumeFromExtents({{1.0f, -3.0f, 10.0f}}, {{7.0f, 5.0f, 10.0f}});
    CHECK(bv.valid);
    CHECK(std::fabs(bv.halfExtent[0] - 3.0f) < 1e-6f);
    CHECK(std::fabs(bv.halfExtent[1] - 4.0f) < 1e-6f);
    CHECK(std::fabs(bv.halfExtent[2] - 0.0f) < 1e-6f);
    CHECK(std::fabs(bv.center[0] - 4.0f) < 1e-6f);
    CHECK(std::fabs(bv.center[1] - 1.0f) < 1e-6f);
    CHECK(std::fabs(bv.center[2] - 10.0f) < 1e-6f);
    CHECK(std::fabs(bv.radius - 5.0f) < 1e-6f); // 3-4-5
}

void testOpenArraysStayOpaque() {
    ClmeshOptions opt;
    opt.count70 = 1;
    opt.count98 = 1;
    opt.countA8 = 1;
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(b));
    CHECK(m.landsOnEof());
    // All three are exposed as byte ranges of exactly count x stride, with
    // no field decoding at all - spec-physics-format.md Sec5 lists every one
    // of them as OPEN / UNKNOWN.
    CHECK(m.arrayBytes(m.array70(), view(b)).size() == 56);
    CHECK(m.arrayBytes(m.array98(), view(b)).size() == 52);
    CHECK(m.arrayBytes(m.arrayA8(), view(b)).size() == 96);
    CHECK(m.arrayBytes(m.array98(), view(b)).at(0) == 0x98);
    CHECK(m.arrayBytes(m.arrayA8(), view(b)).at(95) == 0xA8);
}

void testMiddleIsComputedNotSearched() {
    ClmeshOptions opt;
    opt.midMaterialNames = {"road_a_d.tga", "road_a_n.tga", "road_a_s.tga"};
    opt.midRecordSizes = {24, 40, 16};
    opt.midIndexCountA = 2;
    opt.midIndexCountB = 5;
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(b));
    CHECK(m.landsOnEof());
    // The MIDDLE begins at the 16-aligned end of the HEAD, and the TAIL
    // begins exactly where the MIDDLE ends - one cursor, no gap, no search.
    CHECK(m.middleOffset() == alignUp(m.headEnd(), 16));
    CHECK(m.middleOffset() == lay.middleAt);
    CHECK(m.middleEnd() == lay.middleEnd);
    CHECK(m.tailStart() == m.middleEnd());
    CHECK(m.middleLength() == m.middleEnd() - m.middleOffset());

    const sr3clmesh::MiddleLayout& mid = m.middle();
    CHECK(mid.ok);
    CHECK(!mid.aborted);
    CHECK(mid.recordOffset == lay.middleRecordAt);
    CHECK(mid.materialSetOffset == lay.matSetAt);
    CHECK(mid.materialCount == 3);
    CHECK(mid.materialRecordOffsets.size() == 3);
    CHECK(mid.materialSlots.count == 3 && mid.materialSlots.stride == 8);
    CHECK(mid.lookupTable.count == 3 && mid.lookupTable.stride == 8);
    // Every shipped file has exactly two render-mesh groups.
    CHECK(mid.renderGroupCount == 2);
    CHECK(mid.renderGroups[0].offset == lay.atRenderGroup[0]);
    CHECK(mid.renderGroups[1].offset == lay.atRenderGroup[1]);
    CHECK(mid.renderGroups[0].indexList.count == 2);
    CHECK(mid.renderGroups[1].indexList.count == 5);
    CHECK(mid.renderGroups[0].mesh.checkValue == 0xA1A1A1A1u);
    CHECK(mid.renderGroups[1].mesh.checkValue == 0xB2B2B2B2u);
    // The u16 array then the u8 array, both indexList.count long, with no
    // alignment between them.
    CHECK(mid.renderGroups[1].u8ArrayOffset == mid.renderGroups[1].u16ArrayOffset + 5 * 2);
}

void testMiddleTrailingRecordArraysAndTheirAsymmetry() {
    // The one place the two 0x10-byte record arrays differ: group 1 holds its
    // per-record count at `+0x08`, group 2 at `+0x00`. Unequal counts and
    // unequal sub-strides (0x20 vs 4) make a swap observable.
    ClmeshOptions opt;
    opt.midGroupASub = {2, 1};
    opt.midGroupBSub = {3};
    opt.midTrailer34 = 2;
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(b));
    CHECK(m.landsOnEof());
    const sr3clmesh::MiddleLayout& mid = m.middle();
    CHECK(mid.groupARecords.count == 2 && mid.groupARecords.stride == 0x10);
    CHECK(mid.groupBRecords.count == 1 && mid.groupBRecords.stride == 0x10);
    CHECK(mid.trailer34.count == 2 && mid.trailer34.stride == 0x34);
    CHECK(mid.trailer34.offset % 16 == 0);
}

void testMiddleRecordContentDecoding() {
    // Sec4.4.6(h): materialRecords()/trailerRecords()/groupAFeatureIndices()/
    // groupBFeatureIndices() decode real content, not just locate byte
    // ranges. One material record sized exactly large enough to hold its
    // real 0x30-byte header (0x38 = 4-byte size prefix + up to 4-byte pad to
    // the next 8-boundary + 0x30 header, so the header always fits inside
    // the record's own declared size regardless of alignment phase), two
    // group-1 sub-records, three group-2 inner elements, and two trailer
    // records - each poked with a distinct, recognizable value at exactly
    // the offset Sec4.4.6(h) confirms, everything else left at the
    // fixture's usual 0xA5/0x00 filler so a reader that read the wrong
    // offset would decode filler instead and fail the exact-value check.
    ClmeshOptions opt;
    opt.midMaterialNames = {"single_mat"};
    opt.midRecordSizes = {0x38};
    opt.midGroupASub = {2};
    opt.midGroupBSub = {3};
    opt.midTrailer34 = 2;
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    sr3clmesh::LevelMesh located = sr3clmesh::LevelMesh::parse(view(b));
    CHECK(located.landsOnEof());
    const sr3clmesh::MiddleLayout& mid = located.middle();
    CHECK(mid.materialRecordOffsets.size() == 1);
    CHECK(mid.groupADetail.size() == 1 && mid.groupADetail[0].innerCount == 2);
    CHECK(mid.groupBDetail.size() == 1 && mid.groupBDetail[0].innerCount == 3);
    CHECK(mid.trailer34.count == 2);

    // Poke known values at the exact offsets the walk just located - none of
    // this changes any length/count field, so the walk's own result above
    // stays valid and re-parsing below sees the identical layout.
    const size_t matHeader = alignUp(mid.materialRecordOffsets[0] + sr3clmesh::kMatRecordSizePrefix, 8);
    CHECK(matHeader + sr3clmesh::kMatRecordHeaderSize <= mid.materialRecordOffsets[0] + 0x38);
    putU32At(b, matHeader + sr3clmesh::kMatRecordHash0, 0x11111111u);
    putU32At(b, matHeader + sr3clmesh::kMatRecordHash1, 0x22222222u);
    b[matHeader + sr3clmesh::kMatRecordFlags] = 0x77;
    putU16At(b, matHeader + sr3clmesh::kMatRecordTexCount, 9);
    b[matHeader + sr3clmesh::kMatRecordConstCount] = 5;
    b[matHeader + sr3clmesh::kMatRecordVec4Count] = 3;

    const sr3clmesh::NestedEntry& gaEntry = mid.groupADetail[0];
    putU16At(b, gaEntry.innerOffset + 0 * gaEntry.innerStride + sr3clmesh::kGroupASubFeatureIndex,
             0x1234);
    putU16At(b, gaEntry.innerOffset + 1 * gaEntry.innerStride + sr3clmesh::kGroupASubFeatureIndex,
             0x5678);

    const sr3clmesh::NestedEntry& gbEntry = mid.groupBDetail[0];
    putU16At(b, gbEntry.innerOffset + 0 * gbEntry.innerStride + sr3clmesh::kGroupBInnerFeatureIndex, 11);
    putU16At(b, gbEntry.innerOffset + 1 * gbEntry.innerStride + sr3clmesh::kGroupBInnerFeatureIndex, 22);
    putU16At(b, gbEntry.innerOffset + 2 * gbEntry.innerStride + sr3clmesh::kGroupBInnerFeatureIndex, 33);

    const size_t tr0 = mid.trailer34.offset + 0 * mid.trailer34.stride;
    const size_t tr1 = mid.trailer34.offset + 1 * mid.trailer34.stride;
    putU32At(b, tr0 + sr3clmesh::kTrailerMaterialSlotRaw, 7);
    b[tr0 + sr3clmesh::kTrailerLodByte] = 0x04; // active
    putU32At(b, tr1 + sr3clmesh::kTrailerMaterialSlotRaw, 9);
    b[tr1 + sr3clmesh::kTrailerLodByte] = 0x02; // NOT active - bit 0x04 clear

    sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(b));
    CHECK(m.landsOnEof());

    std::vector<sr3clmesh::MaterialRecord> mats = m.materialRecords(view(b));
    CHECK(mats.size() == 1);
    CHECK(mats[0].hash0 == 0x11111111u);
    CHECK(mats[0].hash1 == 0x22222222u);
    CHECK(mats[0].flags == 0x77);
    CHECK(mats[0].textureBindingCount == 9);
    CHECK(mats[0].constantNameCount == 5);
    CHECK(mats[0].vec4ConstantCount == 3);

    std::vector<uint16_t> gaIdx = m.groupAFeatureIndices(m.middle().groupADetail[0], view(b));
    CHECK(gaIdx.size() == 2 && gaIdx[0] == 0x1234 && gaIdx[1] == 0x5678);

    std::vector<uint16_t> gbIdx = m.groupBFeatureIndices(m.middle().groupBDetail[0], view(b));
    CHECK(gbIdx.size() == 3 && gbIdx[0] == 11 && gbIdx[1] == 22 && gbIdx[2] == 33);

    std::vector<sr3clmesh::TrailerRecord> trs = m.trailerRecords(view(b));
    CHECK(trs.size() == 2);
    CHECK(trs[0].materialSlotRaw == 7 && trs[0].lodActive());
    CHECK(trs[1].materialSlotRaw == 9 && !trs[1].lodActive());
}

void testGroupFeatureIndicesMutationWrongStrideThrows() {
    // MUTATION: calling groupAFeatureIndices() on a groupBDetail entry (or
    // vice versa) must throw, not silently misread a 4-byte stride as a
    // 0x20-byte one or vice versa - same discipline as
    // testGroup88PositionsMutationWrongStrideThrows.
    ClmeshOptions opt;
    opt.midGroupASub = {2};
    opt.midGroupBSub = {3};
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(b));
    CHECK(m.landsOnEof());
    CHECK_THROWS(m.groupAFeatureIndices(m.middle().groupBDetail[0], view(b)));
    CHECK_THROWS(m.groupBFeatureIndices(m.middle().groupADetail[0], view(b)));
}

void testMiddleMutationNameTableNulByte() {
    // MUTATION: the material-set name table laid down WITHOUT the single
    // leading NUL byte. On the shipped population this term is worth 29% of
    // the landings on its own (Sec4.4.6(e)).
    ClmeshOptions opt;
    opt.mutateMiddleNoNulByte = true;
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(b));
    CHECK(!m.landsOnEof());
}

void testMiddleMutationOneRenderGroup() {
    ClmeshOptions opt;
    opt.mutateMiddleOneRenderGroup = true;
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(b));
    CHECK(!m.landsOnEof());
}

void testMiddleMutationNoLookupArray() {
    // MUTATION: the materialCount x 8 array at R[+0x08] left out of the file.
    // This is 0x0074a110's lookup table, and it is a real cursor cost.
    ClmeshOptions opt;
    opt.mutateMiddleNoLookupArray = true;
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(b));
    CHECK(!m.landsOnEof());
}

void testMiddleControlsAreScorableOneTermAtATime() {
    // The same three terms driven from the READER's side rather than the
    // fixture's: a correct file, walked with one term disabled, must miss.
    // This is what tools/validation/validate_clmesh.cpp scores per term.
    ClmeshOptions opt;
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    sr3clmesh::LevelMesh good = sr3clmesh::LevelMesh::parse(view(b));
    CHECK(good.landsOnEof());
    for (int term = 0; term < 4; ++term) {
        sr3clmesh::WalkOptions o;
        if (term == 0) o.nameTableNulByte = false;
        if (term == 1) o.secondRenderGroup = false;
        if (term == 2) o.lookupTableArray = false;
        if (term == 3) o.meshStepRoundUp16 = true;
        sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(b), o);
        CHECK(!m.landsOnEof());
    }
    // ... and a MIDDLE started 4 bytes late must miss too.
    sr3clmesh::MiddleLayout shifted =
        sr3clmesh::walkMiddle(view(b), good.headEnd() + 4, b.size());
    CHECK(!(shifted.ok && shifted.end == good.middleEnd()));
}

void testHeaderFieldAccessorRejectsOutOfRange() {
    ClmeshOptions opt;
    ClmeshLayout lay;
    std::vector<uint8_t> b = buildClmesh(opt, lay);
    sr3clmesh::LevelMesh m = sr3clmesh::LevelMesh::parse(view(b));
    CHECK(m.headerField(sr3clmesh::kCountFc) == 3);
    CHECK_THROWS(m.headerField(0x140));
    CHECK_THROWS(m.headerField(0x49));
}
