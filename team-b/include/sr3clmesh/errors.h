#pragma once

#include <stdexcept>
#include <string>

namespace sr3clmesh {

// Thrown for a violation of a CONFIRMED hard rule (spec-physics-format.md
// Sec4, spec-geometry-format.md Sec4.2): the `0x4fe66afa` magic not being
// where the shared material block says it must be, the version field not
// reading exactly 20, or the content being too short to hold the fixed
// `0x140`-byte header.
//
// NEVER thrown for anything either spec marks HYPOTHESIS / OPEN / UNKNOWN -
// and that is most of this format. The sub-parser arrays whose *contents*
// are OPEN are surfaced as raw byte spans; the one structural step whose
// byte size nobody has established (the `+0xb8` single resolved reference,
// which in every real file swallows the whole material/render-mesh region)
// is surfaced as an explicitly-labelled opaque range, not guessed at.
class FormatError : public std::runtime_error {
public:
    explicit FormatError(const std::string& what) : std::runtime_error(what) {}
};

} // namespace sr3clmesh
