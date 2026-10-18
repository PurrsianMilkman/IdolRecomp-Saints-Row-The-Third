// Private to the sr3tables_progression library: shared file-local helpers and the
// using-declarations that the split tables_progression*.cpp files all rely on.
#pragma once

#include "sr3tables_progression/tables.h"

#include <optional>
#include <string>
#include <string_view>

namespace sr3tables_progression {

using sr3xtbl::ChildText;
using sr3xtbl::EnumIndex;
using sr3xtbl::FindChild;
using sr3xtbl::GetBool;
using sr3xtbl::GetFloat;
using sr3xtbl::GetInt32;
using sr3xtbl::GetUInt32;
using sr3xtbl::HasFlag;
using sr3xtbl::NameEquals;
using sr3xtbl::NextSibling;
using sr3xtbl::ReadBoolAlways;
using sr3xtbl::ReadFloatAlways;
using sr3xtbl::ReadInt32Always;
using sr3xtbl::ReadUInt32Always;
using sr3xtbl::ReadUInt8Always;
using sr3xtbl::ReadVec3Child;
using sr3xtbl::Vec3Result;

namespace detail {
std::optional<std::string> OptText(const Node* n, std::string_view name);
bool TextEqualsTrue(const Node* n, std::string_view name);
std::optional<bool> OptYesTrueOneBool(const Node* n, std::string_view name);
} // namespace detail
using detail::OptText;
using detail::TextEqualsTrue;
using detail::OptYesTrueOneBool;

} // namespace sr3tables_progression
