// WorldPosition: the shared plain-xyz position type (get_dist and the
// character/vehicle natives).
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

namespace sr3luahost {

// A world position (get_dist, Sec4.2; shared by several other natives, see
// CharacterState::worldPosition's own doc comment). Plain xyz, no rotation -
// nothing in scope needs orientation here.
struct WorldPosition { double x = 0, y = 0, z = 0; };

} // namespace sr3luahost
