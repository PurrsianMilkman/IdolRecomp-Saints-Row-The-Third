// The per-character record types CharacterState holds (EnemyTargetRecord,
// MinimapIconRecord, ObjectIndicatorRecord), and the includes the
// CharacterState parts share.
//
// Split out of character_state.h (Team C refactor, 2026-10-17); every line
// below moved verbatim. The doc-comment conventions of character_state.h's
// own top note apply ("this header" = the engine-state header set).
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "sr3airuntime/combat_ai.h"
#include "sr3luahost/open_state.h"
#include "sr3luahost/world_position.h"

namespace sr3luahost {


// One `ai_add_enemy_target` (Sec3.9) threat/target record. RE-DERIVED
// 2026-10-07 (direct disassembly of `0x00a3d260`/`0x00a3d180`/
// `0x004dedb0`/`0x004f5dd0`/`0x008addb0`) - this struct's own fields
// previously modeled the PRE-re-derivation understanding (arg 4 and the
// sentinel match feeding the two flag bits directly); that reading is
// REFUTED by the current spec text, which confirms instead: both bits are
// sourced from ARG 3 (compared against the literals `0` and `2`), land on
// one of TWO DIFFERENT record offsets depending on which of two distinct
// record-management paths ran (an unconditional free-list "create," or a
// per-actor "fetch an EXISTING record, write only if found"), and arg 4's
// real effect is a wholly separate call (see
// `lastAiAddEnemyTargetArg4TargetName` below), never reaching this record
// at all. The spec itself does not resolve whether the real record lives
// on the ACTING character or is a free-standing global table keyed by
// object identity - this project's own choice (stated here, not scraped
// from anywhere): model it as a per-ACTOR map, keyed by the arg-3-derived
// integer (as a decimal string) when nonzero, else by the resolved target
// name.
struct EnemyTargetRecord {
    // int64 of arg 3 via 0x00ea2596: truncation toward zero (Sec4.1, settled
    // 2026-10-01; the earlier banker's/round-to-nearest readings are struck).
    int64_t priorityOrId = 0;
    // CONFIRMED 2026-10-07: both branches use the identical two facts (is
    // arg3 exactly 0; is arg3 exactly 2) to set what are, in both cases,
    // bits 0x02/0x04 respectively - just at two different record offsets
    // (`+0x30` on the create path, `+0x18` on the fetch path) this project
    // does not model as physically distinct fields, since nothing in
    // scope reads either offset back independently of this one record.
    bool bit02ArgIsZero = false; // arg3 == 0
    bool bit04ArgIsTwo = false;  // arg3 == 2
    // true: the unconditional free-list "create" path ran (the common
    // single-player case per this entry's own text); false: the per-actor
    // "fetch an existing record" path ran AND found one (the only way this
    // struct instance exists on that path at all - a miss writes nothing,
    // modeled by simply never inserting a record, see the stub's own
    // lookup-before-write logic).
    bool createdNewRecord = false;
};

// minimap_icon_add_do (spec-lua-api-behaviour.md Sec10.5) - one recorded
// "an icon-add was requested" call against a resolved object. The real
// engine's own body (Sec10.5, fully re-derived by adversarial review) does
// a genuinely bit-tested two-way dispatch (a direct single-target add call
// vs. a filtered-list broadcast, gated on a shared per-object-type
// descriptor table bit this project has no real data for) and resolves
// args 2/3 through a real name->id hash lookup this project also has no
// real table for - per this task's own brief, NONE of that dispatch
// selection or id-hash resolution is modeled here; only the call's own
// raw arguments are recorded, honestly, as "a request," not "the specific
// branch the real engine would have taken." iconType/group are stored as
// the raw strings actually passed (never hashed to an id - Sec10.5's own
// `0x00802f60` hash is OPEN as to its real algorithm), param4/flag5 as the
// raw numeric values after this project's own standard nil-gated-default
// extraction (Sec10.5: param4 defaults 0.0, flag5 defaults 3 - CONFIRMED
// defaults; the real bit-tested MEANING of flag5 is explicitly not
// modeled, see this struct's own top note).
struct MinimapIconRecord {
    std::string iconType;   // arg 2, mandatory
    std::string group;      // arg 3, optional, default "" (CONFIRMED default, Sec10.5)
    double param4 = 0.0;    // arg 4, optional, default 0.0 (CONFIRMED default, Sec10.5)
    int64_t flag5 = 3;      // arg 5, optional, default 3 (CONFIRMED default, Sec10.5) - real bit-tested dispatch meaning NOT modeled (see struct's own top note)
};

// object_indicator_add_do (spec-lua-api-behaviour.md Sec10.6) - one
// recorded "an indicator-add was requested" call against a resolved
// object. Same minimal-stand-in convention as MinimapIconRecord above:
// the real HUD dispatch chain (`0x008f96f0`/`0x008f50d0`, Sec10.6) is not
// modeled, only the raw arguments actually passed. arg2/arg3 are
// CONFIRMED mandatory with NO nil/absence gate (Sec10.6 - a genuine
// difference from every optional sibling argument in this cluster);
// arg4/arg5 are CONFIRMED optional with the standard nil-gated idiom,
// defaults 3 and 100.0 respectively (Sec10.6 - arg5's default is a named
// `.rdata` float constant, not a bare 0.0, unlike several sibling
// functions elsewhere in this document).
struct ObjectIndicatorRecord {
    int64_t arg2 = 0;    // mandatory, no nil-gate (CONFIRMED, Sec10.6)
    int64_t arg3 = 0;    // mandatory, no nil-gate (CONFIRMED, Sec10.6)
    int64_t arg4 = 3;    // optional, default 3 (CONFIRMED, Sec10.6)
    double arg5 = 100.0; // optional, default 100.0 (CONFIRMED, Sec10.6)
};

} // namespace sr3luahost
