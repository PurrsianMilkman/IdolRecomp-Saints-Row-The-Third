// game_peg_load_with_cb request-tracking state: PegLoadFilenameEntry,
// PegLoadRequest.
//
// Split out of engine_state.h (2026-10-08) as a zero-behavior-change
// structural move, so that a merge can only ever land a new field in its
// own domain's file - the monolithic header kept having fields from one
// struct merged into a DIFFERENT struct whose closing brace sat next to
// it. engine_state.h includes this header, so callers keep including
// engine_state.h only; add new fields to these structs HERE.
//
// In the doc comments below, "this header", "this document", "this file"
// and "this header's own top note" keep their pre-split meaning: the
// engine-state header set as a whole (engine_state.h plus the per-domain
// headers it includes), whose top note is at the head of engine_state.h.
// "above"/"below" pointers to a type or member now in another file were
// re-worded to name that file; every other line moved verbatim.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "sr3luahost/open_state.h"

namespace sr3luahost {

// One `game_peg_load_with_cb` (Sec8.24) filename slot entry.
struct PegLoadFilenameEntry {
    std::string name;
    // Real, CONFIRMED, empirically vector-validated hash bucket index
    // (spec-texture-format.md Sec8.2's "multiply-33/XOR" hash,
    // cross-confirmed by spec-lua-api-behaviour.md Sec5.3's 5/5 real I/O
    // vectors) computed with bucket_count=9000 - the exact real call shape
    // Sec8.24 cites for this function's own shared resolution chain
    // (`0x00db3530`->`0x00dab330(name,9000)`->..., reused verbatim from
    // `game_peg_unload`, Sec2.7). This project has no real PEG pointer
    // array (`0x029e1930`) to resolve the bucket into an actual loaded
    // texture, so only the bucket index itself - the one real, checkable
    // number this minimal registry can reproduce exactly - is stored.
    //
    // OPEN for some names (2026-09-30, spec-texture-format.md Sec8.2 desk
    // review): the spec states initial value 0 and 32-bit wrap, but leaves
    // signed vs unsigned modulo and the character load's signedness OPEN.
    // With 9000 buckets those change the answer when the 32-bit hash is
    // >= 2^31 or the name has a byte >= 0x80, so the index is set only
    // when every reading agrees (multiply33XorHashBucketUnambiguous) and
    // stays OPEN otherwise.
    OpenValue<uint32_t> bucketIndex{"multiply-33 bucket index (signed modulo / char signedness)",
                                    "spec-texture-format.md Sec8.2"};
};

// One `game_peg_load_with_cb` (Sec8.24) request-tracking slot.
struct PegLoadRequest {
    // Arg 1, bounds-checked to 64 bytes per Sec8.24's own confirmed text
    // ("copied (bounds-checked to 64 bytes) into a per-request
    // tracking-slot's own name field").
    std::string requestName;
    // Args 3..2+N, N clamped to the confirmed 1..6 range; reading stops
    // early at the first absent slot (CONFIRMED, Sec8.24).
    std::vector<PegLoadFilenameEntry> filenames;
    // Real, CONFIRMED-tier completion/callback-dispatch behavior (Sec8.24:
    // "once every PEG in that slot has finished loading ... queue a stored
    // command name ... then reset the slot's count to 0") is explicitly
    // NOT modeled here - this project has no real async texture-loading
    // pipeline behind this stub host, and the spec itself only reaches
    // HIGH CONFIDENCE on what the stored "+0x28" field even IS ("the
    // actual Lua-callback invocation was not traced past the queued
    // command lookup"). Stated plainly as an open gap, not guessed past.
};

} // namespace sr3luahost
