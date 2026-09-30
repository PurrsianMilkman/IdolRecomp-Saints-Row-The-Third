#pragma once

// sr3tables_customization/color_pools.h - the shared named-colour-pool
// family (spec-tables-customization.md 2), items_color_pool.xtbl (3.1) and
// npc_color_palette.xtbl (3.2). Included from sr3tables_customization/tables.h.
//
// Source: spec-tables-customization.md, the ONLY document consulted for this
// file's schema (cleanroom boundary - see the task's HARD RULES). Built
// entirely on include/sr3xtbl/xtbl.h's accessors; nothing here was derived
// from the game executable, disassembly or decompiled code - every
// address/function name cited in a comment is copied verbatim from the
// spec's own prose, purely for cross-reference, never re-derived.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "sr3xtbl/xtbl.h"

namespace sr3tables_customization {

using sr3xtbl::Always;
using sr3xtbl::Document;
using sr3xtbl::Node;

// ---------------------------------------------------------------------------
// A generic "r g b [a]" whitespace-separated colour string. This project's
// established convention for a colour authored as one text blob rather than
// R/G/B child elements (see sr3tables_environment's ColorRGBAText, tables.h,
// which this mirrors): sr3xtbl exposes only the engine float grammar
// (ParseFloat), not C sscanf/atof, so the parser here tokenises on
// whitespace and applies ParseFloat to each token - the closest available
// approximation to the spec's stated "parsed by FUN_00dad0a0" (2.2).
// Unspecified trailing components stay at their default (1.0 for alpha,
// matching the same established convention).
// ---------------------------------------------------------------------------
struct ColorText {
    float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;
};

// ===========================================================================
// 2. The shared named-color-pool family: character_color_pool.xtbl,
// hair_color_pool.xtbl, makeup_color_pool.xtbl, tattoo_color_pool.xtbl.
// [CONFIRMED - disassembly + empirical, spec 2]
//
// "One reader, four files, driven by a data array" (2.1): all four filenames
// share the identical `Color_Entry` row shape (2.2, one shared 48-byte
// layout) - ONE parser below (ParseColorPoolTable) applies to all four.
//
// NOT modelled (2.3, derived/computed, not an XML element): the two extra
// "player"/"ped" derived RGBA triples char_color_balance.xtbl bakes into
// EVERY character_color_pool.xtbl row at load time (pool index 0 only; the
// other three pools just replicate the base colour into both derived slots
// and skip the transform). That whole-table colour-balance remap is a
// load-time transform over the already-parsed pool, not a per-row XML field
// - see ParseColorBalance below for the raw ColorBalance inputs it uses.
// ===========================================================================
struct ColorPoolEntry {
    std::optional<std::string> name;         // Color_Entry > Name (hashed at load into the pool's lookup key; raw text kept)
    std::optional<ColorText> color;           // Color_Entry > Color - "parsed by FUN_00dad0a0" (2.2); see ColorText banner
                                               // above for the judgement call this project makes for a text-blob colour
    std::optional<std::string> displayName;   // Color_Entry > DisplayName; spec: "or 0.0 if absent" (a null localization id,
                                               // not modelled here as a literal value - see 2.2)
};
ColorPoolEntry ParseColorPoolEntry(const Node* row);
// Applies identically to character_color_pool.xtbl, hair_color_pool.xtbl,
// makeup_color_pool.xtbl and tattoo_color_pool.xtbl - "one reader, four
// files" (2.1); pass whichever document you parsed.
std::vector<ColorPoolEntry> ParseColorPoolTable(const Document& doc);

// char_color_balance.xtbl (2.3): a SINGLE-ROW table - "Table -> exactly one
// ColorBalance element, confirmed empirically ... there is no per-entry
// variation, it's one global tuning knob" (2.3, 2.4). Applies only to pool
// index 0 (character_color_pool.xtbl); the derived player/ped colour
// remap itself is a load-time transform, not modelled (see the banner
// above).
struct ColorBalance {
    Always<float> blackLevel, whiteLevel, saturation;           // Black_Level, White_Level, Saturation
    Always<float> pedBlackLevel, pedWhiteLevel, pedSaturation;  // Ped_Black_Level, Ped_White_Level, Ped_Saturation
};
// Reads the single <ColorBalance> element directly under <Table>, if
// present (spec 2.3/2.4: shipped base game has exactly one).
std::optional<ColorBalance> ParseColorBalance(const Document& doc);

// ===========================================================================
// 3.1 items_color_pool.xtbl - a fifth, structurally separate colour pool.
// [CONFIRMED - schema empirical; consumer OPEN, spec 3.1]
//
// NOT part of the §2 four-slot array: its own element tag is `Item_Color`,
// a different address, and its loader was only narrowed to one candidate
// function, not pinned to the exact call/consumer (3.1, open item §22.3).
// The row SHAPE is confirmed from real data: "flat records, three children
// each - Name, Color, _Editor" (3.1). The spec gives no parsing routine for
// this table's Color field (unlike §2's FUN_00dad0a0), so `color` is kept
// as raw text here rather than guessing the same text-colour grammar.
// ===========================================================================
struct ItemColorEntry {
    std::optional<std::string> name;    // Name
    std::optional<std::string> color;   // Color - raw text (no parsing routine confirmed for this table, 3.1)
    std::optional<std::string> editor;  // _Editor - editor-only metadata, not resolved further
};
ItemColorEntry ParseItemColorEntry(const Node* row);
std::vector<ItemColorEntry> ParseItemsColorPoolTable(const Document& doc);

// ===========================================================================
// 3.2 npc_color_palette.xtbl - a three-tier nested RGB palette tree.
// [CONFIRMED - disassembly + empirical, spec 3.2]
//
//   Palette (Name, hashed)
//     -> Primary_Colors > Primary_Color[]    (R/G/B floats, x255 scale)
//          -> Secondary_Colors > Secondary_Color[]  (same 4-byte RGBA shape)
//               -> Tertiary_Colors > Tertiary_Color[]  (same 4-byte RGBA shape)
//
// "a colour-variation tree (e.g. a base gang/NPC colour that branches into
// acceptable secondary and tertiary accent shades)" (3.2) - each level owns
// its own nested wrapper, not a flat per-Palette list (3.2's ASCII tree).
// Each level's R/G/B is read raw here (Always<float>, as authored); the
// spec states the engine additionally bakes this into a 4-byte {R,G,B,0xff}
// cell scaled by a `255.0` constant (`DAT_012a2d88`) and forces alpha to
// 0xff (3.2) - that bake (and which direction the scale is applied, since
// the spec gives only "the 255.0 scale constant", not a worked example) is
// a load-time transform over the raw authored value, not modelled here; the
// nested count/array-pointer record shapes the spec also describes (the
// runtime 12-byte-stride records) are memory layout, not XML schema, and
// are likewise not modelled.
// ===========================================================================
struct TertiaryColor {
    Always<float> r, g, b;  // R, G, B (raw authored values; see banner above for the un-applied x255 bake)
};
struct SecondaryColor {
    Always<float> r, g, b;
    std::vector<TertiaryColor> tertiaryColors;  // Tertiary_Colors > Tertiary_Color[]
};
struct PrimaryColor {
    Always<float> r, g, b;
    std::vector<SecondaryColor> secondaryColors;  // Secondary_Colors > Secondary_Color[]
};
struct Palette {
    std::optional<std::string> name;             // Name (hashed at load)
    std::vector<PrimaryColor> primaryColors;      // Primary_Colors > Primary_Color[]
};
Palette ParsePalette(const Node* row);
std::vector<Palette> ParseNpcColorPaletteTable(const Document& doc);

}  // namespace sr3tables_customization
