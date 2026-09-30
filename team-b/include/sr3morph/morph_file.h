// Structural reader for .cmorph_pc morph-target (blend-shape) files
// (spec-morph-format.md).
//
// SCOPE: this walks the whole container - outer header, "Morph" block
// header, target directory, per-target descriptors, the inline quantised
// bulk runs, and the trailing sentinel - and hands back the raw 12-byte
// elements. It decodes exactly one field inside an element: the vertex
// index at +6, which spec Sec5 confirms at 25,789/25,789 records against a
// control that scored 1.3%. The three quantised components are exposed as
// raw u16s and NOT decoded: spec Sec7 leaves the dequantisation formula
// open (whether the descriptor's two float triples are (min,extent),
// (offset,scale) or (scale,center), and whether the 16-bit values are
// unsigned-biased or two's-complement, are all undetermined), and closing
// it needs the runtime code that APPLIES a morph to a mesh. Same posture
// as sr3geometry with the six arrays: extract the proven structure, leave
// the encoding alone.
//
// Two things worth knowing before editing this:
//
// 1. Alignment here is the CONDITIONAL form - pad 0 when already aligned.
//    This is explicitly NOT the mandatory-minimum-pad rule that
//    .ccmesh_pc's material/geometry boundary uses (see
//    include/sr3geometry/geometry_block.h finding 0). spec-morph-format.md
//    Sec3 calls this out directly because the loader's own arithmetic
//    shows it. Using the wrong one here breaks the exact-size replay.
//
// 2. parse() IS the structural proof. The spec's headline result (Sec4) is
//    that this walk predicts the exact byte size of 1,541/1,541 real files
//    with zero residual and finds the 0x0BADBEEF sentinel at the predicted
//    end every time. parse() re-checks both of those on every file rather
//    than trusting the walk, so a successful parse is itself that
//    verification, and a format change or a bad file fails loudly instead
//    of silently yielding wrong offsets.

#pragma once

#include <cstdint>
#include <vector>

#include "sr3morph/errors.h"
#include "vpp/byte_view.h"

namespace sr3morph {

using vpp::ByteView;

constexpr uint32_t kOuterMagic = 0x1337BEEFu;   // +0x00, CONFIRMED (loader requires; 1541/1541)
constexpr uint32_t kOuterVersion = 5;           // +0x04, CONFIRMED (loader requires exactly 5)
constexpr uint32_t kBlockMagic = 0x0BADBEEFu;   // +0x10, CONFIRMED - also the trailing sentinel (Sec3.6)
constexpr uint32_t kBlockVersion = 3;           // +0x14, CONFIRMED (loader requires exactly 3)

constexpr size_t kDirectoryOffset = 0x28;       // outer header (0x10) + block header (0x18)
constexpr size_t kDirectoryEntrySize = 0x10;
constexpr size_t kDescriptorSize = 0x28;        // mode 1 (modes 0/2 use 0x18 - not implemented, see kMode1)
constexpr size_t kElementSize = 12;             // mode 1 (mode 2 uses 8)
constexpr size_t kVertexIndexOffsetInElement = 6; // CONFIRMED (Sec5)

// The only mode any shipped file uses: bulk stored inline in this file.
// Modes 0 and 2 place the bulk in a secondary .gmorph_pc buffer - real
// loader capability, but ZERO .gmorph_pc files ship in the entire game
// (Sec6: 2,946 .cmorph_pc, 0 .gmorph_pc), so those paths are unverifiable
// against real data and are deliberately not implemented here rather than
// written blind. parse() rejects them explicitly instead of guessing.
constexpr uint32_t kMode1Inline = 1;

// One target directory entry (Sec3.3): 16 bytes on disk,
// { u32 id, u32 n, u32 runtime_ptr = 0, u32 0 }.
struct DirectoryEntry {
    // Unique per target and recurring across files that share a target.
    // spec Sec3.3 reads it as a hash of the target's name - HIGH
    // CONFIDENCE, not confirmed against any known hash function (Sec11
    // item 5) - so it is surfaced raw and given no interpreted meaning.
    uint32_t id = 0;
    // Number of descriptor records for this target. `1` in every entry
    // inspected; the loader supports more, and whether n > 1 ever occurs
    // is OPEN (Sec11 item 4). The walk handles n > 1 regardless.
    uint32_t recordCount = 0;
};

// One descriptor record (Sec3.4, mode 1: 0x28 bytes).
struct Descriptor {
    size_t targetIndex = 0;            // which directory entry this record belongs to

    uint16_t affectedVertexCount = 0;  // +0x04, CONFIRMED - drives the bulk size (N x 12)

    // +0x06, CONFIRMED - the MAXIMUM vertex index this target references,
    // not a vertex count. spec Sec5 pins the distinction down: it equals
    // max(index) in 25,789/25,789 records. Matters if you size a remap
    // table from it.
    uint16_t maxVertexIndex = 0;

    // +0x08 and +0x14: two float triples, HIGH CONFIDENCE per-axis
    // dequantisation parameters (a small triple and a larger one), but the
    // formula is OPEN (Sec7) - surfaced raw, deliberately not applied.
    float paramsA[3] = {0.0f, 0.0f, 0.0f};
    float paramsB[3] = {0.0f, 0.0f, 0.0f};

    size_t bulkOffset = 0;             // absolute offset of this record's bulk run
    size_t bulkByteLength = 0;         // affectedVertexCount * kElementSize
};

// One 12-byte bulk element (Sec3.5). Only vertexIndex has a confirmed
// meaning; everything else is named by offset on purpose.
struct Element {
    // +0/+2/+4: three quantised components. CONFIRMED bimodal (piling up
    // near 0 and near 65535 - the signature of small signed deltas stored
    // as biased or two's-complement 16-bit values); HIGH CONFIDENCE that
    // they are quantised position deltas; OPEN how to decode them.
    uint16_t component0 = 0;
    uint16_t component1 = 0;
    uint16_t component2 = 0;

    // +6: CONFIRMED - the vertex index into the base mesh (Sec5).
    uint16_t vertexIndex = 0;

    uint16_t field_8 = 0;  // +8, OPEN - not characterized
    uint16_t field_10 = 0; // +10, CONFIRMED < 4096 in 10,415,507/10,415,507; OPEN meaning
};

class MorphFile {
public:
    // Parses a whole .cmorph_pc. Throws FormatError on either magic,
    // either version, a mode outside 0..2, a mode this reader doesn't
    // implement (0 and 2 - see kMode1Inline), a walk that runs past the
    // end of the content, or - crucially - a walk whose predicted end does
    // NOT land exactly on the end of the file with the 0x0BADBEEF sentinel
    // there. That last check is spec Sec4's whole-population proof,
    // re-run per file.
    static MorphFile parse(ByteView bytes);

    uint32_t mode() const { return mode_; }
    const std::vector<DirectoryEntry>& targets() const { return targets_; }
    const std::vector<Descriptor>& descriptors() const { return descriptors_; }

    // Absolute offset of the trailing 0x0BADBEEF sentinel. Equals
    // content.size() - 4 on every real file.
    size_t trailerOffset() const { return trailerOffset_; }

    // Raw bytes of one descriptor's bulk run, sliced from `content` (the
    // same buffer passed to parse() - not owned, same non-owning-view
    // convention as vpp::Container). Throws FormatError on a bad index.
    ByteView bulkBytes(size_t descriptorIndex, ByteView content) const;

    // Reads one 12-byte element structurally. Only vertexIndex is
    // interpreted; see Element. Throws FormatError on a bad index.
    Element elementAt(size_t descriptorIndex, size_t elementIndex, ByteView content) const;

    // Total affected-vertex elements across every descriptor.
    size_t totalElementCount() const;

private:
    uint32_t mode_ = 0;
    std::vector<DirectoryEntry> targets_;
    std::vector<Descriptor> descriptors_;
    size_t trailerOffset_ = 0;
};

} // namespace sr3morph
