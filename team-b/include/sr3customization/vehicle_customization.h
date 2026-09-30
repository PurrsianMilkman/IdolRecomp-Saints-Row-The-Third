#pragma once

// sr3customization - typed reader for the vehicle-customization VARIANT file
// (spec-customization-data.md section 3): `<vehicle_name>.xtbl` (note: NO
// `_veh` suffix - a DIFFERENT file from spec-vehicle-data.md section 7's
// `<vehicle_name>_veh.xtbl` stats file), e.g. `sp_mdsphere_genki.xtbl`.
//
// Built ENTIRELY on sr3xtbl's accessors (include/sr3xtbl/xtbl.h). See
// customization.h's file banner for the shared conventions (Always<T> vs
// optional<T> defaulting, judgement-call fields, the container-fix
// correction to the spec's own §1/§4 item 1) - this header follows the same
// rules and does not repeat them here.
//
// ---------------------------------------------------------------------------
// SCOPE
// ---------------------------------------------------------------------------
// Implements spec section 3's full schema: `Vehicle > Name`, `Variants >
// Variant`, and, PER VARIANT, `Components > Component_Group`, `Colors >
// Color_Group`, `Wheels > Wheel_Group`.
//
// REAL-DATA CORRECTION (not from spec text - the spec's own prose lists
// Components/Colors/Wheels as three bullets parallel to "Vehicle > Name,
// then Variants > Variant", which reads ambiguously as either vehicle-level
// or variant-level groups; it is not resolved by the spec's prose alone).
// Direct inspection of the real worked example itself, `sp_mdsphere_genki.xtbl`
// (DLC1, fully reliable/raw - fair game per this project's rule 2), and
// every other real `<vehicle_name>.xtbl` checked (see
// tools/validation/validate_customization_population.cpp's G4), shows
// `<Vehicle>`'s own children are ONLY `<Name>` and `<Variants>` - NOT
// Components/Colors/Wheels. Those three groups are each children of EVERY
// individual `<Variant>` instead: `Variants > Variant > {File_Id, Name,
// Weight, ParkingWeight, Type, Siren, Fully_Customizable, Has_Peg, Bitmap,
// Components, Colors, Wheels}`. This makes structural sense (each spawnable
// paint-job/style variant - rich/poor/pimped/... - has its OWN independent
// component/color/wheel weighted-random pools, not one pool shared by every
// variant of the vehicle) and is why VehicleVariant, not
// VehicleCustVariants, owns componentGroups/colorGroups/wheelGroups below.
// Every FIELD NAME and inner nesting below (Component_Group,
// Component_Chances/Component_Chance, Components/Component_Element,
// Externalized_Components/Externalized_Component, Color_Group,
// Color_Chances/Color_Chance, Color_Choices/Color_Choice, the `Color`
// wrapper, Wheel_Group's five fields) IS the spec's own text and was
// confirmed, unchanged, against the same real sample - only the TOP-LEVEL
// attachment point (Vehicle vs. Variant) was corrected.
//
// Cross-referenced tables named by section 3's
// "Cross-reference chain" paragraph (`vehicle_cust_color_pool.xtbl` /
// `vehicle_cust_color_sets.xtbl` for the Color_Pool/Color_Set names this
// file references, and the per-vehicle `_cust.xtbl` slot-definition file)
// are OUT OF SCOPE here per this task's instructions and customization.h's
// "DELIBERATELY OPEN" list - this reader stores the raw reference text/kind
// only, resolving nothing.
//
// ---------------------------------------------------------------------------
// JUDGEMENT CALL: the `Color` ComboElement's XML shape
// ---------------------------------------------------------------------------
// Spec section 3 lists `Color_Choice`'s four fields as "Slot, Component,
// Color_Slot, Color - a ComboElement offering either a single Color_Pool
// reference or a Color_Set (palette) reference". Slot/Component/Color_Slot
// are literal, flat child-element names of Color_Choice. This reader treats
// `Color` the same way - a literal child element of Color_Choice, itself
// wrapping EITHER a `Color_Pool` or a `Color_Set` child (whichever is
// present; `Color_Pool` is tried first) - rather than assuming Color_Pool/
// Color_Set sit directly under Color_Choice with no `Color` wrapper. This is
// the more structurally consistent reading (four parallel named fields) but
// is NOT independently confirmed by the spec's own worked example (section
// 3's `sp_mdsphere_genki.xtbl` walkthrough does not show a concrete
// Color_Choice), so it is flagged here as a judgement call, exactly as this
// project's convention requires (see e.g. sr3vehicleinfo's
// NoCustVariantsGrid/NoCustVariantElement precedent). The synthetic test
// suite (tests/synthetic_customization_test.cpp) exercises exactly this
// shape; the validation harness reports, as a denominator, how many
// Color_Choice elements in real data have EITHER shape so a reviewer can
// confirm or correct the call against real base-game data.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "sr3xtbl/xtbl.h"

namespace sr3customization {

using sr3xtbl::Always;
using sr3xtbl::Document;
using sr3xtbl::Node;

// `Variants > Variant`'s `Type` enum (spec 3: "a Selection-typed enum:
// confirmed choices rich/poor/pimped/riced/normal, from the inline
// TableDescription"). Index into this array == EnumIndex's result; -1 if
// absent/unmatched (this project's general enum-field convention, e.g.
// sr3vehicleinfo's Special_Vehicle_Type).
inline constexpr std::string_view kVehicleVariantTypeNames[5] = {
    "rich", "poor", "pimped", "riced", "normal",
};

// `Components > Component_Group > Component_Chances > Component_Chance >
// Components > Component_Element` (spec 3). Defined ahead of VehicleVariant
// since a variant owns a vector of these (see the REAL-DATA CORRECTION
// banner above).
struct ComponentElement {
    std::optional<std::string> slot;       // Slot
    std::optional<std::string> component;  // Component
};

// `... > Externalized_Components > Externalized_Component` (spec 3).
struct ExternalizedComponent {
    std::optional<std::string> component;  // Externalized_Component > Component
};

// `Component_Chances > Component_Chance` (spec 3).
struct ComponentChance {
    std::optional<std::string> name;  // Name
    Always<float> weight;               // Weight (see VehicleVariant::weight's note)

    std::vector<ComponentElement> components;                    // Components > Component_Element
    std::vector<ExternalizedComponent> externalizedComponents;   // Externalized_Components > Externalized_Component
};

// `Components > Component_Group` (spec 3, e.g. "Chassis", "Wheels").
struct ComponentGroup {
    std::optional<std::string> name;  // Name
    std::vector<ComponentChance> componentChances;  // Component_Chances > Component_Chance
};

// The `Color` ComboElement (spec 3) - see the JUDGEMENT CALL banner above.
enum class ColorRefKind { None, ColorPool, ColorSet };
struct ColorRef {
    ColorRefKind kind = ColorRefKind::None;
    std::string value;  // the referenced Color_Pool or Color_Set name; empty when kind == None
};

// `Color_Choices > Color_Choice` (spec 3).
struct ColorChoice {
    std::optional<std::string> slot;        // Slot
    std::optional<std::string> component;    // Component
    std::optional<std::string> colorSlot;     // Color_Slot
    ColorRef color;                             // Color (ComboElement: Color_Pool XOR Color_Set)
};

// `Color_Chances > Color_Chance` (spec 3).
struct ColorChance {
    std::optional<std::string> name;  // Name
    Always<float> weight;               // Weight
    std::vector<ColorChoice> colorChoices;  // Color_Choices > Color_Choice
};

// `Colors > Color_Group` (spec 3).
struct ColorGroup {
    std::optional<std::string> name;  // Name
    std::vector<ColorChance> colorChances;  // Color_Chances > Color_Chance
};

// `Wheels > Wheel_Group` (spec 3).
struct WheelGroup {
    std::optional<std::string> name;  // Name
    Always<float> frontWidth;           // Front_Width
    Always<float> frontSize;             // Front_Size
    Always<float> rearWidth;              // Rear_Width
    Always<float> rearSize;                // Rear_Size
    Always<float> weight;                    // Weight
};

// One `Variants > Variant` (spec 3): a spawnable paint-job/style variant.
struct VehicleVariant {
    // File_Id: no numeric width given by the spec. INTERPRETATION (judgement
    // call, same footing as customization.h's Price/Respect_Bonus): signed
    // 32-bit.
    Always<int32_t> fileId;

    std::optional<std::string> name;  // Name

    // Weight / ParkingWeight: "spawn-frequency weights - the same 'weights
    // summing to ~100 as a percentage' convention seen in the .xtbl schema-
    // description text itself" (spec 3) - modeled as float (a percentage-like
    // weight, consistent with every other *_Weight/*_Chance field in this
    // project's other tables, e.g. Component_Chance/Color_Chance above).
    Always<float> weight;         // Weight
    Always<float> parkingWeight;  // ParkingWeight

    int32_t type = -1;  // Type; EnumIndex over kVehicleVariantTypeNames, -1 if absent/unmatched

    Always<bool> siren;               // Siren
    Always<bool> fullyCustomizable;   // Fully_Customizable
    Always<bool> hasPeg;              // Has_Peg

    std::optional<std::string> bitmap;  // Bitmap (a texture/bitmap reference name)

    // Components / Colors / Wheels: PER-VARIANT groups (see the REAL-DATA
    // CORRECTION banner above - confirmed children of `<Variant>`, not of
    // `<Vehicle>`).
    std::vector<ComponentGroup> componentGroups;  // Components > Component_Group
    std::vector<ColorGroup> colorGroups;           // Colors > Color_Group
    std::vector<WheelGroup> wheelGroups;             // Wheels > Wheel_Group
};

// One `<Vehicle>` row of a `<vehicle_name>.xtbl` variant file (spec 3: "Full
// schema, from the same worked example (sp_mdsphere_genki.xtbl, DLC1)").
struct VehicleCustVariants {
    std::optional<std::string> vehicleName;  // Vehicle > Name

    std::vector<VehicleVariant> variants;  // Variants > Variant (each with its OWN component/color/wheel groups)
};

// Parses one `<Vehicle>` element (of a `<vehicle_name>.xtbl` variant file)
// into a VehicleCustVariants. `row` must be that element (case-insensitive),
// directly under `<root><Table>`.
VehicleCustVariants ParseVehicleCustVariants(const Node* row);

// Convenience: parses every `<Vehicle>` row of a whole `<vehicle_name>.xtbl`
// variant-file document, in file order (spec 3's worked example shows one
// row per file, but this reader does not assume that - see
// ParseVehicleCustVariants for the single-row form).
std::vector<VehicleCustVariants> ParseAllVehicleCustVariants(const Document& doc);

}  // namespace sr3customization
