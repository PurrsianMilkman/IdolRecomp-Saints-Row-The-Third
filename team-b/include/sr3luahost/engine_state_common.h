// Shared includes and forward declarations for EngineState (engine_state.h) and its
// per-subsystem slices (engine_state_<subsystem>.h). Moved verbatim out of engine_state.h
// (refactor step 3a, 2026-10-17) so each slice header is self-contained.
#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// sr3save::nameHash(): the engine's general lower-cased, seed-0, no-final-
// XOR reflected CRC-32 name hash (0x00d9e740 - spec-save-format.md
// Sec9.8/Sec6.1, table 0x01320DA0, CONFIRMED - empirical via spec-lua-api-
// behaviour.md Sec5.1's 7/7 real I/O vectors for the sibling raw-byte entry
// point 0x00d9e790 sharing the same table/algorithm). Reused by citation,
// not reimplemented - see spec-lua-bindings.md Sec15's own text on
// vint_object_find, which hashes its `name` argument via this exact
// routine before the parent/document-scoped lookup below. Header-only, no
// new link dependency (include/ is one shared tree - see this project's
// own established convention, every library already does this).
#include "sr3cutscene/camera_script.h"
#include "sr3luahost/engine_callbacks.h"
#include "sr3luahost/lua_c_api.h"
#include "sr3luahost/open_state.h"
#include "sr3player/control_state.h"
#include "sr3player/input.h"
#include "sr3save/save_crc.h"

// Per-domain engine-state structs, split out of this header 2026-10-08 as a
// zero-behavior-change structural move (every struct, field and doc comment
// moved verbatim). Each domain owns its own file so a merge can only ever
// land a new field in that domain's file: with every struct in this one
// header, merges kept landing one struct's new fields inside a DIFFERENT,
// adjacent struct whose closing brace sat next to it. Add new fields to the
// struct's own header below, never here. They are all included ahead of
// EngineState, so a struct an EngineState doc comment calls "above" is
// still declared before EngineState - in the header listed here for it.
#include "sr3luahost/world_position.h"            // WorldPosition
#include "sr3luahost/character_state.h"           // CharacterState, EnemyTargetRecord, MinimapIconRecord, ObjectIndicatorRecord
#include "sr3luahost/vehicle_state.h"             // VehicleState
#include "sr3luahost/placed_object_state.h"       // PlacedObjectState
#include "sr3luahost/hud_state.h"                 // kHudBarSlotCount/kHudTimerSlotCount, HudBarSlot, HudTimerSlot, FakeNotorietySlot, HudHardeningCounters, HudDisplayStatePool, QteSlotEntry
#include "sr3luahost/pcr_customization_state.h"   // PcrCustomizationItem, PcrCustomizationState
#include "sr3luahost/vint_state.h"                // VdoObject, LoadedVintDocument, VintTaggedValue, VintStoredProperty, VintPropertyStats, VintDataItem, VintDataResponderRecord
#include "sr3luahost/zone_state.h"                // ZoneMember, PauseMapHoveredZone
#include "sr3luahost/zone_object_resolver.h"       // ZoneObjectResolver - real name<->sr3zone::ObjectRecord link (2026-10-08)
#include "sr3luahost/peg_load_state.h"            // PegLoadFilenameEntry, PegLoadRequest
#include "sr3luahost/misc_state.h"                // ObjectiveTextSlot, TeleportRequest, ConversationPlay
#include "sr3luahost/save_game_state.h"           // CollectedCollectible, SaveGameState (spec-save-format.md Sec15 wiring, 2026-10-08)

// sr3tables_progression::RespectLevel: the real respect_levels.xtbl per-level
// record type (spec-tables-progression.md Sec5.2), reused directly below as
// the live runtime's level-up cost/unlock lookup (Sec5.4: "0x0060eb60 is not
// a separate algorithm ... it is the exact same accessor" as the save-shaped
// path's own cost table) - NOT a second, parallel cost struct. Only the
// struct's DEFINITION is used here (a plain data record: Always<int32_t>,
// optional<string>, vector<string> - all header-only, sr3xtbl/xtbl.h pulls
// in no out-of-line symbol this header calls), so this remains header-only:
// sr3luahost does not gain a new link dependency on sr3tables_progression,
// and this subsystem never reads a real respect_levels.xtbl file (reading
// real xtbl content is a separate concern, same footing as this task's own
// collectibles.xtbl Respect_Award fields being out of scope) - tests supply
// the per-level records directly via respectLevelTable().
#include "sr3tables_progression/tables_world.h"   // sr3tables_progression::RespectLevel

// Same header-only footing as tables_world.h just above: only the
// Unlockable/UnlockableAutoUnlock struct/enum DEFINITIONS are used here
// (plain data: std::string, enum, optional, vector - no out-of-line symbol
// this header calls), so grantNewGameAutoUnlocks() below adds no new link
// dependency on sr3tables_progression - callers that want to parse a real
// unlockables.xtbl still go through ParseUnlockablesTable() themselves and
// hand the result in.
#include "sr3tables_progression/tables_core.h"    // sr3tables_progression::Unlockable, UnlockableAutoUnlock

namespace sr3airuntime {
struct NpcAiTables;     // include/sr3airuntime/npc_ai_runtime.h - EngineState::enableNpcAi
struct NpcAiConfig;
class NpcAiRuntime;
}

namespace sr3vintdoc {
struct Document; // include/sr3vintdoc/vint_doc.h - EngineState::loadVintDocument
}

namespace sr3luahost {

// Forward-declared only (full definition: bare_globals.h) so this header
// does not cross-include it - pushGamePauseReason()/popGamePauseReason()
// below are implemented out-of-line in src/lua_menu_runtime.cpp, which
// does include it.
class BareThreadTable;

// The aggregate every engine_state_<subsystem>.h slice is a base of (engine_state.h).
class EngineState;

} // namespace sr3luahost
