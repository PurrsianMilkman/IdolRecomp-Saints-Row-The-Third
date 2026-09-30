#pragma once

// sr3tables_vehicle_world - section 11: the shared "LightSet" table -
// Vehicle-Customization-Lightset.xtbl, store_gang_lightset.xtbl,
// test_shop_light_01.xtbl (spec-tables-vehicle-world.md 11). Included from
// tables.h; see that file's banner for the shared conventions and the
// cleanroom sourcing statement (spec-tables-vehicle-world.md ONLY).
//
// spec 11.1 is explicit that the disassembly-confirmed record (FUN_00598160)
// is the RUNTIME ACTIVATION pass, not the XML-sourced row - it is not
// modelled here at all. What IS modelled is spec 11.2's element vocabulary,
// which is CONFIRMED - EMPIRICAL (one of this spec's own top-tier confidence
// ratings, spec's Confidence key) even though no byte offset was ever
// recovered for it (the real per-field reader, inside FUN_005984c0, was not
// decompiled - spec 23 item 2). A confirmed element vocabulary is enough to
// write a correct XML->struct reader without needing a byte offset for
// anything, so this table is implemented essentially in full despite being
// listed among the spec's "OPEN" items for a DIFFERENT reason (byte layout,
// not vocabulary).
//
// spec 11.2 flags Color/Position as an unusual INLINE "r g b" / "x y z"
// vec3 (space-separated, NOT X/Y/Z children) - "unlike almost every other
// vec3 in this project". Orientation/Hotspot/Attenuation are each
// documented only as "two floats" with no stated encoding (inline text vs.
// two named children) - since the spec does not commit to either shape,
// this reader keeps their RAW TEXT rather than guessing a split (file
// banner: "never given an interpreted meaning the spec itself didn't commit
// to"). Same for Indoor/Outdoor ("int flags" - ambiguous whether this is one
// field or two, and what a "flag" value's bits mean): both are read as
// separate raw text fields, named exactly as spec 11.2 lists them.

#include <optional>
#include <string>
#include <vector>

#include "sr3xtbl/xtbl.h"

namespace sr3tables_vehicle_world {

using sr3xtbl::Always;
using sr3xtbl::Document;
using sr3xtbl::Node;

// A generic "x y z" whitespace-separated inline vec3 (spec 11.2's Color /
// Position convention). Unlike tables_environment.h's ColorRGBAText, the
// spec states NO fallback value for unspecified trailing components here, so
// absent components are simply left at the struct's default (0.0), NOT 1.0 -
// this is a parser default, not a spec-stated one; see this table's own
// notes above.
struct LightSetVec3Text {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

struct Light {
    std::optional<std::string> name;               // Name
    std::optional<std::string> type;                 // Type (seen: "omni", "circular spotlight" - spec 11.2)
    Always<bool> category0, category1, category2, category3;  // Category_0..Category_3
    Always<bool> castShadows;                          // CastShadows
    std::optional<LightSetVec3Text> color;             // Color ("three 0-1 floats, space-separated")
    Always<float> multiplier;                           // Multiplier
    Always<bool> templateFlag;                          // Template (named `templateFlag`: `template` is a C++ keyword)
    std::optional<LightSetVec3Text> position;           // Position (inline vec3, same convention as Color)
    std::optional<std::string> orientation;             // Orientation ("two floats - azimuth/elevation"; raw text -
                                                          // encoding not spec-stated, see file banner)
    std::optional<std::string> indoor;                   // Indoor ("int flags"; raw text, see file banner)
    std::optional<std::string> outdoor;                  // Outdoor
    std::optional<std::string> hotspot;                  // Hotspot ("two floats - spotlight cone angles"; raw text)
    std::optional<std::string> attenuation;              // Attenuation ("two floats - near/far falloff"; raw text)
    std::optional<std::string> slateName;                // Slate_Name
    std::optional<std::string> lightCharacter;           // LightCharacter
    std::optional<std::string> shadowCharacter;          // ShadowCharacter
    std::optional<std::string> lightLevel;                // LightLevel (type not stated by spec 11.2 - raw text kept)
    std::optional<std::string> shadowLevel;               // ShadowLevel (same)
};

struct LightSet {
    std::optional<std::string> name;   // Name
    std::optional<std::string> startTime;  // StartTime (type not stated by spec 11.2 - raw text kept)
    std::optional<std::string> endTime;    // EndTime
    Always<float> exposure;             // Exposure
    Always<bool> rampExposure;          // RampExposure
    std::vector<Light> lights;          // Lights/Light
};

LightSet ParseLightSet(const Node* row);
// The three files each hold exactly one <LightSet> row under <Table> in real
// base data (spec 11.2: "1 LightSet in each of the three files"). Modelled
// as a single-element parse (nullopt if the file has no LightSet row at
// all), matching this table's own documented shape rather than forcing a
// vector for a table that is never repeated in practice.
std::optional<LightSet> ParseLightSetTable(const Document& doc);
inline std::optional<LightSet> ParseVehicleCustomizationLightsetTable(const Document& doc) {
    return ParseLightSetTable(doc);
}
inline std::optional<LightSet> ParseStoreGangLightsetTable(const Document& doc) { return ParseLightSetTable(doc); }
inline std::optional<LightSet> ParseTestShopLight01Table(const Document& doc) { return ParseLightSetTable(doc); }

}  // namespace sr3tables_vehicle_world
