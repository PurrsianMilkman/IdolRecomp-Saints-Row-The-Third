#pragma once

// sr3player/movement_tweak_table.h - seeds MoveTweaks (movement.h) from the
// real tweak_table.xtbl rows, through this project's EXISTING reader
// (sr3tables_progression::ParseTweakTable, spec-tables-progression.md
// Sec10.10). Header-only on purpose: sr3player itself stays free of the
// sr3xtbl/sr3tables_progression dependency; a caller that includes this
// header links sr3tables_progression.
//
// The engine mechanism being reproduced (spec-tables-progression.md
// Sec10.10, Sec1.4, Sec14.15): the movement code never reads the table at
// run time - it reads compiled-in globals (0x0262e9e0, 0x0140e64c, ...). At
// LOAD time each row's Name is matched case-insensitively against the
// 586-name registry and the row's Value overwrites that global. A row is
// accepted only if its Framework (default "main") equals, case-
// insensitively, the framework being loaded; the base load passes "main",
// each mounted DLC bundle its own name, later loads overwriting earlier.
//
// FALLBACK. A tunable that no accepted row names keeps its compiled-in
// static value - which no spec in this repository publishes (the registry
// gives names, types and destinations only). Such a tunable therefore
// stays OPEN here instead of receiving a made-up default. A row whose
// <Value> is absent is skipped for the same reason (the reader's 0 is not
// the engine's value).

#include <cctype>
#include <string>
#include <utility>
#include <vector>

#include "sr3player/movement.h"
#include "sr3tables_progression/tables_rules.h"

namespace sr3player {

// Applies one load of tweak_table.xtbl (base: framework "main"; a DLC
// overlay: that bundle's framework name). Call once per load, in load
// order. Returns how many MoveTweaks fields this load (re)seeded.
inline int SeedMoveTweaksFromTweakTable(MoveTweaks& tweaks,
                                        const std::vector<sr3tables_progression::TweakTableEntry>& rows,
                                        const std::string& framework = "main") {
    const auto iequals = [](const std::string& a, const std::string& b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i)
            if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
                return false;
        return true;
    };
    std::vector<std::pair<std::string, double>> accepted;
    for (const auto& row : rows) {
        const std::string rowFramework = row.framework ? *row.framework : std::string("main");
        if (!iequals(rowFramework, framework)) continue;
        if (!row.value.present) continue;
        // CHOSEN: applied as a float tunable (registry type 1, stored as
        // the float the reader produced). The per-name registry TYPE lives
        // in tools/ai_tweak_registry.tsv, which is not in this repository,
        // and no spec here states it for these names; an int-typed
        // registration (type 2) would store int(Value) truncated instead
        // (spec-tables-progression.md Sec10.10).
        accepted.emplace_back(row.name, static_cast<double>(row.value.value));
    }
    return tweaks.seedFromTweakRows(accepted);
}

}  // namespace sr3player
