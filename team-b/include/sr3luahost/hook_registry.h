// IMPLEMENTED PRE-REVIEW, PENDING CLEARANCE (manager rule 2026-09-30): the
// spec-lua-api-behaviour.md / spec-lua-bindings.md sections this file rests on
// are marked "NOT yet cleared for implementation" by the 2026-09-30 desk
// review (115/122 and 57/58 review-status lines). Kept working, behaviour
// unchanged, until Team A clears them; no new behaviour from those sections
// before then (team-b/HANDOFF.md section A, standing rule).
//
// The confirmed engine->Lua hook name census this project fires as real
// calls, built ONLY from spec-lua-bindings.md Sec8.2-8.4 and Sec12.2-12.9
// (read in full before writing this file - not worked from a summary).
// This closes the gap sr3luahost's original scaffold (host.h's own top
// comment, HANDOFF.md Sec9.113) left open: lua_pcall on a script's TOP
// LEVEL only runs its top-level statements, so a script that is mostly
// `function some_hook_name() ... end` definitions never gets its own real
// logic exercised. The real engine calls INTO those functions later, by
// name, at real gameplay trigger points this project cannot observe
// directly (clean-room: no disassembly here) - but Team A's spec already
// confirms WHICH names and WHICH mechanism, so this project can fire them.
//
// Three groups, all from the same spec, each with a DIFFERENT confidence
// level - kept distinct here rather than flattened into one list, because
// the task this was built for is explicit that conflating them would
// misrepresent the evidence:
//
// - Group 1 (73 names, spec Sec12.3, #1-73): CONFIRMED by disassembly -
//   every one is a literal string argument observed at a real call site to
//   the confirmed dispatcher (FUN_00e0cef0). The real, confirmed mechanism
//   (spec Sec8.3): the engine checks the global exists AND
//   type(x)=="function" first (a SILENT existence check - no error if
//   absent) and only calls it if that check passes. Implemented here as
//   lua_getglobal + lua_isfunction, call only if true.
//
// - Group 2 (2 names, spec Sec12.4, #74-75): CONFIRMED by disassembly, but
//   with a documented ASYMMETRY versus Group 1 - the real engine calls
//   these two UNCONDITIONALLY, with NO existence check first
//   (FUN_00e0de80's own decompiled body has no _GetAnyGlobalSilent guard,
//   unlike every Group-1 caller) - meaning the real engine assumes the Lua
//   side always defines them. This project still runs the existence check
//   first, but ONLY for honest measurement ("defined vs not") - it does
//   NOT gate the call, matching the real engine's own confirmed behavior.
//   A script that doesn't define one of these will, in this project's own
//   run, get a real Lua "attempt to call a nil value" error recorded for
//   it - an honest simulation of "presumably errors at the real call
//   site" (spec Sec12.4's own words), not a fabricated one.
//
// - Group 3 (58 names, spec Sec12.6, #76-133): the spec's own stated
//   confidence level is EXPLICITLY LOWER than Group 1/2 - these are
//   HIGH CONFIDENCE by naming-convention pattern (`m<mission>_<name>`
//   string presence in .rdata/.data), NOT individually re-confirmed
//   dispatcher call sites: only 2 of the underlying 60 mission-numbered
//   strings (m24_killbane_on_death/_on_undowned) were individually traced
//   to a real dispatcher call site, and those 2 are already counted in
//   Group 1 (#72-73), not duplicated here. Fired the same existence-gated
//   way as Group 1 (spec Sec12.6 gives no reason to think the mechanism
//   differs), but tagged distinctly everywhere this project reports on
//   them so nobody downstream mistakes this for Group 1's confirmation
//   strength.
//
// Explicitly NOT implemented as concrete calls, per the spec's own text:
// - Group 4 (spec Sec8.4, 7 sprintf-templated per-widget-instance name
//   families, e.g. "%s_reset") - unbounded: a real name only exists per
//   live, named UI widget instance, and no static list of instance names
//   exists to fire.
// - Group 5 (spec Sec8.5, 2 data-driven runtime-populated tables,
//   DAT_026e83f0/DAT_02319570) - both read as entirely null in the static
//   executable image (spec's own checked-negative population figures,
//   0/80 and 0/4 non-null); their real per-mission contents are
//   OPEN/UNKNOWN from static analysis, so there is nothing to fire.
//
// SAME-DAY FOLLOW-UP (2026-09-29), two independent additions, both folded
// into this originally-133-entry table without disturbing the 133 above:
//
// 1. **5 more Group-1 names from spec-lua-bindings.md Sec14.6**
//    (`store_weapon_uncover_weapon`, `store_gallery_upload_complete`,
//    `store_gallery_download_list_complete`,
//    `screen_capture_open_preview_dialog`, `dialog_build`) - found as a
//    side effect of Sec14.6's own exhaustive negative search for a
//    mission-lifecycle sprintf-hook family; each is CONFIRMED via the
//    same real dispatcher call-site mechanism as every other Group 1
//    name (Sec14.6's own text gives no reason to sort any of the 5 into
//    Group 2/3), so they are appended as ordinary Group 1 entries, not
//    specially treated. Total 133+5 = 138 - see confirmedHooks()'s own
//    comment; nowhere in this codebase hardcodes "138" either, for the
//    same reason "133" was never hardcoded as a computed value (only ever
//    printed via hooks.size()/similar) - see tools/lua_host_run.cpp.
//
// 2. **Real per-hook argument contracts for 6 of the above hooks, spec
//    Sec14** - HookArg/HookSpec::args below.
#pragma once

#include <string>
#include <vector>

namespace sr3luahost {

enum class HookGroup {
    Group1_GeneralNamed,                 // spec Sec12.3, #1-73, existence-gated, CONFIRMED
    Group2_LifecycleUnconditional,       // spec Sec12.4, #74-75, UNCONDITIONAL real call, CONFIRMED
    Group3_MissionNumberedPatternBased,  // spec Sec12.6, #76-133, existence-gated, HIGH CONFIDENCE / pattern-based, NOT individually dispatcher-confirmed
};

// Short, stable machine label (used as a TSV column value).
const char* hookGroupLabel(HookGroup g);

// One-line human-readable confidence/mechanism note (used in the verdict
// tool's own printed summary) - states the real distinction spelled out
// in this file's own top comment, so the printed report carries the same
// honesty the code does.
const char* hookGroupNote(HookGroup g);

// Real per-hook argument contracts (spec-lua-bindings.md Sec14, folded in
// 2026-09-29 - the "future pass" this file's own earlier comment
// anticipated). A small tagged union rather than a bare std::vector<string>
// (this file's original shape): one hook's confirmed argument needs a
// value only known at FIRE TIME, not at table-construction time (the
// script's own filename, Sec14.1 - a per-call context value, not a static
// literal), so a plain literal-value list cannot represent every real
// contract this pass folds in. Every OTHER hook not mentioned in
// lua_hook_registry.cpp's own per-hook comments keeps an EMPTY args list,
// UNCHANGED from before this pass - this remains the default, not
// something every hook opts into.
enum class HookArgKind {
    LiteralNumber,       // push a fixed double, known and stated at table-build time
    LiteralString,       // push a fixed string, known and stated at table-build time
    ScriptFilenameString // push the CALLING script's own canonicalized filename
                         // (Sec14.1: "the canonicalized script filename" -
                         // this project's own real, on-hand entryName for
                         // whichever script is currently being fired
                         // against, threaded through via
                         // Host::fireConfirmedHooks's new `scriptFilename`
                         // parameter - see host.h). Not a literal: the
                         // real value differs per script, by design.
};

struct HookArg {
    HookArgKind kind;
    double numberValue = 0.0;   // valid iff kind == LiteralNumber
    std::string stringValue;    // valid iff kind == LiteralString

    static HookArg number(double v) {
        HookArg a; a.kind = HookArgKind::LiteralNumber; a.numberValue = v; return a;
    }
    static HookArg literalString(std::string v) {
        HookArg a; a.kind = HookArgKind::LiteralString; a.stringValue = std::move(v); return a;
    }
    static HookArg scriptFilename() {
        HookArg a; a.kind = HookArgKind::ScriptFilenameString; return a;
    }
};

// One confirmed (or pattern-based) hook name this project fires against a
// loaded script's Lua state.
// How firmly a Group 1 name is established as a real Lua hook
// (spec-lua-bindings.md desk review, 2026-09-30, on §4). Every hook is still
// fired exactly as before; this label only says how far to trust a result
// that depends on it. Groups 2/3 carry their own group-level status.
enum class HookEvidence {
    Sec8Confirmed,           // re-confirmed by the §8 method (§8.2 table), or by §14.4/§14.6
    Sec4ScanPendingRecheck,  // only in §4's automated-scan table; OPEN until re-checked against the exe
    RefutedSec14_3,          // §14.3: not a Lua-hook dispatcher call (a different native mechanism)
    GroupLevel,              // Group 2 / Group 3: see hookGroupNote()
};
const char* hookEvidenceLabel(HookEvidence e);

struct HookSpec {
    std::string name;
    HookGroup group;
    HookEvidence evidence = HookEvidence::GroupLevel;

    // Real per-hook argument list this firing pass pushes before
    // lua_pcall, IN ORDER - EMPTY for every hook except the small,
    // explicitly-enumerated set spec-lua-bindings.md Sec14 gives a real,
    // confirmed argument count/type for (see lua_hook_registry.cpp's own
    // per-hook comments for exactly which ones and the real citation for
    // each). This field existed empty from the start (this project's own
    // prior "no invented fixes" scope, HANDOFF.md Sec9.117) specifically
    // so this later pass could populate it per-hook without any change to
    // the firing loop's own shape - Host::fireConfirmedHooks (host.h)
    // always reads whatever is here, empty or not.
    std::vector<HookArg> args;
};

// The full, static confirmed/pattern-based hook table - originally 133
// entries (spec-lua-bindings.md Sec12.3 [73, Group 1] + Sec12.4 [2, Group
// 2] + Sec12.6 [58, Group 3], transcribed directly from those sections'
// own numbered listings #1-73/#74-75/#76-133, cross-checked against
// Sec12.9's own total of 155 = 133 + Sec12.7's Groups 4-7 [mechanisms/
// property-accessors, not literal hook names, correctly excluded here]),
// extended same-day (2026-09-29) with 5 more Group-1 names from Sec14.6 -
// 138 entries total. Call `.size()` for the live count rather than citing
// a fixed number - this file's own top comment has the full provenance.
// A flat compiled table, not a runtime-loaded text file: these are fixed
// literal strings quoted directly from the spec's own prose, not a
// population this project re-scans per run.
const std::vector<HookSpec>& confirmedHooks();

} // namespace sr3luahost
