#pragma once

// sr3airuntime/navpoint_selector.h - the real, live NPC movement-target
// selection mechanism: spec-ai-runtime.md Sec15, "The real live
// movement-target mechanism: Navpoint/`cover_node`" (2026-10-08).
//
// WHY THIS EXISTS. Sec14 traced the project's navmesh/path-point query
// layer and found it dead in every shipped file - decorative, never
// exercised by any real call site. Sec15 is the direct follow-up: if the
// navmesh is dead, something else must be driving the chase/cover movement
// NPCs visibly exhibit, and Sec15 traces that "something else" end to end
// from its one real call site (`0x00b3e180`, called only from
// `0x00b42974`). It is a flat linear scan over a flat global array of
// `Navpoint` positions, filtered by a two-radius (annulus) geometric
// distance test, capped at 10 candidates, then a uniform-random pick among
// survivors - CONFIRMED NOT a graph search and NOT nearest-neighbor
// (Sec15.3's own words). This file ports exactly that CONFIRMED algorithm;
// nothing here is a guess filling a gap Sec15 itself leaves open.
//
// INPUT: THIS PROJECT'S OWN ZoneObjects READER, NOT RE-PARSED. `Navpoint`
// and `cover_node` are ordinary 0x2234-section object instances (Sec15.1:
// "each read via their real per-instance tail-readers... every property
// either class stores is a scalar... never a link, index, or handle to
// another instance"). sr3zone::ZoneObjects (spec-zone-data-format.md
// Sec11) already parses every placed object in a zone, generically, by
// class hash and named property id - this file does not reimplement any
// part of that reader. It consumes sr3zone::ObjectRecord directly (via
// ExtractNavpoints() below), using sr3zone's own position/transform and
// navpoint_flags accessors (zone_objects.h, added 2026-10-08 alongside
// this file) rather than hand-rolling a second property decoder.
//
// NO ADJACENCY GRAPH ANYWHERE (Sec15.2). Neither `Navpoint` nor
// `cover_node` carries any on-disk link/index/handle to another instance.
// "Self-registration" (Sec15.2) means type/flag-driven bucketing into up
// to 4 FLAT arrays, selected by bits of `navpoint_flags` - not graph
// construction. This file's NavpointRegistrationMask()/BuildNavpointArray()
// below are a direct, literal port of that bucketing, not an abstraction
// over something richer.
//
// SCOPE: NAVPOINT-BASED SELECTION ONLY, `cover_node`'S OWN ALGORITHM IS
// OPEN. Sec15.6 is explicit: "the real `cover_node` selection algorithm
// (who picks which cover a human uses) was not located." This file
// implements ONLY the CONFIRMED Navpoint destination-selection algorithm
// (Sec15.3). It does not guess at a cover_node equivalent - see
// SelectCoverNodeDestination's own comment below (deliberately absent,
// not deliberately stubbed - there is no CONFIRMED control flow to wrap,
// the same discipline actions.h's own file comment documents for routines
// with no confirmed gating structure at all).
//
// THE RNG SEAM - NOT A BIT-EXACT PORT OF `0x00dab660`. Sec15.3 cites
// `0x00dab660` only as EVIDENCE that the real call site performs a
// uniform-random pick among survivors ("pick one uniformly at random
// (0x00dab660)") - its own internal generator/state (LCG? xorshift? a
// shared global stream seeded at process start?) was not traced, and nothing
// in Sec15 states it. Reproducing an untraced RNG's bit-exact output would
// not be a clean-room port of a CONFIRMED algorithm, it would be a guess
// dressed up as one - exactly what this project's standing "implement only
// what the spec confirms" discipline rules out. So: the SHAPE of the pick
// (uniform over however many candidates survived the filter, cap 10) is
// CONFIRMED and is what this file implements; the specific SEQUENCE of
// picks for a given seed is this project's own, via the injectable
// `pickUniform` callable below (same seam idea as actions.h's
// ActionWorldFacts) and the small DeterministicRng this file provides for
// callers/tests that want one. Determinism IS meaningful and tested here -
// same seed, same sequence of picks, every time - just not against the
// real engine's own undisclosed stream.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "sr3airuntime/common.h"
#include "sr3save/save_crc.h" // sr3save::nameHash() - explicit include even though
                               // sr3zone/zone_objects.h already pulls it in transitively,
                               // since this header calls it directly (NavpointClassHash()/
                               // CoverNodeClassHash() below).
#include "sr3zone/zone_objects.h"

namespace sr3airuntime {

// --- class identity (Sec15.1's classIdx/vtable pair is memory-layout
// detail this project's zone reader does not reproduce; the class-name
// hash, already resolvable via sr3zone::ClassNameForHash()'s own Navpoint/
// cover_node entries, is the static-data-only identity this file uses
// instead). ---

inline uint32_t NavpointClassHash() {
    static const uint32_t h = sr3save::nameHash("Navpoint");
    return h;
}
inline uint32_t CoverNodeClassHash() {
    static const uint32_t h = sr3save::nameHash("cover_node");
    return h;
}
inline bool IsNavpointRecord(const sr3zone::ObjectRecord& rec) {
    return rec.classHash == NavpointClassHash();
}
inline bool IsCoverNodeRecord(const sr3zone::ObjectRecord& rec) {
    return rec.classHash == CoverNodeClassHash();
}

// A Navpoint, reduced to exactly what Sec15.3's algorithm reads: its world
// position and its raw navpoint_flags bitmask. `handle` is carried through
// purely for test/caller identity (which real object a picked position
// came from) - the algorithm itself never branches on it.
struct Navpoint {
    uint64_t handleLo = 0;
    uint64_t handleHi = 0;
    Vec3 pos;
    uint32_t navpointFlags = 0; // 0 if the record carried no navpoint_flags property (Sec15.1: common case)
};

// Builds a Navpoint from one real sr3zone::ObjectRecord. Does NOT check
// rec.classHash == NavpointClassHash() - this is a pure field-extraction
// step, callers filter by class via IsNavpointRecord()/ExtractNavpoints()
// below. Returns std::nullopt if `rec` carries no position/transform
// property (sr3zone::kPositionPropertyId) - the one property this
// algorithm structurally needs (a Navpoint with no decodable position
// cannot participate in Sec15.3's own distance filter). A record with no
// navpoint_flags property decodes to navpointFlags=0 (Sec15.1's documented
// common case, not an error).
inline std::optional<Navpoint> MakeNavpointFromRecord(const sr3zone::ObjectRecord& rec) {
    std::optional<sr3zone::Position3f> p = sr3zone::ObjectPosition(rec);
    if (!p) return std::nullopt;
    Navpoint np;
    np.handleLo = rec.handleLo;
    np.handleHi = rec.handleHi;
    np.pos = Vec3{p->x, p->y, p->z};
    np.navpointFlags = static_cast<uint32_t>(sr3zone::NavpointFlags(rec).value_or(0));
    return np;
}

// Convenience: filters `records` down to real Navpoint-class instances
// with a decodable position, converting each via MakeNavpointFromRecord().
// This is the intended one-call integration path from a parsed
// sr3zone::ObjectsSection (ZoneObjects::locate()/parseAt()) into this
// file's own algorithm - see this header's own "INPUT" note above.
inline std::vector<Navpoint> ExtractNavpoints(const std::vector<sr3zone::ObjectRecord>& records) {
    std::vector<Navpoint> out;
    out.reserve(records.size());
    for (const auto& rec : records) {
        if (!IsNavpointRecord(rec)) continue;
        if (auto np = MakeNavpointFromRecord(rec)) out.push_back(*np);
    }
    return out;
}

// --- Sec15.2: self-registration into up to 4 flat global arrays, selected
// by bits of navpoint_flags. Each bit is independent (a Navpoint can
// register into more than one array, or none at all - the common case,
// Sec15.1: navpoint_flags itself is present on only 9.1% of instances). ---

enum class NavpointArray : uint8_t {
    kBit1 = 0, // 0x1 -> 0x0289fc08 - the ONE array Sec15.3's traced call site scans
    kBit2 = 1, // 0x2 -> 0x0262d528, real array cap 256 (not reproduced here - see BuildNavpointArray's own comment)
    kBit4 = 2, // 0x4 -> 0x026236a0
    kBit8 = 3, // 0x8 -> 0x01503e1c, real array cap 1 (a true runtime singleton)
};

inline uint32_t NavpointArrayBit(NavpointArray arr) {
    switch (arr) {
        case NavpointArray::kBit1: return 0x1u;
        case NavpointArray::kBit2: return 0x2u;
        case NavpointArray::kBit4: return 0x4u;
        case NavpointArray::kBit8: return 0x8u;
    }
    return 0u;
}

// Which of the 4 arrays `navpointFlags` registers into - a bitset, bit i
// set iff `navpointFlags` has that array's own bit set. Bits of
// navpointFlags other than 0x1/0x2/0x4/0x8 (e.g. the raw bit 0x10 Sec15.6
// flags OPEN - "meaning" unresolved) do not map to any array here and are
// simply ignored, never guessed at.
inline uint8_t NavpointRegistrationMask(uint32_t navpointFlags) {
    uint8_t mask = 0;
    if (navpointFlags & NavpointArrayBit(NavpointArray::kBit1)) mask |= (1u << static_cast<uint8_t>(NavpointArray::kBit1));
    if (navpointFlags & NavpointArrayBit(NavpointArray::kBit2)) mask |= (1u << static_cast<uint8_t>(NavpointArray::kBit2));
    if (navpointFlags & NavpointArrayBit(NavpointArray::kBit4)) mask |= (1u << static_cast<uint8_t>(NavpointArray::kBit4));
    if (navpointFlags & NavpointArrayBit(NavpointArray::kBit8)) mask |= (1u << static_cast<uint8_t>(NavpointArray::kBit8));
    return mask;
}
inline bool NavpointRegistersInto(uint32_t navpointFlags, NavpointArray arr) {
    return (NavpointRegistrationMask(navpointFlags) & (1u << static_cast<uint8_t>(arr))) != 0;
}

using NavpointArrayView = std::vector<Navpoint>;

// Builds one flat array (Team B's equivalent of one of the real engine's 4
// global pointer arrays) by filtering `all` through NavpointRegistersInto().
// Deliberately NOT a global/static singleton in this project's own code -
// no hidden process-wide state; a caller rebuilds this per zone-load
// exactly as the real arrays are populated at real zone-load time
// (Sec15.2). Deliberately does NOT enforce the real arrays' own fixed
// caps (256 for bit 0x2, 1 for bit 0x8) - nothing in Sec15.3's CONFIRMED
// destination-selection algorithm reads either of those two arrays (it
// scans the uncapped bit-0x1 array specifically), and Sec15.2 does not
// state an overflow policy (first-N-wins vs. last-N-wins vs. something
// else) for the two that ARE capped - enforcing one here would be a guess,
// not a port. Flagged for a future unit if either capped array is ever
// found to have its own traced consumer.
inline NavpointArrayView BuildNavpointArray(const std::vector<Navpoint>& all, NavpointArray arr) {
    NavpointArrayView out;
    for (const auto& np : all) {
        if (NavpointRegistersInto(np.navpointFlags, arr)) out.push_back(np);
    }
    return out;
}

// --- Sec15.3: the real, traced destination-selection algorithm. ---

struct SelectionResult {
    bool found = false;
    Vec3 position; // only meaningful if found
    uint64_t pickedHandleLo = 0, pickedHandleHi = 0; // identity of the picked Navpoint, test/diagnostic only
    // Every candidate that survived the annulus filter, in scan order,
    // ALREADY capped at candidateCap (Sec15.3: "collecting candidates
    // (cap 10)") - exposed for tests/diagnostics, not just the one picked.
    std::vector<Navpoint> candidates;
};

// `array`: one flat Navpoint array, e.g. BuildNavpointArray(all, NavpointArray::kBit1)
//   - the one the real traced call site (0x00b42974) always scans.
// `targetPos`: the position candidates are filtered around (Sec15.3: "is
//   within an inner..outer radius pair" of the target's position).
// `innerRadius`/`outerRadius`: the annulus bounds. Sec15.3 does not state
//   whether the comparison is inclusive or exclusive at either bound; this
//   implementation treats both as INCLUSIVE (innerRadius <= distance <=
//   outerRadius) - a documented choice, not a confirmed one.
// `pickUniform`: called with the surviving candidate count N (1..cap) and
//   must return a value in [0, N) - the RNG seam, see this header's own
//   file comment for why this is injected rather than bit-replaying
//   0x00dab660.
// `candidateCap`: Sec15.3's own "cap 10", exposed as a parameter purely so
//   a test can use a smaller cap without rebuilding a 10+-element fixture
//   every time; real callers should leave this at the default.
//
// Scan order is `array`'s own element order (the caller's - this file
// does not reorder it; the real arrays are populated in zone-load/
// registration order, Sec15.2, which this port does not replay, so
// "first N in scan order" here means first N in the std::vector the
// caller handed in, exactly as BuildNavpointArray() built it).
inline SelectionResult SelectNavpointDestination(const NavpointArrayView& array, const Vec3& targetPos,
                                                  double innerRadius, double outerRadius,
                                                  const std::function<size_t(size_t)>& pickUniform,
                                                  size_t candidateCap = 10) {
    SelectionResult result;
    const double innerSq = innerRadius * innerRadius;
    const double outerSq = outerRadius * outerRadius;

    for (const auto& np : array) {
        if (result.candidates.size() >= candidateCap) break; // Sec15.3: "cap 10"
        const double dSq = DistanceSq(np.pos, targetPos);
        if (dSq >= innerSq && dSq <= outerSq) result.candidates.push_back(np);
    }

    if (result.candidates.empty()) {
        // Sec15.3: "else: fall through to a separate, untraced fallback"
        // (0x00b3ac20) - OPEN, explicitly out of this file's scope (Sec15.6
        // does not name this fallback as something this task should
        // implement; the task brief citing this section is explicit that
        // the fallback is "OPEN, not your scope"). result.found stays
        // false; the caller is responsible for knowing that means "go run
        // the untraced fallback," not "no movement."
        return result;
    }

    const size_t idx = pickUniform(result.candidates.size());
    // Defensive clamp: a caller-supplied pickUniform that misbehaves (out
    // of range) must not read out of bounds - never trust an injected seam
    // blindly, same discipline this project applies to every other
    // injected-fact seam (ActionWorldFacts, MoveInputSource).
    const size_t safeIdx = (idx < result.candidates.size()) ? idx : 0;
    const Navpoint& picked = result.candidates[safeIdx];

    result.found = true;
    result.position = picked.pos;
    result.pickedHandleLo = picked.handleLo;
    result.pickedHandleHi = picked.handleHi;
    return result;
}

// NOTE ON `cover_node`'s OWN DESTINATION-SELECTION ALGORITHM:
// deliberately NOT implemented here. Sec15.6: "the real cover_node
// selection algorithm (who picks which cover a human uses) was not
// located." There is no CONFIRMED control-flow text to port - the same
// "nothing to wrap" discipline actions.h's own file comment documents for
// Sec11.3 H's two trajectory solvers before unit A5 read them. A future
// unit that traces that algorithm's real call site should add it
// alongside this file, not guess at it as a variant of
// SelectNavpointDestination above (Sec15.2 is explicit the two classes'
// registration arrays are separate and Navpoint-specific; nothing confirms
// cover_node uses flat-array-plus-annulus at all).

// --- a small, deterministic, seedable uniform RNG for callers/tests that
// don't have their own - see this header's own file comment for why this
// does NOT attempt to reproduce 0x00dab660 bit-exactly. splitmix64 (Steele/
// Vigna) - a well-known, simple, good-enough-for-test-determinism mix, NOT
// a disassembly of anything in the real binary. ---
class DeterministicRng {
public:
    explicit DeterministicRng(uint64_t seed) : state_(seed) {}

    // Uniform in [0, exclusiveUpperBound). exclusiveUpperBound == 0 always
    // returns 0 (defensive; SelectNavpointDestination never calls this
    // path since it only invokes pickUniform when candidates is non-empty).
    size_t next(size_t exclusiveUpperBound) {
        if (exclusiveUpperBound == 0) return 0;
        uint64_t z = (state_ += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        z = z ^ (z >> 31);
        return static_cast<size_t>(z % static_cast<uint64_t>(exclusiveUpperBound));
    }

private:
    uint64_t state_;
};

// Wraps a DeterministicRng as the `pickUniform` callable SelectNavpointDestination
// wants. The returned std::function captures `rng` BY REFERENCE - `rng` must
// outlive every call through the returned callable.
inline std::function<size_t(size_t)> MakePickUniform(DeterministicRng& rng) {
    return [&rng](size_t n) { return rng.next(n); };
}

}  // namespace sr3airuntime
