// Vint element types and their property-descriptor tables (2026-10-03).
//
// The engine registers 13 UI element types, each with a table of named
// properties (spec-lua-bindings.md Sec9.1/Sec9.2, all 13 tables enumerated by
// Team A's U1 pass): a property is found by the engine's lower-cased CRC-32
// name hash (sr3save::nameHash), first in the object's own type's table and
// then, on a miss, up the type's parent chain (Sec9.1: "the per-type
// descriptor lookup (0x00e28980) walks up this same parent chain on a miss";
// spec-vint-doc-format.md Sec5). Every row carries a type/size code (Sec9.2
// +0x04) that drives how vint_set_property parses its trailing Lua values and
// how many values vint_get_property pushes (Sec20.1/Sec20.2).
//
// Representation (this project's own choice): one static table per type,
// rows in the spec's own listed order, looked up by (type name, name hash).
// A type's name is matched exactly (document type strings are the engine's
// own lower-case names).
//
// Per-row codes - WHERE EACH ONE COMES FROM. The clean spec text names every
// row of all 13 tables but does NOT reproduce a per-row code column (Sec9.2:
// "full per-row detail ... is in review/exe-notes-2026-10-01/
// interp_vint_u1_property_tables.md Sec4, not reproduced in full here"; that
// note is Team A's disassembly-side material, outside this project's
// clean-room boundary, and was not read). So each row is tagged with its
// source:
//   Spec      - the code is stated for this row in spec text: the 10 code-0xd
//               rows (Sec9.2a: exactly one enum binding per 0xd row, 10
//               bindings), the 3 code-0xe rows (Sec9.2 / Sec20.1), 9 =
//               document_name (Sec9.2), 0xa = bitmap/bitmap_circle image and
//               7 = bitmap source_nw/source_se, bool = bitmap
//               custom_source_coords (spec-vint-doc-format.md Sec8 item 7),
//               8 = tween end_event (Sec20.1 case 8 + vint_lib.lua:1172),
//               3 = sr2_map zoom (spec-hud-runtime.md Sec6, 2026-10-08 pass:
//               the get/set bodies 0x00e34420/0x00e34460 are read directly
//               and read/write this+0xa0 as a float - added 2026-10-08,
//               minimap live-transform task; previously left Open because
//               spec-lua-bindings.md Sec9.2's own table lists property names
//               only, not a per-row code column).
//   FieldType - element's 17 rows (and their identical re-listings on
//               group/point/text/bitmap_circle): spec-vint-render.md Sec1
//               CONFIRMS the object field behind every one (3 floats, 2
//               floats, float, bool byte, int, dword enum) and Sec7.2 the
//               2-value shape of screen_*; the code is the only code in
//               Sec9.2's vocabulary with that shape (vec3 -> 6, vec2 -> 7,
//               float -> 3, bool byte -> 5, signed int -> 1). This is a
//               deduction from two confirmed texts, not a stated per-row
//               code - flagged so Team A can confirm the column.
//   Open      - no clean text gives the code (52 of 96 rows). vint_set_property
//               / vint_get_property store and echo such a row's value as the
//               Lua caller gave it (CHOSEN pass-through - the pre-descriptor
//               behaviour), never a parsed/converted guess.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sr3luahost {

// Sec9.2 +0x04 vocabulary (CONFIRMED U1: 1-9, 0xa, 0xb, 0xd, 0xe; no 0xc).
// 0 is this project's own "code not known from clean spec text" marker.
enum : uint8_t {
    kVintCodeOpen = 0,
    kVintCodeSignedInt = 1,
    kVintCodeUnsignedInt = 2,
    kVintCodeFloat = 3,
    kVintCodeString = 4,
    kVintCodeBool = 5,
    kVintCodeVec3 = 6,
    kVintCodeVec2 = 7,
    kVintCodeCallback = 8,
    kVintCodeDocumentName = 9,
    kVintCodeImageName = 0xa,
    kVintCodeFontName = 0xb,
    kVintCodeEnum = 0xd,
    kVintCodeVariant = 0xe,
};

enum class VintCodeSource { Open, Spec, FieldType };

struct VintPropertyDescriptor {
    const char* name;
    uint32_t nameHash;   // sr3save::nameHash(name), filled at first use
    uint8_t code;        // kVintCode*, kVintCodeOpen when not in clean text
    VintCodeSource source;
};

struct VintElementTypeInfo {
    const char* name;
    const char* parent;  // nullptr = no parent (element, tween, animation - Sec9.1)
    std::vector<VintPropertyDescriptor> rows;
};

// All 13 types in registration order (Sec9.1: element, tween, animation,
// bitmap, gradient, text, group, document, point, clip, bitmap_circle,
// sr2_map, video).
const std::vector<VintElementTypeInfo>& vintElementTypes();
const VintElementTypeInfo* findVintElementType(const std::string& typeName);
// `typeName` is `ancestor` or derives from it (Sec9.1 parent chain).
bool vintTypeIsA(const std::string& typeName, const char* ancestor);
// is-an-`element` (spec-vint-render.md Sec2.3 step 6 / Sec7.1): element and
// the 10 types under it; tween/animation are not.
inline bool vintIsElementFamily(const std::string& typeName) { return vintTypeIsA(typeName, "element"); }

struct VintDescriptorHit {
    const VintPropertyDescriptor* row = nullptr;
    const VintElementTypeInfo* owner = nullptr; // the table the row was found in
};
// Own table first, then up the parent chain (CONFIRMED, Sec9.1/vint-doc Sec5).
// nullopt = no row anywhere on the chain (or an unknown type name).
std::optional<VintDescriptorHit> findVintPropertyDescriptor(const std::string& typeName, uint32_t nameHash);

// How many Lua values one value of `code` takes/pushes: 1 for every scalar,
// bool and name code, 2 for vec2, 3 for vec3 (Sec20.1: "one, two, or three
// trailing values"; Sec20.2: scalar/bool/string 1, vec2 2). 0 for
// kVintCodeOpen / kVintCodeVariant (no fixed shape).
int vintCodeArity(uint8_t code);

// The 10 code-0xd enumeration tables (Sec9.2a): case-insensitive name lookup,
// matched against a binding whose type is ancestor-or-self of `typeName` in
// either direction. `tween.algorithm` is only partly known in clean text
// (Sec9.2a lists linear=0 and ease_in_out_bounce=24 by name, the 23 between
// only as "8 families x {in, out, in-out}"), so a name not listed there is
// Open rather than Miss.
enum class VintEnumLookup { Found, Miss, Open };
VintEnumLookup lookupVintEnum(const std::string& typeName, const std::string& propertyName,
                              const std::string& valueName, int32_t* valueOut);

} // namespace sr3luahost
