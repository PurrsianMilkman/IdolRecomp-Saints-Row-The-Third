#pragma once

// sr3airuntime/notoriety.h - the real notoriety (wanted-level) tracking
// MECHANISM: spec-ai-runtime.md Sec19 ("Notoriety (wanted-level) tracking
// and police-response dispatch", 2026-10-08/2026-10-09), read alongside
// spec-tables-progression.md Sec6/Sec14.6 for the real shipped
// notoriety_levels.xtbl numbers this file ports directly.
//
// SCOPE (per this task's own brief): the team-relationship hostility flip at
// the zero boundary (Sec19.4), the crimes-to-points and decay rules
// (Sec19.2/19.3), the clear-to-zero roster/timer sequence (Sec19.4,
// corrected 2026-10-09), and the heat-code table's TRACK-indexing
// (Sec19.4's "two further corrections"). This file does NOT model
// police/roadblock SPAWNING (that is include/sr3airuntime/
// police_roadblock_spawn.h, Sec19.7 - a separate, sibling per-frame system -
// see "THE REMAINING OPEN GATE" note below for exactly how the two relate).
//
// THIS IS NEW GROUND, not a patch to existing code. A repo-wide grep for
// "heat_code"/"heatcode"/"heat code" before writing this file found zero
// hits anywhere outside this very spec citation - no existing code in this
// project assumed level-indexing for the heat-code table, so there was
// nothing to correct elsewhere; the correction below exists purely so a
// FUTURE caller of this file doesn't reintroduce that mistake (see the
// kHeatCodeByTrack regression test, tests/synthetic_notoriety_test.cpp).
//
// CONFIRMED vs CHOSEN vs OPEN, same confidence key spec-ai-runtime.md's own
// top-of-file "Confidence key" paragraph defines, and the same discipline
// police_roadblock_spawn.h/ambient_population.h already apply:
//
// CONFIRMED (Sec19.1, the live record): a 4-slot array, one per TRACK -
// **0 = Luchadores, 1 = Deckers, 2 = Morningstar, 3 = Police** - each slot
// {level: s32, fraction: float}, the fraction banked toward the next level
// against that level's own NotorietyLevelLimit entry; crossing the threshold
// rolls the integer level up or down. NotorietyTrack/NotorietyTrackRecord
// below. Record fields +0x08/+0x0c, +0x14, +0x18 bit 0 and the min/max bound
// stacks (+0x1c/+0x34, +0x28/+0x40) are confirmed to exist but their exact
// roles (bound stacks excepted, whose role Sec19.1 DOES name) are OPEN - not
// modeled here, out of this task's own scope (hostility flip / points-decay
// / clear-to-zero / heat-code, not notoriety_set_min/_max's bound-stack
// clamping).
//
// CONFIRMED (Sec19.2, what raises it): the crime-report reader (`0x006076a0`)
// selects the GANG or POLICE sub-block of the activity record (notoriety.xtbl,
// spec-tables-progression.md Sec6.1/Sec14.6 - note there is only ONE "Gang"
// sub-block per activity, shared by all 3 gang tracks; no per-gang-track
// distinction exists in the table itself), checks the current level is
// within that sub-block's own Min_Level/Max_Level window, and - when
// Check_Detection is set - additionally requires a live player to currently
// be detecting the actor. Points flow through the add-points primitive
// (`0x00606f40`): an early-out respects the "gain suppressed" flag
// (`0x014A4C0D`); otherwise the delta is added to the fraction and a
// roll-over loop advances the level while the fraction exceeds each
// successive level's own point threshold, dispatching a level-change on any
// resulting change. TryAwardCrimePoints()/AddPointsRaw() below.
//
// CONFIRMED (Sec19.3, what lowers it): a genuine decay tick (`0x00607200`),
// timer-gated on the per-track decay-interval timer (record +0x10) via the
// SAME countdown-timer-expiry primitive (`0x00d9e1e0`) spec-ai-runtime.md
// Sec17 Sec4.4 already documents as the shared throttle underneath ambient
// population's own ~1s cadence (common.h's EngineTimer models that exact
// primitive family already - reused here, not reinvented). On expiry it
// computes a decay amount and rolls the level down in the same manner as the
// crime roll-up, in reverse. **Resolves spec-tables-progression.md Sec6.2's
// own flagged OPEN item**: Set 0 (`0x014A45C4`) = Gang (tracks 0-2), Set 1
// (`0x014A45DC`) = Police (track 3) - a live decay-rate reader branches
// explicitly on `track==3`. kGangLevelTable/kPoliceLevelTable below port the
// REAL shipped values (spec-tables-progression.md Sec14.6, empirical from
// notoriety_levels.xtbl) for both NotorietyLevelLimit and NotorietyDecayRate.
// **No distance-based "lost the cops" mechanic exists** - Sec19.3's own
// explicit search, not a skip: purely timer/table-driven, not modeled here
// either (nothing to model - a confirmed negative).
//
// CHOSEN (the one real ambiguity in Sec19.3's own text, flagged honestly
// rather than resolved by invention): Sec19.3's prose literally reads "it
// computes a decay amount from the same `NotorietyLevelLimit` table (scaled
// by a constant at `0x01123400`, value not read)" - naming the LIMIT table,
// not the separately-named NotorietyDecayRate field, with an unread scaling
// constant. Inventing that constant's value would misrepresent an OPEN fact
// as CONFIRMED. This file's CHOSEN reading instead uses the xtbl's own
// NotorietyDecayRate[track][level] value directly as the per-tick decay
// amount (the field's own name is literally "decay rate", and this is the
// natural engineering interpretation when the literal scaling constant is
// unavailable) - real shipped numbers, not fabricated ones, just one
// un-re-derived step in how the runtime actually combines them. Likewise
// CHOSEN: the recurring (not just first) decay-tick interval reuses
// NotorietyFirstDecay[track][level] (no separate "recurring interval" field
// is named anywhere in the xtbl or Sec19.3's text) - a reasonable default
// pending a pass that re-derives the real recurring cadence.
//
// CONFIRMED (Sec19.4, the shared level-change dispatcher `0x00606b80`,
// corrected 2026-10-09 via full re-disassembly after an earlier, wrong
// "runs unconditionally every call" reading was struck in place) - EVERY
// real path that changes a track's level (notoriety_set, notoriety_reset,
// organic crime/decay) funnels through this one dispatcher:
//
//   0x00606b80(newLevel, track):
//       heatCode = heatCodeTable[track]   // TRACK-indexed, not level - see below
//       call 0x004f6540(heatCode)          // AI known-hostiles state-rewrite, UNCONDITIONAL, both directions
//       if newLevel > 0:
//           record[track].+0xc = -1        // OPEN role, not modeled
//           call 0x009b7130(0, heatCode, 0, 1)   // team-relation write: 0 = HOSTILE
//           return                          // NO roster walk, NO player dispatch, NO timer rearm
//       else (newLevel <= 0):
//           call 0x009b7130(0, heatCode, 1, 1)   // team-relation write: 1 = UNFRIENDLY
//           [roster walk, player dispatch, timer rearm]
//
// **The genuine, previously-undocumented finding**: `0x00606b80` is a FOURTH
// writer of the already-documented team-relationship matrix (spec-ai-
// runtime.md Sec12.7, `0x0262d940`), via the SAME write primitive
// (`0x009b7130`) the Lua natives `team_make_allies`/`_unfriendly`/`_hostile`
// use (spec-lua-api-behaviour.md Sec20.6/Sec6.7-family for `_unfriendly`/
// Sec6.7-family for `_hostile`) - writing value 0 (= `_hostile`'s own write
// value) or 1 (= `_unfriendly`'s own write value, "a stand-down, not a
// return to full neutrality"), symmetrically (`[a][b]` AND `[b][a]`, the
// trailing `true`/`1` argument), from team 0 ("the player/civilian baseline
// - the one team value the roster-loop switch doesn't map to any gang/
// police track") against the heat-coded team slot. **THE ZERO BOUNDARY,
// stated precisely**: any level >= 1 on a track flips that track's
// relationship cell to HOSTILE; any level <= 0 flips it to UNFRIENDLY (not
// neutral) - a binary trigger on the SIGN of the level, the specific
// magnitude 1-5 is never read by this function. This runs on EVERY level
// change in EITHER direction (the matrix write call appears in BOTH
// branches above, just with a different value) - what differs by direction
// is ONLY whether the roster/player/timer sequence additionally runs.
//
// **THE CLEAR-TO-ZERO ROSTER/TIMER SEQUENCE, confirmed to run ONLY on the
// `newLevel <= 0` branch** (Sec19.4, 2026-10-09 correction - an EARLIER,
// struck reading had wrongly described this as unconditional): (2) walks the
// entire live AI-human roster and, for each human on the affected track,
// re-checks eligibility and calls `0x008addb0(human, 0)` - CONFIRMED (full
// disassembly) to be a generic "single-gate numeric-mode liveness wrapper"
// (spec-lua-api-behaviour.md Sec3.6/Sec7.33's own already-documented
// variant-2 thin wrapper over `0x00884760`), reused across dozens of
// unrelated systems - i.e. a spawn-SUPPRESSION-eligibility RECONSIDERATION
// pass over ALREADY-EXISTING pursuers, NOT a creation/spawn call (the roster
// loop has no allocation call in it either way); (3) walks up to 2 (co-op)
// player pointers and dispatches each through a 10-case jump table to one of
// 3 player-state handlers - ALL THREE confirmed to be handle-RELEASE shapes
// ("resolve a handle, check class/liveness, release it"), not spawn calls
// (Handler 1 releases a vehicle/slot handle pair gated on the Traffic
// zone-data singleton, shared across 46 callers project-wide; Handler 2 is a
// trivial 2-field setter; Handler 3, `0x00606e39`, opened 2026-10-08, is a
// third instance of the same release shape); (4) re-arms a 2000ms per-track
// timer via the same `0x00d9e140` primitive family common.h's EngineTimer
// already models (the 500ms decay-timer-slot rearm belongs to
// notoriety_reset, Sec19.1, a DIFFERENT timer - the removed driver modelled it).
// **"Ordinary decay" that steps a track DOWN but stays above zero (e.g.
// level 3 -> level 2) takes the newLevel > 0 cheap branch - it re-affirms
// HOSTILE on the matrix and returns; it does NOT run the roster/timer
// sequence.** Only a transition that actually reaches level <= 0 does.
// (the removed NotorietyDriver modelled this; no class here implements it now).
//
// **Two further corrections to the heat-code step itself (Sec19.4,
// 2026-10-09).** First, the 4-byte table at `0x011286c4` (values `1, 2, 3,
// 5`) is indexed by the function's own TRACK argument, **not by level** -
// proven by the same index value being reused immediately after to address
// the live per-track notoriety-record array itself (unambiguously
// track-indexed). So the codes are **Luchadores=1, Deckers=2,
// Morningstar=3, Police=5** - a track->code mapping. kHeatCodeByTrack/
// HeatCodeForTrack() below. Second, `0x004f6540` is NOT "a global heat/
// difficulty setter" (an earlier, struck reading) - full disassembly shows
// it is a generic AI known-hostiles STATE-REWRITE pass over every live
// human's own threat-tracking lists, unrelated to spawning, run
// unconditionally on every level change regardless of direction - modeled
// here only as the injectable `onKnownHostilesStateRewrite` seam (its own
// internals - which two linked lists, which state-byte values - are OPEN
// per this task's own scope, not re-derived further here).
//
// THE REMAINING OPEN GATE - READ BEFORE WIRING THIS MODULE UP TO ANYTHING.
// Sec19.4's own net verdict (re-confirmed by Sec19.7's independent trace):
// "police response is CONFIRMED structurally separate from the ambient
// distant/LOD population system - no call-graph edge connects 0x00606b80's
// chain to either the ambient population ticks or the REAL police/roadblock
// SPAWN tick (0x00930e20, include/sr3airuntime/police_roadblock_spawn.h)."
// This file's hostility flip is the real, CONFIRMED mechanism for making
// EXISTING nearby police/gang NPCs actually hostile at notoriety > 0 - but
// it is explicitly NOT the roadblock/vehicle SPAWN trigger. Handler 3
// (`0x00606e39`) - the one remaining unopened handler as of Sec19.4's
// earlier text - was opened this spec pass and confirmed to be a fourth
// instance of the generic handle-release shape, NOT a spawn call either;
// `0x00606b80`'s own chain is now exhaustively characterized end to end and
// none of it creates anything. **This CLOSES, not just narrows, the
// question of whether THIS dispatcher is secretly the roadblock-spawn
// trigger - it is not.** police_roadblock_spawn.h's own "THE HONEST
// NOTORIETY GATE" comment (its `armed` bool, explicitly NOT wired to any
// notoriety state) is therefore still correct to leave that bool
// caller-set: this file does not close that link either, and should not -
// wiring it would still be inventing a fact neither Sec19.4 nor Sec19.7
// confirms exists in the traced code. The genuinely OPEN remainder (per
// Sec19.5/19.6/19.7, unchanged by this file) is who WRITES a
// `"roadblock"`-class instance's own slot table in the first place - a
// question this module's own confirmed scope (hostility flip, points/decay,
// clear-to-zero sequence, heat-code-by-track) does not touch or answer.

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "sr3airuntime/common.h"

namespace sr3airuntime {

// ===========================================================================
// Sec19.1 CONFIRMED: the 4 tracks, in the live record's own slot order.
// Matches spec-tables-progression.md Sec4.6/Sec6.4's independently-cited
// "luchadores 0, deckers 1, morningstar 2, police 3" multiplier mapping.
// ===========================================================================
enum class NotorietyTrack : int32_t {
    kLuchadores = 0,
    kDeckers = 1,
    kMorningstar = 2,
    kPolice = 3,
};
inline constexpr int32_t kNotorietyTrackCount = 4;
inline constexpr int32_t kNotorietyMaxLevel = 5;  // Sec19.1: max-bound getter (0x00605640) defaults to 5 unconstrained

// ===========================================================================
// Sec19.4 CONFIRMED, corrected 2026-10-09: the heat-code table, indexed by
// TRACK, not level. Luchadores=1, Deckers=2, Morningstar=3, Police=5.
// ===========================================================================
inline constexpr std::array<int32_t, kNotorietyTrackCount> kHeatCodeByTrack = {
    1,  // Luchadores
    2,  // Deckers
    3,  // Morningstar
    5,  // Police
};

inline int32_t HeatCodeForTrack(NotorietyTrack track) {
    return kHeatCodeByTrack[static_cast<size_t>(track)];
}

// ===========================================================================
// Sec12.7/Sec20.6-family CONFIRMED team-relationship write values - matches
// team_make_hostile (writes 0) / team_make_unfriendly (writes 1) exactly,
// and perception.h's own independently-documented TeamRelationship() read
// scale ("0 hostile, 1 unfriendly, 4 allied"). A plain local alias, not a
// perception.h include - this module stays decoupled the same way
// police_roadblock_spawn.h does, and the matrix's own real VA is unmapped
// either way (callers supply their own 8x8 instance, exactly as perception.h
// already does for its read-side TeamRelationship() helper).
// ===========================================================================
using TeamRelationMatrix = std::array<std::array<uint8_t, 8>, 8>;
inline constexpr uint8_t kTeamRelationHostile = 0;     // team_make_hostile's own write value
inline constexpr uint8_t kTeamRelationUnfriendly = 1;  // team_make_unfriendly's own write value - "a stand-down, not a return to full neutrality"
inline constexpr uint8_t kTeamRelationNeutral = 2;     // the read accessor's own id==9 sentinel default (Sec12.7) - never WRITTEN by this dispatcher
inline constexpr uint8_t kTeamRelationAllied = 4;      // team_make_allies's own write value - never written by this dispatcher either

// Sec19.4: "team 0 (the player/civilian baseline - the one team value the
// roster-loop switch doesn't map to any gang/police track)".
inline constexpr int32_t kPlayerBaselineTeam = 0;

// ===========================================================================
// Sec19.1/Sec19.3 CONFIRMED (empirical, spec-tables-progression.md Sec6.2/
// Sec14.6): the real shipped notoriety_levels.xtbl numbers, one set per
// gang/police split. Level 0's "limit" is stored as a -1 sentinel in the
// real file/array (the limit is never WRITTEN for NotorietyLevel 0) - this
// table stores 0 instead, the value actually needed for the span math below
// (Sec14.6's own note: this is purely what "keeps set 1 level 0 from
// overwriting set 0 level 5" in the real flat array - a storage-layout
// artifact, not a point value).
// ===========================================================================
struct NotorietyLevelTable {
    std::array<int32_t, 6> levelLimit;    // CONFIRMED (cumulative points threshold to BE at this level)
    std::array<int32_t, 6> decayRate;     // CONFIRMED real xtbl field; CHOSEN as the per-tick decay amount (see file header)
    std::array<int32_t, 6> firstDecayMs;  // CONFIRMED real xtbl field (seconds x1000); CHOSEN reused as the recurring interval too
};

// Set 0 = Gang (tracks 0-2). Sec14.6 real values.
inline constexpr NotorietyLevelTable kGangLevelTable = {
    /*levelLimit=*/{0, 75, 225, 500, 1200, 2500},
    /*decayRate=*/{1, 5, 10, 20, 40, 50},
    /*firstDecayMs=*/{15000, 15000, 15000, 15000, 15000, 60000},
};

// Set 1 = Police (track 3). Sec14.6 real values - resolves spec-tables-
// progression.md Sec6.2's own flagged OPEN item (which set is which).
inline constexpr NotorietyLevelTable kPoliceLevelTable = {
    /*levelLimit=*/{0, 75, 300, 700, 1200, 2500},
    /*decayRate=*/{1, 6, 15, 25, 50, 75},
    /*firstDecayMs=*/{15000, 15000, 15000, 15000, 15000, 60000},
};

inline const NotorietyLevelTable& LevelTableForTrack(NotorietyTrack track) {
    return track == NotorietyTrack::kPolice ? kPoliceLevelTable : kGangLevelTable;
}

// The points needed to advance from `level` to `level+1` (the cumulative
// table's own adjacent difference) - level must be in [0, 4].
inline int32_t NotorietyLevelSpan(const NotorietyLevelTable& table, int32_t level) {
    return table.levelLimit[static_cast<size_t>(level) + 1] - table.levelLimit[static_cast<size_t>(level)];
}

// ===========================================================================
// Sec19.2 CONFIRMED: one notoriety.xtbl activity record's two sub-blocks.
// There is only ONE "Gang" sub-block per activity (spec-tables-progression.md
// Sec6.1) - all 3 gang tracks share it; there is no per-specific-gang-track
// distinction anywhere in the table.
// ===========================================================================
struct CrimeActivitySubBlock {
    int32_t points = 0;
    int32_t minLevel = 0;
    int32_t maxLevel = kNotorietyMaxLevel;
    bool checkDetection = false;
};

struct CrimeActivity {
    std::string name;
    int32_t delayMs = 0;  // Sec6.1: shared by both sub-blocks; OPEN exact runtime role beyond the loader-side field, not consumed below
    CrimeActivitySubBlock gang;
    CrimeActivitySubBlock police;
};

// Real named activities, spec-tables-progression.md Sec14.6 (empirical from
// notoriety.xtbl). Numbers explicitly called out in that section are
// CONFIRMED; the OTHER track's sub-block for a single-track activity is not
// independently quoted there - CHOSEN 0 points / the default [0,5] window
// for that inactive side, flagged per-field below.
inline const CrimeActivity kConfirmedArmoredTruckAssault = {
    "armored truck assault",
    500,
    /*gang=*/{0, 0, kNotorietyMaxLevel, false},    // CHOSEN: police-only per Sec14.6, gang side not separately quoted
    /*police=*/{100, 2, 3, false},                 // CONFIRMED: 100 points, window levels 2-3
};
inline const CrimeActivity kConfirmedHelicopterDestroy = {
    "helicopter destroy",
    500,
    /*gang=*/{0, 0, kNotorietyMaxLevel, false},    // CHOSEN
    /*police=*/{500, 4, 5, false},                 // CONFIRMED: 500 points, window levels 4-5
};
inline const CrimeActivity kConfirmedGovtHeliTheft = {
    "govt heli theft",
    500,
    /*gang=*/{0, 0, kNotorietyMaxLevel, false},    // CHOSEN
    /*police=*/{250, 4, 5, false},                 // CONFIRMED: 250 points, window levels 4-5
};
inline const CrimeActivity kConfirmedGangAlert = {
    "gang alert",
    500,
    /*gang=*/{75, 0, kNotorietyMaxLevel, false},   // CONFIRMED: 75 points (gang-only)
    /*police=*/{0, 0, kNotorietyMaxLevel, false},  // CHOSEN: gang-only per Sec14.6, police side not separately quoted
};
// The only negative-point activity in the whole 30-row table (Sec6.1/14.6) -
// entering an OWNED store REDUCES notoriety, both tracks.
inline const CrimeActivity kConfirmedEnterOwnedStore = {
    "enter owned store",
    0,                                              // CONFIRMED: Delay 0ms (one of exactly 2/30 activities with Delay=0)
    /*gang=*/{-20, 0, kNotorietyMaxLevel, false},   // CONFIRMED points value; window CHOSEN default (not separately quoted)
    /*police=*/{-20, 0, kNotorietyMaxLevel, false}, // CONFIRMED points value; window CHOSEN default
};

// NOTE: the former NotorietyDriver (AddPointsRaw/DecayTick/Reset) and its
// track/alert/dispatch records were removed 2026-10-18: they contradicted
// Sec19.8 FINAL. The gain/decay recipe lives in notoriety_escalation.h
// (NotorietyEscalation), which reuses the tables and CrimeActivity records above.

}  // namespace sr3airuntime
