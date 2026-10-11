// Vint (UI document/object) engine state: VdoObject, LoadedVintDocument,
// VintTaggedValue, VintStoredProperty, VintPropertyStats, VintDataItem,
// VintDataResponderRecord.
//
// Split out of engine_state.h (2026-10-08) as a zero-behavior-change
// structural move, so that a merge can only ever land a new field in its
// own domain's file - the monolithic header kept having fields from one
// struct merged into a DIFFERENT struct whose closing brace sat next to
// it. engine_state.h includes this header, so callers keep including
// engine_state.h only; add new fields to these structs HERE.
//
// In the doc comments below, "this header", "this document", "this file"
// and "this header's own top note" keep their pre-split meaning: the
// engine-state header set as a whole (engine_state.h plus the per-domain
// headers it includes), whose top note is at the head of engine_state.h.
// "above"/"below" pointers to a type or member now in another file were
// re-worded to name that file; every other line moved verbatim.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace sr3luahost {

// vint_object_find (spec-lua-bindings.md Sec15, folded in mid-task per the
// orchestrator's relay of Team A's Sec13.7/Sec15 finding: this native
// global was simply UNREGISTERED, accounting for 43% of all hook-fire
// errors - a nil-global call, not a "stub returns the wrong thing" case).
// One entry = one "VDO UI-element instance" the real engine would track;
// this project's own minimal stand-in, populated ONLY via
// EngineState::registerVdoObjectForTesting() (no Lua-visible function in
// this pass's scope populates it - vint_object_create remains an ordinary
// generic logging stub, per the orchestrator's own "you don't need to
// implement all of those this pass" allowance) - stated plainly, not
// hidden: every REAL script call this pass can exercise therefore takes
// the real, CONFIRMED "not found" path (returns 0.0), which is itself
// correct, real behavior for a never-populated object space, not a
// simplification of the return contract itself.
//
// UPDATE 2026-10-03: no longer test-only. EngineState::loadVintDocument()
// is a real loading path - a parsed `.vint_doc` (sr3vintdoc::parseDocument,
// layout CONFIRMED in spec-vint-doc-format.md at main 67455c3) becomes one
// VdoObject per element/animation record, with real names, parents, first
// children and file-applied properties. Hosts that never load a document
// (every existing test) see exactly the old behaviour.
struct VdoObject {
    std::string name;
    // Real, CONFIRMED-mechanism hash of `name` (0x00d9e740, lower-cased,
    // this project's own default seed 0 - see EngineState::findVdoObject's
    // own doc comment for why 0, specifically).
    uint32_t nameHash = 0;
    // 0 = "no parent" (a document-root object) - this project's own chosen
    // sentinel (real handle 0 is never issued by nextVdoObjectHandle(),
    // which starts at 1, so 0 is unambiguous as "none").
    uint32_t parentHandle = 0;
    // Which "document" this object belongs to - 0 is this project's own
    // chosen sentinel for "the current default document" (real
    // 0x00e0ceb0's own stand-in, EngineState::currentDefaultDocHandle()).
    uint32_t docHandle = 0;
    // vint_object_first_child (spec-lua-bindings.md Sec18.1): the real
    // record's own offset +0x1c, "the record's first-child pointer." 0 = "no
    // children" - this project's own chosen sentinel, same reasoning as
    // parentHandle above (handle 0 is never issued by nextVdoObjectHandle()).
    // Only ever set by EngineState::setVdoObjectFirstChildForTesting() - no
    // Lua-visible function in this pass's scope (vint_object_create remains a
    // generic stub, same as parentHandle/docHandle's own established
    // precedent) populates a real parent/child tree, so a real script's
    // vint_object_first_child call on any object this host can resolve
    // honestly takes the real, CONFIRMED "no children" path unless a test set
    // one up directly.
    uint32_t firstChildHandle = 0;

    // --- Set only by EngineState::loadVintDocument (2026-10-03), the real
    // loading path; left empty/0 by registerVdoObjectForTesting(). ---
    // The record's registered type name (`bitmap`, `group`, `tween`, ...),
    // resolved through the document's string table (spec-vint-doc-format.md
    // Sec4).
    std::string typeName;
    // The next record under the same parent in file order (0 = last). Not
    // read by any Lua-visible function yet (vint_object_next_sibling is not
    // implemented); kept so the tree's sibling order is not lost.
    uint32_t nextSiblingHandle = 0;
    bool fromDocument = false;
};

// One document loaded by EngineState::loadVintDocument (2026-10-03).
struct LoadedVintDocument {
    std::string name;          // the caller's document name, e.g. "cmp_mission.vint_doc"
    uint32_t handle = 0;       // this project's own document handle (see loadVintDocument)
    std::string luaScriptFile; // metadata "lua_script_file", "" if absent
    std::vector<uint32_t> elementHandles;   // top-level elements (header +0x1A), file order
    std::vector<uint32_t> animationHandles; // top-level animations (header +0x1C), file order
    size_t objectCount = 0;    // every record, recursively
    size_t propertyCount = 0;  // every applied property, over all records
};

// vint_dataitem_get (spec-lua-bindings.md Sec19.2): one generic captured Lua
// scalar - this project's own minimal stand-in for the real 24-byte tagged-
// value record this pass's spec text finds reused across several of these
// natives' own argument/field marshaling (Sec19.2's data-item fields,
// Sec21.2's data-responder trailing args). `None` covers a slot this project
// has no typed value for (a nil, or - per Sec21.2's own text - "any other
// type gets an untyped/default slot") rather than guessing a type.
struct VintTaggedValue {
    enum class Kind { Number, Boolean, String, None };
    Kind kind = Kind::None;
    double number = 0.0;
    bool boolean = false;
    std::string text;
};

// One stored Vint property value (vint_set_property / the document loader /
// the element defaults). `typedAs` is the type/size code
// (include/sr3luahost/vint_property_types.h) the values were parsed or
// decoded as - their shape then matches that code exactly (vec2 = 2
// numbers, ...). 0 = stored raw, as the caller gave it (an untyped test
// object, a row whose code is OPEN in clean spec text, or a value this host
// cannot convert faithfully - see EngineState::setVintProperty).
struct VintStoredProperty {
    std::vector<VintTaggedValue> values;
    uint8_t typedAs = 0;
};

// Counters for the typed property path (2026-10-03), so a real run can show
// how much of the traffic went through a confirmed type versus a CHOSEN
// pass-through or an OPEN refusal. Reported by lua_host_run as
// vint_properties= in verdict_summary.txt.
struct VintPropertyStats {
    // vint_set_property
    uint64_t setBadHandle = 0;           // handle resolves to no object -> silent no-op (CONFIRMED)
    uint64_t setTyped = 0;             // parsed through the row's (or a borrowed) code
    uint64_t setRawOpenCode = 0;         // row found, code OPEN in clean text -> stored raw (CHOSEN)
    uint64_t setRawUntypedObject = 0;    // object has no registered type (test-registered / clone) -> legacy raw
    uint64_t setRawUnknownType = 0;      // object's type name is none of the 13 -> legacy raw
    uint64_t setRawEnumUnresolved = 0;   // 0xd string not resolvable from the clean tables -> stored raw
    uint64_t setRawTweenBorrowUnresolved = 0; // tween start/end_value whose target code is unknown -> stored raw
    uint64_t setTweenBorrowTyped = 0;    // subset of setTyped: parsed with the target property's code
    uint64_t setNameMiss = 0;            // no descriptor on the type chain -> no write (CONFIRMED)
    uint64_t setReadOnlyNoop = 0;        // fail-stub setter (screen_nw/se, unscaled_size; group/point screen_size)
    uint64_t setShapeRefusals = 0;       // OpenStateError: Lua values do not fit the code
    uint64_t setCallbackNamesStored = 0; // code 8 names stored (pool claim not modelled)
    uint64_t screenSizeSets = 0;         // screen_size setter wrote scale
    uint64_t screenSizeDivZeroGuards = 0; // HOST-SAFETY: zero current size, no write
    // vint_get_property
    uint64_t getBadHandle = 0;           // zero Lua values (CONFIRMED)
    uint64_t getTyped = 0;
    uint64_t getRawEcho = 0;             // raw stored value echoed (CHOSEN)
    uint64_t getComputed = 0;            // screen_nw/se/size, mouse_depth-follows-depth
    uint64_t getNameMiss = 0;            // zero Lua values (CONFIRMED)
    uint64_t getOpenRefusals = 0;        // OpenStateError
    // EngineState::loadVintDocument
    uint64_t docRecordsTyped = 0;
    uint64_t docRecordsRaw = 0;
    uint64_t docRecordsSkippedNoDescriptor = 0; // CONFIRMED loader skip (vint-doc Sec5)
    uint64_t docRecordsSkippedReadOnly = 0;     // fail-stub setters
    uint64_t docObjectsSeeded = 0;              // element defaults (spec-vint-render.md Sec8)
    // spec-vint-render.md Sec12.1 image-setter side effects, applied at load
    // time only when the caller passes an image-size resolver (2026-10-03,
    // UI draw unit U3 - see loadVintDocument's doc comment).
    uint64_t docImageSizesApplied = 0;          // `image` record resolved -> unscaled_size re-seeded
    uint64_t docImageSourceCoordsReset = 0;     // ... and source_nw/source_se reset (custom_source_coords clear)
    uint64_t docImageUnresolved = 0;            // resolver returned nothing -> no side effect (pool lookup failed)
    uint64_t docAutoOffsetApplied = 0;          // Sec5.4 applier wrote `offset` (auto_offset 1..9)
    uint64_t docAutoOffsetNotApplied = 0;       // Sec5.4 applier: auto_offset none/out of range/not a typed value
};

// vint_dataitem_get (Sec19.2): one "data item" record - a second, SEPARATE
// handle space from the VDO-object handles above (Sec19.2's own text: "a
// different resolver... data items are therefore a genuinely separate
// handle/record space"). This project has no real data-item loader (the
// resolved record's real own fields come from UI runtime memory this host
// does not model) - populated ONLY via
// EngineState::registerVintDataItemForTesting(). Bounded at 32 fields
// (CONFIRMED, the real record's own fixed local array size); real shipped
// source only ever reads the first 24 (`vint_lib.lua:393`).
struct VintDataItem {
    std::vector<VintTaggedValue> fields;
};

// vint_dataresponder_finished / vint_internal_dataresponder_request
// (spec-lua-bindings.md Sec21): one named-record registry entry - CONFIRMED a
// genuinely different shape from every numeric-handle resolver elsewhere in
// this document (Sec21's own text: "a name-keyed record registry"). CONFIRMED
// (Sec21.2): no Lua-visible function in this pass's 2-name scope ever
// CREATES one of these records (`vint_internal_dataresponder_request`'s own
// text: "this native never creates the named record") - so, same convention
// as VdoObject/VintDataItem above, this registry is populated ONLY via
// EngineState::registerDataResponderForTesting().
struct VintDataResponderRecord {
    // +0x258, CONFIRMED: the completion flag vint_dataresponder_finished reads.
    bool finished = false;
    // Not a real engine field: this project's own count of how many times
    // vint_internal_dataresponder_request reached its own real "type-validated,
    // record exists" gate (Sec21.2) for this name - test-observable proof the
    // gate logic runs, without claiming any further engine effect (the actual
    // dispatch, 0x00e1e660, was not independently decompiled this pass and
    // is not modeled - see EngineState::dataResponderRequest's own doc comment).
    int dispatchAttempts = 0;
};

} // namespace sr3luahost
