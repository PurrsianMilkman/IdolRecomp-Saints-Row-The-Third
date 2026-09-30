#pragma once

#include <stdexcept>
#include <string>

namespace sr3effects {

// Thrown only for a violation of a hard rule that spec-effects-format.md
// documents as CONFIRMED and that the game's own loader enforces: the
// shared material-block magic, the `71BW` marker at the position the
// material block's own name-table length predicts (spec §5.2), and the
// version range 42-44 (spec §5.3). Never thrown for anything the spec
// marks HYPOTHESIS/OPEN, and never for an array that merely fails a
// bounds or layout check - those are reported through the query methods
// on EffectFile so a population harness can count them instead of
// losing the file.
class FormatError : public std::runtime_error {
public:
    explicit FormatError(const std::string& what) : std::runtime_error(what) {}
};

} // namespace sr3effects
