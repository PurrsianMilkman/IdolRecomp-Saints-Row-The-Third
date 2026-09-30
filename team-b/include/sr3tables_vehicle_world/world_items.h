#pragma once

// sr3tables_vehicle_world - sections 12-19: level_objects.xtbl, props.xtbl,
// triggers.xtbl, items_inventory.xtbl, items_3d.xtbl (partial),
// contacts_sr3.xtbl, activity_player_persona_replacement.xtbl,
// airplane_takeoff_land_curves.xtbl (spec-tables-vehicle-world.md 12-19).
// Included from tables.h; see that file's banner for the shared conventions
// and the cleanroom sourcing statement (spec-tables-vehicle-world.md ONLY).
//
// items_3d.xtbl (section 16) is intentionally the least complete struct in
// this file: the spec's own coverage table (section 1.1) lists it as
// "Array/resolver/row-tag CONFIRMED - disassembly and empirical; full
// per-item byte offsets OPEN" - the real per-row builder (FUN_00904d10) was
// never decompiled. Only element names the spec states are either consumed
// by SOME other fully-read pass over the same document, or empirically
// observed in real base rows, are modelled - all as raw text/counts, no byte
// offset is claimed for any of them because the spec gives none. Per the
// task's naming-correction note (spec 16.1): "items_preload_containers" and
// "items_containers" are profiler/memory-pool zone labels, NOT XML section
// names, and neither string appears anywhere in this file.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "sr3xtbl/xtbl.h"

namespace sr3tables_vehicle_world {

using sr3xtbl::Always;
using sr3xtbl::Document;
using sr3xtbl::Node;
using sr3xtbl::Vec3Result;

// ===========================================================================
// 12. level_objects.xtbl - placeable-object physical/audio/visual properties
//     catalogue (spec-tables-vehicle-world.md 12)
// ===========================================================================
// The 27-literal Flags vocabulary (spec 12.2); receives_bullet_impulse lives
// at +0xC0 bit31, the other 26 at +0xC4 - a bit-position detail this reader
// does not reproduce (see the "always-write default" convention in tables.h's
// banner: these are individual bool fields, not a raw bitmask).
inline constexpr std::array<std::string_view, 27> kLevelObjectFlagNames = {
    "receives_bullet_impulse", "disappear_on_death", "disable_lights_on_dislodge", "disable_effects_on_dislodge",
    "ignore_human_collision", "camera_collide", "vehicle_camera_collide", "ignore_bullet_collision",
    "bullets_penetrate", "fire_hydrant", "do_not_aim_at_me", "non_walkable", "can_walk_up",
    "nearby_player_despawn", "shatters_against_world", "blackjack", "poker", "zombie", "basketball", "tv",
    "no_detour_until_moved", "does_not_generate_detour", "generate_detour_even_if_in_air", "breakable_glass",
    "ai_los_ignore", "damaged_by_players_only", "no_brute_pickup",
};
// Vehicle_Obstacle: 3-literal enum; "true" is the default/no-op value, only
// "false"/"unanchored" set a bit (spec 12.1).
inline constexpr std::array<std::string_view, 3> kVehicleObstacleNames = {"true", "false", "unanchored"};

struct LevelObjectAnchored {
    bool present = false;                        // Anchored wrapper present (+0xC0 bits 0-1 / +0xC4 bit19)
    Always<uint32_t> dislodgeHitpoints;            // Anchored/Dislodge_Hitpoints
    std::optional<std::string> dislodgeEffect;     // Anchored/Dislodge_Effect -> effects CRC (default -1)
    std::optional<uint32_t> coinsReleased;         // Anchored/Coins_Released (if-present)
    bool dislodgeOnDeath = false;                   // Dislodge_On_Death (+0xC0 bit13)
    bool dislodgeNotoriety = false;                 // Dislodge_Notoriety (+0xC0 bit30)
    // Anchored/Dislodged_By/Element (spec 12.3, a confirmed dead-element
    // finding, e.g. `<Dislodged_By><Element>none</Element></Dislodged_By>`):
    // real authored data that FUN_008e7490's full body never reads - not
    // modelled, matching this project's convention of not inventing a
    // destination for content the spec explicitly documents as unconsumed.
};

struct LevelObjectCollisionSound {
    std::optional<float> vehicleIVS;              // Collision_Sound/VehicleIVS (if-present; presence sets +0xC0 bit28)
    std::optional<float> objectMVS;                // Collision_Sound/ObjectMVS (if-present; presence sets +0xC0 bit29)
    std::optional<std::string> foleyCollision;      // Collision_Sound/FoleyCollision -> foley id
};

struct LevelObjectDeathMoney {
    Always<uint32_t> min;    // Death_Money/Min
    Always<uint32_t> max;    // Death_Money/Max
    Vec3Result point;         // Death_Money's own X/Y/Z (the "Cash_Out_Point"-labelled vec3 - CONFIRMED empirically
                              // to be Death_Money's own children, not a separate wrapper, spec 12.1/12.3)
    Always<bool> justCoins;   // Death_Money/Just_Coins
};

struct LevelObject {
    std::optional<std::string> name;  // Name (char[0x30] bounded copy)
    Always<uint32_t> hitpoints;        // Hitpoints
    std::optional<std::string> material;  // Material -> the 33 physical-material names (spec-tables-weapons-combat.md
                                          // 1.6's FUN_006F76F0 - cited but NOT enumerated by THIS spec, so no name
                                          // list is reproduced here per the cleanroom single-source rule; raw text
                                          // kept, no index/default computed)
    Always<float> lifetimeSeconds;      // Lifetime_seconds (RAW seconds, pre ->ticks conversion)
    Always<float> weight;                // Weight (RAW, pre unsigned-wraparound-correction/scale)
    // Friction/Restitution/Angular_Damping/Linear_Damping/Buoyancy_Modifier/Vehicle_Repulsor_Scale: spec 12.1 states
    // each has a concrete if-present default, but names the default only as an unresolved constant address
    // (DAT_0126d2cc / DAT_01115d6c / DAT_012a2dd0 / DAT_01117a4c) with no numeric value given anywhere in this
    // spec - so no `...OrDefault()` helper is offered for these (would require guessing a number the spec does
    // not state), unlike Surface_Velocity whose default (0) the spec DOES give as a literal.
    std::optional<float> friction;          // Friction (if-present; default DAT_0126d2cc, not numerically given)
    std::optional<float> restitution;        // Restitution (if-present; default DAT_01115d6c, not numerically given)
    std::optional<float> angularDamping;     // Angular_Damping (if-present; default DAT_01115d6c, not numerically given)
    std::optional<float> linearDamping;      // Linear_Damping (if-present; default DAT_012a2dd0, not numerically given)
    std::optional<float> surfaceVelocity;    // Surface_Velocity (if-present; spec-stated default 0)
    float SurfaceVelocityOrDefault() const { return surfaceVelocity.value_or(0.0f); }
    std::optional<float> buoyancyModifier;   // Buoyancy_Modifier (if-present; default DAT_01117a4c, not numerically given)

    LevelObjectAnchored anchored;             // Anchored (whole block optional; see ::present)
    std::optional<std::string> emittingSound;  // Emitting_Sound -> audio id (FUN_00462960)
    LevelObjectCollisionSound collisionSound;   // Collision_Sound
    std::optional<std::string> deathEffect;      // Death_Effect -> effects CRC (default -1)
    std::optional<std::string> deathExplosion;   // Death_Explosion -> explosions CRC (default 0)
    LevelObjectDeathMoney deathMoney;             // Death_Money

    std::optional<float> vehicleRepulsorScale;    // Vehicle_Repulsor_Scale (if-present; default DAT_01117a4c, not
                                                   // numerically given)
    Vec3Result comOffset;           // center_of_mass/com_offset
    Vec3Result comOffsetCorpse;     // center_of_mass/com_offset_corpse

    bool movableByHumans = false;    // +0xC0 bit24 (Movable_By_Humans - not part of the 27-literal Flags vocabulary)
    std::optional<std::string> vehicleObstacle;  // Vehicle_Obstacle -> kVehicleObstacleNames

    // The 27-literal Flags vocabulary (spec 12.2).
    bool receivesBulletImpulse = false;
    bool disappearOnDeath = false;
    bool disableLightsOnDislodge = false;
    bool disableEffectsOnDislodge = false;
    bool ignoreHumanCollision = false;
    bool cameraCollide = false;
    bool vehicleCameraCollide = false;
    bool ignoreBulletCollision = false;
    bool bulletsPenetrate = false;
    bool fireHydrant = false;
    bool doNotAimAtMe = false;
    bool nonWalkable = false;
    bool canWalkUp = false;
    bool nearbyPlayerDespawn = false;
    bool shattersAgainstWorld = false;
    bool blackjack = false;
    bool poker = false;
    bool zombie = false;
    bool basketball = false;
    bool tv = false;
    bool noDetourUntilMoved = false;
    bool doesNotGenerateDetour = false;
    bool generateDetourEvenIfInAir = false;
    bool breakableGlass = false;
    bool aiLosIgnore = false;
    bool damagedByPlayersOnly = false;
    bool noBrutePickup = false;
};

LevelObject ParseLevelObject(const Node* row);
std::vector<LevelObject> ParseLevelObjectsTable(const Document& doc);

// ===========================================================================
// 13. props.xtbl - per-activity decorative-prop counts (spec-tables-vehicle-world.md 13)
// ===========================================================================
// NOTE, an internal inconsistency in the spec worth flagging explicitly: the
// prose lists these 14 literals with underscores ("assault_thug", ...), but
// the very next sentence (13, "Validation") states real base data matches
// with SPACE-separated text ("space-separated in the XML, e.g. assault
// thug") and reports 14/14. Since the empirical statement is the one that
// describes what was actually matched against real rows, the space-separated
// form is used here as the canonical literal set (CONFIRMED - empirical
// takes priority for what actually matches); see this library's report for
// this call-out.
inline constexpr std::array<std::string_view, 14> kPropsActivityNames = {
    "assault thug", "assault killa", "assault gangsta", "assault kingpin",
    "kill thug", "kill killa", "kill gangsta", "kill kingpin",
    "shooting thug", "shooting killa", "shooting gangsta", "shooting kingpin",
    "destroy gang car", "collect item pickup",
};

struct PropsActivityCount {
    std::optional<std::string> name;  // Name -> kPropsActivityNames (matched _stricmp; unmatched rows are not
                                       // stored by the real loader - kept here regardless)
    Always<int32_t> numProps;          // Num_Props (always)
};

PropsActivityCount ParsePropsActivityCount(const Node* row);
std::vector<PropsActivityCount> ParsePropsTable(const Document& doc);

// ===========================================================================
// 14. triggers.xtbl - default properties patched onto pre-placed trigger
//     instances (spec-tables-vehicle-world.md 14)
// ===========================================================================
inline constexpr std::array<std::string_view, 5> kTriggerFlagNames = {
    "check_npcs", "continuous_activation", "disabled_for_demo", "ignore_vehicles", "ignore_on_foot",
};
inline constexpr std::array<std::string_view, 2> kTriggerIconTypeNames = {"Save", "Store"};

struct Trigger {
    // Name is matched via a multiply-by-33 bucket hash (FUN_00DAB330,
    // modulus 20) against an ALREADY-POPULATED runtime trigger-instance
    // table (spec 14); a row with no match is silently discarded by the real
    // loader. Not modelled (this reader has no runtime instance table to
    // consult) - the row's own XML content is surfaced regardless.
    std::optional<std::string> name;
    std::optional<std::string> effect;    // Effect -> effects CRC (FUN_005C50B0)
    std::optional<std::string> icon;       // Icon -> resolved via FUN_00802f60 (raw text kept; the spec does not
                                           // state the resolved value's own type beyond "a value resolved through")
    std::optional<std::string> foley;      // Foley -> audio id (FUN_00462960)
    std::optional<std::string> iconType;   // IconType -> kTriggerIconTypeNames (2 literals); "does not appear in any
                                           // real row" per spec 14.2 (only in the table's own TableDescription block)
    std::optional<std::string> useMessage;  // UseMessage -> localized-string handle

    bool flagsPresent = false;  // Flags child present
    bool checkNpcs = false;
    bool continuousActivation = false;
    bool disabledForDemo = false;
    bool ignoreVehicles = false;
    bool ignoreOnFoot = false;
};

Trigger ParseTrigger(const Node* row);
std::vector<Trigger> ParseTriggersTable(const Document& doc);

// ===========================================================================
// 15. items_inventory.xtbl - player inventory item catalogue
//     (spec-tables-vehicle-world.md 15)
// ===========================================================================
struct InventoryItem {
    std::optional<std::string> name;   // Name (+0x00, row key; interned via FUN_00A74910)
    // Framework - filter only; UNLIKE this group's usual Framework-filter
    // idiom, items_inventory's BASE load accepts every row regardless of
    // Framework text (only a DLC-framework load filters by exact match,
    // spec 15.1) - kept here as raw text for completeness.
    std::optional<std::string> framework;
    std::optional<std::string> displayName;  // DisplayName -> localization handle (+0x08; spec-stated placeholder
                                              // default text when absent, not itself given verbatim by the spec)
    std::optional<std::string> bitmap;        // Bitmap -> resource handle (+0x10; "-1" default when text empty)
    std::optional<float> impactShapeMinOffset;  // Impact_shape_min_offset (if-present, default 0)
    float ImpactShapeMinOffsetOrDefault() const { return impactShapeMinOffset.value_or(0.0f); }
    std::optional<int32_t> cost;               // Cost (if-present, default -1)
    int32_t CostOrDefault() const { return cost.value_or(-1); }
    std::optional<int32_t> defaultCount;       // Default_Count (if-present, default 1)
    int32_t DefaultCountOrDefault() const { return defaultCount.value_or(1); }
    std::optional<int32_t> maxInventory;       // Max_Inventory (if-present, default = the resolved Default_Count value)
    int32_t MaxInventoryOrDefault() const { return maxInventory.value_or(DefaultCountOrDefault()); }
    std::optional<std::string> description;    // Description -> resolved string pointer (+0x24; default 0)
    std::optional<std::string> useScript;        // Use_Script (+0x2C; single recognised literal "none" - raw text
                                                 // kept; the no-match translation-table read is OPEN/"not examined
                                                 // further" per spec 15.1, not modelled)
};

InventoryItem ParseInventoryItem(const Node* row);
// spec 15.1: the base load accepts every row unconditionally (no Framework
// filter); 110 fixed slots, 94 populated in real base data (spec 15.2).
std::vector<InventoryItem> ParseItemsInventoryTable(const Document& doc);

// ===========================================================================
// 16. items_3d.xtbl - 3D item/prop mesh catalogue (PARTIAL - spec-tables-
//     vehicle-world.md 16; see this file's banner for why)
// ===========================================================================
struct ItemMesh {
    std::optional<std::string> filename;  // Mesh/Filename -> .csmesh_pc
};
struct ItemCharacterMesh {
    std::optional<std::string> filename;      // character_mesh/character_mesh/Filename -> .ccmesh_pc
    std::optional<std::string> rigFilename;    // character_mesh/Rig or /rig/Filename -> .rigx (spec 16.1: "Rig/rig")
    std::optional<std::string> animSet;        // character_mesh/Anim_set (HYPOTHESIS/OPEN-tier per spec 16.1, but a
                                                // confirmed-to-exist element empirically, e.g. "none"/"NONE")
};
struct ItemProp {
    std::vector<std::string> flags;  // Flags/Flag (repeated; tested elsewhere against the literal "Attach by default")
};

struct Item3D {
    std::optional<std::string> name;   // Name (row tag confirmed "Item"; key CRC at +0x04 of the resolver's own
                                        // record - stride/count/resolver are all from the OTHER, already-known
                                        // array anchor cited by spec 16, not re-derived here)
    std::optional<ItemMesh> mesh;                     // Mesh (wrapper)
    std::optional<ItemCharacterMesh> characterMesh;   // character_mesh (wrapper)
    std::vector<ItemProp> props;                       // Props/Prop (spec 16.2: all 391 real rows have a Props
                                                        // wrapper, usually empty)
    Always<bool> largeProp;                             // LargeProp
    std::optional<std::string> streamingCategory;        // streaming_category -> resolved via FUN_00904770

    // Empirically observed in real base rows but NOT traced to any confirmed
    // reader this pass (spec 16.1: "these are HYPOTHESIS/OPEN, not confirmed
    // dead" - still real, confirmed-to-exist XML elements, surfaced raw per
    // this project's confidence discipline).
    std::optional<std::string> glowType;     // Glow_Type
    std::optional<std::string> scaleAmbient;  // Scale_Ambient
    bool colorVariantsPresent = false;         // Color_Variants (seen empty in real data)
    std::vector<std::string> itemFlags;         // Item_Flags/Flag - a DIFFERENT wrapper from Props/Prop/Flags
                                                // (e.g. `<Item_Flags><Flag>inherit_bone_transforms</Flag></Item_Flags>`)

    // The per-item "contained items" fixup mechanism (spec 16.1) is
    // confirmed to EXIST but its own XML element name was never identified
    // by the spec ("candidates not ruled out: a per-Item child element,
    // since no separate top-level section exists") - not modelled, since
    // there is no confirmed name to read.
};

Item3D ParseItem3D(const Node* row);
std::vector<Item3D> ParseItems3DTable(const Document& doc);

// ===========================================================================
// 17. contacts_sr3.xtbl - in-game phone contact list (spec-tables-vehicle-world.md 17)
// ===========================================================================
struct Contact {
    std::optional<std::string> name;    // Name (+0x00-0x3F, raw NUL-terminated unbounded copy)
    std::optional<std::string> image;    // Image (+0x40-0x7F, same)
    std::optional<std::string> persona;   // Persona -> audio persona id (+0x80, FUN_00462960)
};

Contact ParseContact(const Node* row);
// Fixed capacity 35 (spec 17.1); 31 real rows (spec 17.2).
std::vector<Contact> ParseContactsTable(const Document& doc);

// ===========================================================================
// 18. activity_player_persona_replacement.xtbl - per-activity player-persona
//     substitution (spec-tables-vehicle-world.md 18)
// ===========================================================================
struct PersonaReplacement {
    std::optional<std::string> original;     // Original -> persona resolver (FUN_0070a2f0)
    std::optional<std::string> replacement;   // Replacement -> same resolver (if-present; "none" if absent)
};

struct ActivityPersonaReplacement {
    // Name -> the activity-name resolver (FUN_00614d70); unmatched names are
    // silently skipped by the real loader (spec 18.1) - raw text kept
    // regardless.
    std::optional<std::string> name;
    std::vector<PersonaReplacement> personaReplacements;  // Persona_Replacements/Persona_Replacement
};

ActivityPersonaReplacement ParseActivityPersonaReplacement(const Node* row);
std::vector<ActivityPersonaReplacement> ParseActivityPlayerPersonaReplacementTable(const Document& doc);

// ===========================================================================
// 19. airplane_takeoff_land_curves.xtbl - takeoff/landing approach-curve
//     tuning (spec-tables-vehicle-world.md 19)
// ===========================================================================
// Exactly two named curves are recognised (spec 19); any other Name is
// silently ignored by the real loader.
inline constexpr std::array<std::string_view, 2> kAirplaneCurveNames = {"Landing", "Take Off"};

struct AirplaneCurvePoint {
    Always<float> offsetHeight;  // Offset_Height
    Always<float> offsetDist;     // Offset_Dist
    Always<float> speed;          // Speed
    // +0x00 of the runtime record is reserved/explicitly zeroed, not read
    // from XML (spec 19.1) - nothing to model.
};

struct AirplaneCurveParams {
    std::optional<std::string> name;      // Name -> kAirplaneCurveNames (2 literals); any other name silently
                                           // ignored by the real loader - raw text kept regardless
    std::vector<AirplaneCurvePoint> points;  // Points/Point
    // The derived "slope" value computed from the first two points when a
    // curve has >= 2 points (spec 19.1, HYPOTHESIS on its downstream use) is
    // a load-time derived value, not XML content - not modelled.
};

AirplaneCurveParams ParseAirplaneCurveParams(const Node* row);
std::vector<AirplaneCurveParams> ParseAirplaneTakeoffLandCurvesTable(const Document& doc);

}  // namespace sr3tables_vehicle_world
