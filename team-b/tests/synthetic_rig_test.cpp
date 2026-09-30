// Synthetic tests for the `.rig_pc` skeleton reader.
//
// Buffers are built from scratch from the layout `spec-rig-format.md` §3–§6
// STATES, not from what `src/rig.cpp` happens to do. That distinction is
// load-bearing and this project has already paid for ignoring it once: the
// `.ctdg_pc` `sourceNameTruncated` bug survived a green suite because the
// synthetic fixture had inherited the reader's own misreading of the field,
// and only a whole-population sweep caught it. A fixture derived from the
// parser only ever proves the parser agrees with itself.
//
// So the builder below encodes the spec's numbers directly:
//   * 0x50-byte header, bone count at +0x24, detail index at +0x2C,
//     attachment count at +0x30;
//   * hash table at +0x50, one u32 per bone in bone order, PADDED TO 8
//     before the bone array;
//   * 0x28-byte bone records: name offset +0x00, position +0x08,
//     offset-to-parent +0x14, parent index +0x20, -1 at +0x24;
//   * 0x40-byte attachment records (§13.4, corrected 2026-09-20 - an earlier
//     version of this fixture, and of the reader, put a quaternion at +0x20):
//     name +0x00, 3x3 rotation matrix rows at +0x08/+0x14/+0x20, translation
//     vec3 at +0x2C, parent bone +0x38, int tag +0x3C (-1 in every shipped
//     record); header +0x28 = T (trailing group size), +0x2C = K, with
//     K + T == bone count (§13.2);
//   * name region last, with both arrays' offsets relative to its start.
//
// Two checks are deliberately anchored to values this file computes
// independently rather than to the library:
//   * the name hash is recomputed here from the algorithm as WRITTEN in
//     spec-vpp-container.md §2.2 (rotate left 6, XOR the character), so a
//     drift in `vpp::hash` fails the test rather than being mirrored by it;
//   * the `+0x14` field is written as `pos(parent) - pos(bone)` per spec
//     §4, from positions chosen arbitrarily here, so the reader's
//     `parentRelativeOffset()` has to invert it correctly. That guards the
//     `restRotationRaw` -> `negatedParentOffset` rename (HANDOFF §9.15),
//     which until now had nothing but a real-data sweep behind it.

#include <cmath>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "sr3rig/errors.h"
#include "sr3rig/rig.h"

namespace {

int g_failures = 0;

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
        try { expr; } catch (const sr3rig::FormatError&) { threw = true; }   \
        if (!threw) {                                                        \
            std::cerr << "CHECK FAILED (expected FormatError): " #expr       \
                      << " at " __FILE__ ":" << __LINE__ << "\n";            \
            ++g_failures;                                                    \
        }                                                                    \
    } while (0)

void putU32At(std::vector<uint8_t>& b, size_t off, uint32_t v) {
    b[off + 0] = static_cast<uint8_t>(v & 0xFF);
    b[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    b[off + 2] = static_cast<uint8_t>((v >> 16) & 0xFF);
    b[off + 3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

void putF32At(std::vector<uint8_t>& b, size_t off, float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, 4);
    putU32At(b, off, bits);
}

// The engine string hash, written here straight from spec-vpp-container.md
// §2.2's description (rotate the 32-bit hash left by 6, XOR in the
// character) with spec-rig-format.md §5's lowercase fold. Deliberately a
// second implementation - if this and `vpp::hash` ever disagree, one of
// them is wrong and the test should say so.
uint32_t specNameHash(const std::string& name) {
    uint32_t h = 0;
    for (char ch : name) {
        unsigned char c = static_cast<unsigned char>(ch);
        if (c >= 'A' && c <= 'Z') c = static_cast<unsigned char>(c - 'A' + 'a');
        h = (h << 6) | (h >> 26); // rotate left 6
        h ^= c;
    }
    return h;
}

struct SyntheticBone {
    std::string name;
    float pos[3];
    uint32_t parent; // 0xFFFFFFFF = root
};

struct SyntheticAttachment {
    std::string name;
    float rot[3][3];   // rows at +0x08 / +0x14 / +0x20
    float trans[3];    // +0x2C
    uint32_t parentBone;
    int32_t tag = -1;  // +0x3C
};

struct BuildOptions {
    bool corruptOneHash = false;     // make hashTableMatchesNames() report false
    bool nameOffsetPastEnd = false;  // a name offset that cannot resolve
    bool parentNotLessThanChild = false;
    bool parentOutOfRange = false;
    bool attachmentParentOutOfRange = false;
    uint32_t overrideBoneCount = 0;  // 0 = use the real count
    uint32_t leadingGroupCount = 0;  // header +0x2C (K)
    uint32_t trailingGroupCount = 0; // header +0x28 (T)
};

// Builds a well-formed .rig_pc exactly per spec §3: header, hash table,
// 8-byte pad, bone array, attachment array, name region.
std::vector<uint8_t> buildRig(const std::vector<SyntheticBone>& bones,
                              const std::vector<SyntheticAttachment>& attachments,
                              const BuildOptions& opt = BuildOptions{}) {
    const size_t boneCount = bones.size();
    const size_t attachCount = attachments.size();

    const size_t headerSize = 0x50;
    const size_t hashTableAt = headerSize;
    const size_t hashTableBytes = boneCount * 4;
    size_t boneArrayAt = hashTableAt + hashTableBytes;
    boneArrayAt = (boneArrayAt + 7) & ~static_cast<size_t>(7); // spec §3 pad to 8
    const size_t boneArrayBytes = boneCount * 0x28;
    const size_t attachArrayAt = boneArrayAt + boneArrayBytes;
    const size_t attachArrayBytes = attachCount * 0x40;
    const size_t nameRegionAt = attachArrayAt + attachArrayBytes;

    // Lay out the name region first so offsets are known.
    std::vector<uint8_t> nameRegion;
    std::vector<uint32_t> boneNameOffsets, attachNameOffsets;
    for (const auto& b : bones) {
        boneNameOffsets.push_back(static_cast<uint32_t>(nameRegion.size()));
        nameRegion.insert(nameRegion.end(), b.name.begin(), b.name.end());
        nameRegion.push_back(0);
    }
    for (const auto& a : attachments) {
        attachNameOffsets.push_back(static_cast<uint32_t>(nameRegion.size()));
        nameRegion.insert(nameRegion.end(), a.name.begin(), a.name.end());
        nameRegion.push_back(0);
    }

    std::vector<uint8_t> b(nameRegionAt + nameRegion.size(), 0);

    putU32At(b, 0x24, opt.overrideBoneCount != 0 ? opt.overrideBoneCount
                                                 : static_cast<uint32_t>(boneCount));
    putU32At(b, 0x28, opt.trailingGroupCount);
    putU32At(b, 0x2C, opt.leadingGroupCount);
    putU32At(b, 0x30, static_cast<uint32_t>(attachCount));
    putU32At(b, 0x38, 0); // resolves to the hash table in 585/585
    putU32At(b, 0x40, 0);
    putU32At(b, 0x48, 0);

    for (size_t i = 0; i < boneCount; ++i) {
        uint32_t h = specNameHash(bones[i].name);
        if (opt.corruptOneHash && i == 0) h ^= 0x1u;
        putU32At(b, hashTableAt + i * 4, h);
    }

    for (size_t i = 0; i < boneCount; ++i) {
        const size_t at = boneArrayAt + i * 0x28;
        uint32_t nameOff = boneNameOffsets[i];
        if (opt.nameOffsetPastEnd && i == 0) nameOff = 0xF0000000u;
        putU32At(b, at + 0x00, nameOff);
        putU32At(b, at + 0x04, 0); // runtime slot, zeroed
        for (int c = 0; c < 3; ++c) putF32At(b, at + 0x08 + static_cast<size_t>(c) * 4, bones[i].pos[c]);

        // spec §4: `+0x14` is pos(parent) - pos(bone), with the origin
        // standing in when there is no parent. Written here from the
        // POSITIONS, so the reader has to derive the same thing back.
        float parentPos[3] = {0.0f, 0.0f, 0.0f};
        if (bones[i].parent != 0xFFFFFFFFu) {
            const auto& p = bones[bones[i].parent].pos;
            parentPos[0] = p[0]; parentPos[1] = p[1]; parentPos[2] = p[2];
        }
        for (int c = 0; c < 3; ++c)
            putF32At(b, at + 0x14 + static_cast<size_t>(c) * 4,
                     parentPos[c] - bones[i].pos[static_cast<size_t>(c)]);

        uint32_t parent = bones[i].parent;
        if (opt.parentNotLessThanChild && i == 1) parent = 2;       // parent >= child
        if (opt.parentOutOfRange && i == 1) parent = 9999;
        putU32At(b, at + 0x20, parent);
        putU32At(b, at + 0x24, 0xFFFFFFFFu); // -1 in 22,274/22,274 (spec §4)
    }

    for (size_t i = 0; i < attachCount; ++i) {
        const size_t at = attachArrayAt + i * 0x40;
        putU32At(b, at + 0x00, attachNameOffsets[i]);
        putU32At(b, at + 0x04, 0);
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c)
                putF32At(b, at + 0x08 + static_cast<size_t>(r) * 12 + static_cast<size_t>(c) * 4,
                         attachments[i].rot[r][c]);
        for (int c = 0; c < 3; ++c)
            putF32At(b, at + 0x2C + static_cast<size_t>(c) * 4, attachments[i].trans[c]);
        uint32_t pb = attachments[i].parentBone;
        if (opt.attachmentParentOutOfRange && i == 0) pb = 9999;
        putU32At(b, at + 0x38, pb);
        putU32At(b, at + 0x3C, static_cast<uint32_t>(attachments[i].tag));
    }

    std::memcpy(b.data() + nameRegionAt, nameRegion.data(), nameRegion.size());
    return b;
}

// A three-bone rig. Three is deliberate: 0x50 + 3*4 = 0x5C is NOT 8-aligned,
// so this exercises spec §3's pad-to-8 rule. An even bone count would let a
// reader that forgot the padding pass anyway.
std::vector<SyntheticBone> sampleBones() {
    return {
        {"Pelvis", {0.0f, 1.0f, 0.0f}, 0xFFFFFFFFu},
        {"spine",  {0.0f, 1.4f, 0.05f}, 0},
        {"l-hand", {0.35f, 1.2f, -0.1f}, 1},
    };
}

// Attachment 0: a 90-degree rotation about X (a proper rotation, every entry
// distinct so a swapped row/column or wrong stride fails), non-zero
// translation. Attachment 1: identity, zero translation, the same-name-as-
// parent shape (68% of shipped records).
std::vector<SyntheticAttachment> sampleAttachments() {
    return {
        {"hsattach",
         {{1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, -1.0f, 0.0f}},
         {0.25f, -0.5f, 0.125f},
         1},
        {"l-hand",
         {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
         {0.0f, 0.0f, 0.0f},
         2},
    };
}

void testRoundTrip() {
    std::vector<uint8_t> buf = buildRig(sampleBones(), sampleAttachments());
    sr3rig::Rig rig = sr3rig::Rig::parse(vpp::ByteView(buf.data(), buf.size()));

    CHECK(rig.bones().size() == 3);
    CHECK(rig.attachments().size() == 2);

    CHECK(rig.bones()[0].name == "Pelvis");
    CHECK(rig.bones()[1].name == "spine");
    CHECK(rig.bones()[2].name == "l-hand");

    CHECK(rig.bones()[0].isRoot());
    CHECK(!rig.bones()[1].isRoot());
    CHECK(rig.bones()[1].parentIndex == 0);
    CHECK(rig.bones()[2].parentIndex == 1);

    CHECK(std::fabs(rig.bones()[2].restPosition[0] - 0.35f) < 1e-6f);
    CHECK(std::fabs(rig.bones()[1].restPosition[1] - 1.4f) < 1e-6f);

    CHECK(rig.attachments()[0].name == "hsattach");
    CHECK(rig.attachments()[0].parentBoneIndex == 1);
    CHECK(rig.attachments()[1].parentBoneIndex == 2);
}

// spec §13.4: rotation rows at +0x08/+0x14/+0x20 (stride 12, NOT a
// quaternion at +0x20), translation at +0x2C, tag at +0x3C. Every matrix
// entry and the translation are distinct, so a reader reading the old
// "quaternion at +0x20" would get row 2 plus the translation's x here and
// fail loudly rather than pass by symmetry.
void testAttachmentMatrixLayout() {
    std::vector<uint8_t> buf = buildRig(sampleBones(), sampleAttachments());
    sr3rig::Rig rig = sr3rig::Rig::parse(vpp::ByteView(buf.data(), buf.size()));
    const auto& a = rig.attachments()[0];
    const float expectRot[3][3] = {{1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, -1.0f, 0.0f}};
    for (size_t r = 0; r < 3; ++r)
        for (size_t c = 0; c < 3; ++c)
            CHECK(a.rotationRows[r][c] == expectRot[r][c]);
    CHECK(a.translation[0] == 0.25f);
    CHECK(a.translation[1] == -0.5f);
    CHECK(a.translation[2] == 0.125f);
    CHECK(a.tag == -1);

    // The same-name-as-parent shape: identity rotation, zero translation.
    const auto& id = rig.attachments()[1];
    CHECK(id.rotationRows[0][0] == 1.0f && id.rotationRows[1][1] == 1.0f &&
          id.rotationRows[2][2] == 1.0f);
    CHECK(id.translation[0] == 0.0f && id.translation[1] == 0.0f && id.translation[2] == 0.0f);

    // A non-default tag round-trips as a SIGNED value.
    std::vector<SyntheticAttachment> tagged = sampleAttachments();
    tagged[0].tag = 7;
    tagged[1].tag = -1;
    std::vector<uint8_t> tbuf = buildRig(sampleBones(), tagged);
    sr3rig::Rig trig = sr3rig::Rig::parse(vpp::ByteView(tbuf.data(), tbuf.size()));
    CHECK(trig.attachments()[0].tag == 7);
    CHECK(trig.attachments()[1].tag == -1);
}

// The rename guard. The file stores pos(parent) - pos(bone); the reader
// must expose that verbatim as negatedParentOffset and its inverse as
// parentRelativeOffset(). Positions here are arbitrary and chosen in this
// file, so a reader reading the wrong offset or dropping the sign fails.
void testParentOffsetIdentity() {
    std::vector<SyntheticBone> bones = sampleBones();
    std::vector<uint8_t> buf = buildRig(bones, {});
    sr3rig::Rig rig = sr3rig::Rig::parse(vpp::ByteView(buf.data(), buf.size()));

    for (size_t i = 0; i < bones.size(); ++i) {
        float parentPos[3] = {0.0f, 0.0f, 0.0f};
        if (bones[i].parent != 0xFFFFFFFFu) {
            const auto& p = bones[bones[i].parent].pos;
            parentPos[0] = p[0]; parentPos[1] = p[1]; parentPos[2] = p[2];
        }
        const auto& stored = rig.bones()[i].negatedParentOffset;
        std::array<float, 3> inverted = rig.bones()[i].parentRelativeOffset();
        for (int c = 0; c < 3; ++c) {
            float expectStored = parentPos[c] - bones[i].pos[static_cast<size_t>(c)];
            float expectInverted = bones[i].pos[static_cast<size_t>(c)] - parentPos[c];
            CHECK(std::fabs(stored[static_cast<size_t>(c)] - expectStored) < 1e-6f);
            CHECK(std::fabs(inverted[static_cast<size_t>(c)] - expectInverted) < 1e-6f);
        }
    }

    // And the property that made the field identifiable in the first place
    // (spec §4): it is fully derivable from the positions and the parent
    // link, i.e. it carries no independent information.
    for (size_t i = 0; i < rig.bones().size(); ++i) {
        const auto& bone = rig.bones()[i];
        float px = 0.0f, py = 0.0f, pz = 0.0f;
        if (!bone.isRoot()) {
            const auto& pp = rig.bones()[bone.parentIndex].restPosition;
            px = pp[0]; py = pp[1]; pz = pp[2];
        }
        CHECK(std::fabs(bone.negatedParentOffset[0] - (px - bone.restPosition[0])) < 1e-6f);
        CHECK(std::fabs(bone.negatedParentOffset[1] - (py - bone.restPosition[1])) < 1e-6f);
        CHECK(std::fabs(bone.negatedParentOffset[2] - (pz - bone.restPosition[2])) < 1e-6f);
    }
}

// spec §5: the table holds the hash of the LOWERCASED name. "Pelvis" in the
// fixture is deliberately mixed-case, so a reader hashing the stored
// spelling fails this while a reader folding case passes.
void testHashTable() {
    std::vector<uint8_t> good = buildRig(sampleBones(), sampleAttachments());
    sr3rig::Rig rig = sr3rig::Rig::parse(vpp::ByteView(good.data(), good.size()));
    CHECK(rig.hashTableMatchesNames());

    for (const auto& bone : rig.bones()) {
        std::string lowered = bone.name;
        for (auto& ch : lowered)
            if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
        CHECK(bone.nameHash == specNameHash(lowered));
    }

    // A corrupt hash is NOT a parse failure - the spec confirms the
    // property, but the reader reports it rather than refusing the file.
    BuildOptions opt;
    opt.corruptOneHash = true;
    std::vector<uint8_t> bad = buildRig(sampleBones(), sampleAttachments(), opt);
    sr3rig::Rig badRig = sr3rig::Rig::parse(vpp::ByteView(bad.data(), bad.size()));
    CHECK(!badRig.hashTableMatchesNames());
    CHECK(badRig.bones().size() == 3);
}

void testFindBone() {
    std::vector<uint8_t> buf = buildRig(sampleBones(), sampleAttachments());
    sr3rig::Rig rig = sr3rig::Rig::parse(vpp::ByteView(buf.data(), buf.size()));
    CHECK(rig.findBone("spine") == 1);
    CHECK(rig.findBone("SPINE") == 1);   // case-insensitive per spec §5
    CHECK(rig.findBone("pelvis") == 0);  // stored as "Pelvis"
    CHECK(rig.findBone("l-hand") == 2);
    CHECK(rig.findBone("no-such-bone") == -1);
}

// spec §13.1: the engine's by-name lookup is a case-insensitive STRING
// compare returning the FIRST match; it never consults the hash table. So
// (a) a corrupt hash for a bone must not hide it from findBone, and (b) two
// bones that differ only in case resolve to the earlier one. The old
// hash-based findBone fails (a).
void testFindBoneIsStringCompareNotHash() {
    BuildOptions opt;
    opt.corruptOneHash = true; // bone 0's stored hash is now wrong
    std::vector<uint8_t> buf = buildRig(sampleBones(), sampleAttachments(), opt);
    sr3rig::Rig rig = sr3rig::Rig::parse(vpp::ByteView(buf.data(), buf.size()));
    CHECK(!rig.hashTableMatchesNames());
    CHECK(rig.findBone("pelvis") == 0);
    CHECK(rig.findBone("PELVIS") == 0);

    std::vector<SyntheticBone> dup = {
        {"Root", {0.0f, 0.0f, 0.0f}, 0xFFFFFFFFu},
        {"twin", {0.0f, 1.0f, 0.0f}, 0},
        {"TWIN", {0.0f, 2.0f, 0.0f}, 0},
    };
    std::vector<uint8_t> dbuf = buildRig(dup, {});
    sr3rig::Rig drig = sr3rig::Rig::parse(vpp::ByteView(dbuf.data(), dbuf.size()));
    CHECK(drig.findBone("Twin") == 1); // first match wins
}

// An odd bone count makes the hash table end at a non-8-aligned offset. If
// the pad were dropped the bone array would be read 4 bytes early and every
// name offset would miss, so this is the padding regression guard.
void testPaddingWithOddBoneCount() {
    std::vector<SyntheticBone> three = sampleBones();
    CHECK(three.size() % 2 == 1);
    std::vector<uint8_t> buf = buildRig(three, {});
    sr3rig::Rig rig = sr3rig::Rig::parse(vpp::ByteView(buf.data(), buf.size()));
    CHECK(rig.bones().size() == 3);
    CHECK(rig.bones()[2].name == "l-hand");

    // Even count too, so both alignment cases are covered.
    std::vector<SyntheticBone> four = three;
    four.push_back({"head", {0.0f, 1.7f, 0.0f}, 1});
    std::vector<uint8_t> buf4 = buildRig(four, {});
    sr3rig::Rig rig4 = sr3rig::Rig::parse(vpp::ByteView(buf4.data(), buf4.size()));
    CHECK(rig4.bones().size() == 4);
    CHECK(rig4.bones()[3].name == "head");
}

// spec §13.2: +0x2C (K) and +0x28 (T) are a partition pair, K + T ==
// bone_count in 585/585 shipped rigs. The reader reports the counts and
// whether the partition holds; it does not refuse a file where it does not
// (nothing in the loader enforces it), and does not claim what it means.
void testGroupPartition() {
    BuildOptions opt;
    opt.leadingGroupCount = 2;
    opt.trailingGroupCount = 1; // 2 + 1 == 3 bones
    std::vector<uint8_t> buf = buildRig(sampleBones(), {}, opt);
    sr3rig::Rig rig = sr3rig::Rig::parse(vpp::ByteView(buf.data(), buf.size()));
    CHECK(rig.leadingGroupCount() == 2);
    CHECK(rig.trailingGroupCount() == 1);
    CHECK(rig.groupPartitionHolds());

    BuildOptions broken = opt;
    broken.trailingGroupCount = 2; // 2 + 2 != 3
    std::vector<uint8_t> bbuf = buildRig(sampleBones(), {}, broken);
    sr3rig::Rig brig = sr3rig::Rig::parse(vpp::ByteView(bbuf.data(), bbuf.size()));
    CHECK(brig.trailingGroupCount() == 2);
    CHECK(!brig.groupPartitionHolds());
}

void testRejections() {
    // Too small for the 0x50 header.
    std::vector<uint8_t> tiny(0x10, 0);
    CHECK_THROWS(sr3rig::Rig::parse(vpp::ByteView(tiny.data(), tiny.size())));

    // Declared bone count far beyond the file.
    BuildOptions big;
    big.overrideBoneCount = 100000;
    std::vector<uint8_t> huge = buildRig(sampleBones(), {}, big);
    CHECK_THROWS(sr3rig::Rig::parse(vpp::ByteView(huge.data(), huge.size())));

    // A name offset that cannot resolve - spec §3 has these resolving
    // 22,274/22,274, so a miss means the layout is wrong, not that a bone
    // is unnamed.
    BuildOptions badName;
    badName.nameOffsetPastEnd = true;
    std::vector<uint8_t> nameBuf = buildRig(sampleBones(), {}, badName);
    CHECK_THROWS(sr3rig::Rig::parse(vpp::ByteView(nameBuf.data(), nameBuf.size())));

    // parent >= child breaks spec §4's 22,274/22,274 ordering property.
    BuildOptions order;
    order.parentNotLessThanChild = true;
    std::vector<uint8_t> orderBuf = buildRig(sampleBones(), {}, order);
    CHECK_THROWS(sr3rig::Rig::parse(vpp::ByteView(orderBuf.data(), orderBuf.size())));

    // A parent index past the end of the bone array.
    BuildOptions range;
    range.parentOutOfRange = true;
    std::vector<uint8_t> rangeBuf = buildRig(sampleBones(), {}, range);
    CHECK_THROWS(sr3rig::Rig::parse(vpp::ByteView(rangeBuf.data(), rangeBuf.size())));

    // An attachment pointing at a bone that does not exist - spec §6 has
    // these in range 12,480/12,480.
    BuildOptions attach;
    attach.attachmentParentOutOfRange = true;
    std::vector<uint8_t> attachBuf = buildRig(sampleBones(), sampleAttachments(), attach);
    CHECK_THROWS(sr3rig::Rig::parse(vpp::ByteView(attachBuf.data(), attachBuf.size())));

    // Truncation anywhere in the middle must be refused, never read past.
    std::vector<uint8_t> whole = buildRig(sampleBones(), sampleAttachments());
    for (size_t cut : {size_t(0x51), size_t(0x60), size_t(0x80), whole.size() - 1}) {
        if (cut >= whole.size()) continue;
        std::vector<uint8_t> cutBuf(whole.begin(), whole.begin() + static_cast<long>(cut));
        bool threwOrParsedSafely = false;
        try {
            sr3rig::Rig::parse(vpp::ByteView(cutBuf.data(), cutBuf.size()));
            threwOrParsedSafely = true; // no crash, no overread
        } catch (const sr3rig::FormatError&) {
            threwOrParsedSafely = true;
        } catch (const std::out_of_range&) {
            threwOrParsedSafely = true; // ByteView's bounds check
        }
        CHECK(threwOrParsedSafely);
    }
}

// A rig with no attachments at all - 260 of 585 shipped rigs have none, and
// the attachment array offset must not be mistaken for the name region.
void testNoAttachments() {
    std::vector<uint8_t> buf = buildRig(sampleBones(), {});
    sr3rig::Rig rig = sr3rig::Rig::parse(vpp::ByteView(buf.data(), buf.size()));
    CHECK(rig.bones().size() == 3);
    CHECK(rig.attachments().empty());
    CHECK(rig.bones()[0].name == "Pelvis");
    CHECK(rig.hashTableMatchesNames());
}

// Multi-root rigs are normal, not an error: 275 of 585 have several roots
// (spec §4), typically vehicles and multi-root props.
void testMultipleRoots() {
    std::vector<SyntheticBone> bones = {
        {"root_a", {0.0f, 0.0f, 0.0f}, 0xFFFFFFFFu},
        {"root_b", {1.0f, 0.0f, 0.0f}, 0xFFFFFFFFu},
        {"child",  {1.0f, 0.5f, 0.0f}, 1},
    };
    std::vector<uint8_t> buf = buildRig(bones, {});
    sr3rig::Rig rig = sr3rig::Rig::parse(vpp::ByteView(buf.data(), buf.size()));
    CHECK(rig.bones().size() == 3);
    CHECK(rig.bones()[0].isRoot());
    CHECK(rig.bones()[1].isRoot());
    CHECK(!rig.bones()[2].isRoot());
    CHECK(rig.bones()[2].parentIndex == 1);
}

// The vehicle-rig case that refuted the "rest rotation" reading (HANDOFF
// §9.15): a bone metres from its parent makes the `+0x14` field exceed pi
// in magnitude. Under the old reading that needed a "spun beyond one turn"
// story; under the correct one it is unremarkable and must round-trip.
void testLargeOffsetBeyondPi() {
    std::vector<SyntheticBone> bones = {
        {"body",   {0.0f, 0.0f, 0.0f}, 0xFFFFFFFFu},
        {"l-wingc", {-3.3933f, 3.9743f, -2.4434f}, 0},
    };
    std::vector<uint8_t> buf = buildRig(bones, {});
    sr3rig::Rig rig = sr3rig::Rig::parse(vpp::ByteView(buf.data(), buf.size()));
    const auto& off = rig.bones()[1].negatedParentOffset;
    CHECK(std::fabs(off[0]) > 3.14159265f);
    CHECK(std::fabs(off[1]) > 3.14159265f);
    CHECK(std::fabs(off[0] - 3.3933f) < 1e-5f);
    CHECK(std::fabs(off[1] + 3.9743f) < 1e-5f);
    // Nothing about a value beyond pi is an error here.
    CHECK(rig.bones().size() == 2);
}

// Runs one test, turning an unexpected FormatError on a VALID fixture into
// a reported failure. Without this a reader that rejects a well-formed file
// lets the exception escape main and the process fast-fails with
// 0xC0000409 and no message - which is a detection, but an illegible one.
// Found while mutation-testing this very suite.
void run(const char* name, void (*fn)()) {
    try {
        fn();
    } catch (const sr3rig::FormatError& ex) {
        std::cerr << "CHECK FAILED: " << name
                  << " rejected a valid fixture: " << ex.what() << "\n";
        ++g_failures;
    } catch (const std::exception& ex) {
        std::cerr << "CHECK FAILED: " << name << " threw: " << ex.what() << "\n";
        ++g_failures;
    }
}

} // namespace

int main() {
    run("testRoundTrip", testRoundTrip);
    run("testParentOffsetIdentity", testParentOffsetIdentity);
    run("testHashTable", testHashTable);
    run("testFindBone", testFindBone);
    run("testFindBoneIsStringCompareNotHash", testFindBoneIsStringCompareNotHash);
    run("testAttachmentMatrixLayout", testAttachmentMatrixLayout);
    run("testPaddingWithOddBoneCount", testPaddingWithOddBoneCount);
    run("testGroupPartition", testGroupPartition);
    run("testRejections", testRejections);
    run("testNoAttachments", testNoAttachments);
    run("testMultipleRoots", testMultipleRoots);
    run("testLargeOffsetBeyondPi", testLargeOffsetBeyondPi);

    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed.\n";
        return 1;
    }
    std::cout << "All synthetic rig-format tests passed.\n";
    return 0;
}
