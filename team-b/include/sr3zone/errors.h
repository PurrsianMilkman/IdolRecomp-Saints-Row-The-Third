#pragma once

#include <stdexcept>
#include <string>

namespace sr3zone {

// Thrown for a violation of a CONFIRMED hard rule: the shared material
// block failing to validate (sr3geometry::FormatError propagates through
// unchanged - not re-wrapped), the `SR3Z` magic or its 27-29 accepted
// version range (spec-ctorless-types.md Sec5, disassembly-confirmed - the
// real loader itself rejects anything outside this range with its own
// error string), the zone-header record array not landing exactly on the
// file's own end (the "replay is exact, 2,971/2,971" invariant, same
// spec), or a Mesh sub-block anchor that fails sr3mesh's own walk. Never
// thrown for anything either spec marks HYPOTHESIS/OPEN/UNKNOWN - those
// fields are surfaced raw instead (see zone_header.h/zone_geometry.h).
class FormatError : public std::runtime_error {
public:
    explicit FormatError(const std::string& what) : std::runtime_error(what) {}
};

} // namespace sr3zone
