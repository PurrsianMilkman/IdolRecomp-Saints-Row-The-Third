#pragma once

// sr3tables_progression - typed readers for the 26 progression/rules/
// world-state `.xtbl` tables of spec-tables-progression.md.
//
// This is the aggregator header; the structs and ParseXxx() declarations
// live in (grouped by spec section, each with its own banner comment citing
// the spec section it implements):
//   tables_core.h   - stats.xtbl (S2), achievements.xtbl (S3), unlockables.xtbl
//                      / patch_unlockables.xtbl / *_unlockables.xtbl (S4)
//   tables_world.h  - respect_levels.xtbl (S5), notoriety.xtbl (S6.1),
//                      notoriety_levels.xtbl (S6.2), notoriety_spawn.xtbl (S6.3),
//                      difficulty_levels.xtbl (S7), cheats.xtbl (S8),
//                      collectibles.xtbl (S9)
//   tables_rules.h  - store_discounts.xtbl, gameplay_constants.xtbl,
//                      gameplay_nags.xtbl + Gameplay_nag_globals.xtbl,
//                      mission_checkpoints.xtbl, mission_help.xtbl,
//                      spawn_info_categories/groups/ranks.xtbl, drunk_levels.xtbl,
//                      rank_reactions.xtbl, default_global.xtbl, tweak_table.xtbl,
//                      metered_sprint.xtbl, activity_types.xtbl (all S10.x)
//
// Built ONLY from spec-tables-progression.md and include/sr3xtbl/xtbl.h's
// accessors (the cleanroom foundation); nothing here was derived from the
// game executable, disassembly or decompiled code. See tables_core.h's
// banner for the Always<T>/std::optional<T> convention every struct here
// follows.

#include "sr3tables_progression/tables_core.h"
#include "sr3tables_progression/tables_rules.h"
#include "sr3tables_progression/tables_world.h"
