// EngineState slice: vint documents/objects/properties, data items, data responders, query-time geometry.
//
// Split out of engine_state.h (refactor step 3a, 2026-10-17) as a zero-behaviour-change move: every
// type, field, initialiser and comment below moved verbatim (namespace-scope types re-indented one
// level). Types that were nested in EngineState are now namespace-scope types in sr3luahost;
// EngineStateVintData re-exports each one under its old name, so `EngineState::Type` still
// resolves. EngineState inherits EngineStateVintData publicly, so `state.member`,
// `EngineState::kConstant` and `state.accessor()` are unchanged for every caller. Members that
// were private in EngineState are `protected` here (the only access change). Method
// declarations stay in EngineState (engine_state.h); bodies are in src/*.cpp.
#pragma once

#include "sr3luahost/engine_state_common.h"

namespace sr3luahost {

// --- Real document loading (2026-10-03) --------------------------------
//
// Builds the live element tree of one parsed document
// (sr3vintdoc::parseDocument) into this registry, the way the real
// binary loader (spec-vint-doc-format.md Sec4/Sec5 at 67455c3:
// 0x00e2a280 creates each element and attaches it to its parent, then
// 0x00e29860 applies its properties through the type's descriptor
// setters) does:
//  * a fresh document handle (this project's own counter, starting at 1
//    and separate from object handles - the real engine resolves
//    documents through a different resolver, 0x00e1f330, Sec18.2/
//    Sec19.1; its numbering is not specified, same "plain incrementing
//    counter" convention as registerVdoObjectForTesting);
//  * one VdoObject per element AND per animation record (Sec4: the same
//    record shape, both lists under the same document), handles issued in
//    file pre-order, name hashed with sr3save::nameHash (Sec15), parent =
//    the enclosing record (0 for a top-level record - this project's
//    existing "document root" sentinel; whether the real engine puts a
//    root object above them is not specified), firstChildHandle = the
//    first child record (Sec18.1's +0x1c), nextSiblingHandle = the next
//    record under the same parent;
//  * every applied property stored into the SAME per-(object, name-hash)
//    bag vint_set_property writes and vint_get_property reads, as the
//    loader's setter call would leave it (CONFIRMED that the loader
//    applies file values through the descriptor's +0x08 setter, Sec5 /
//    spec-lua-bindings.md Sec9.3). Value mapping (CHOSEN, one Lua value per
//    stored component, matching Sec20.2's "N values per the type" rule):
//    tag 1 -> one number from a SIGNED 32-bit integer, tag 2 -> one
//    number from an UNSIGNED 32-bit integer (both CONFIRMED widths,
//    67455c3), tag 3 -> one number (float), tag 4 -> one string (the
//    document's string at that index), tag 5 -> one boolean, tag 6 ->
//    three numbers, tag 7 -> two numbers.
// Which list is applied: the effective properties for `activeResolution`
// (Sec5's override-then-baseline rule, ElementNode::effectiveProperties).
// The real active resolution comes from a resolution table this project
// does not model; the default "" (no override matches - what every
// resolution other than the only shipped override, "640x480", gives) is
// CHOSEN.
// UPDATED 2026-10-03 (property descriptors + spec-vint-render.md):
//  * every element-family object (VdoObject::typeName is-an-element) is
//    first seeded with the element initializer's defaults
//    (spec-vint-render.md Sec8, "a host should seed every element with
//    these before applying the loaded document's own property records") -
//    except unscaled_size for bitmap/gradient/text/bitmap_circle, which
//    their own size paths set (OPEN here, Sec5.4);
//  * each file record is then matched against the object's type's
//    descriptor chain: no row -> consumed and skipped (CONFIRMED,
//    spec-vint-doc-format.md Sec5); a fail-stub row (screen_nw/se,
//    unscaled_size, spec-vint-render.md Sec7.3) -> skipped too (the
//    loader writes through the same setter); otherwise stored typed when
//    the file tag's shape is the row's code (tag n for code n, a tag-4
//    string for codes 9/0xa/0xb, a tag-1/2 integer or a resolvable tag-4
//    enumerator name for 0xd - the latter the loader's own 0x00e307c0
//    conversion, vint-doc Sec5 HIGH CONFIDENCE / spec-lua-bindings.md
//    Sec9.2a), raw otherwise. An object of a type name none of the 13
//    keeps every record raw (the pre-descriptor behaviour).
// Still NOT modelled: the auto_offset applier (spec-vint-render.md
// Sec5.4 - when it fires after a load is not specified), the instancing
// of a `document`-typed record's referenced widget document (its
// `document_name` names another .vint_doc - not specified anywhere).
// LoadedVintDocument::propertyCount counts applied FILE records only
// (not seeded defaults, not skipped records).
//
// UPDATED 2026-10-03 (UI draw unit U3, spec-vint-render.md Sec12.1):
// `imageSize`, when given, is consulted for every `image` record (tag-4
// string) of a `bitmap`-family object (bitmap and gradient - gradient's
// parent type is bitmap, spec-lua-bindings.md Sec9.1) AT THAT RECORD'S
// POSITION in the applied order, i.e. exactly where the real loader runs
// the image setter. It returns the image's own size (w, h) - the
// bitmap-resource pool lookup itself (sheet sub-image vs standalone
// image, Sec12.1) is the caller's, since this host has no resource pool.
// On a hit the CONFIRMED Sec12.1 side effects run, in this order:
//  1. if custom_source_coords is clear, source_nw := (0,0) and
//     source_se := (w,h). CHOSEN: an object with no stored
//     custom_source_coords counts as clear (bitmap's constructor default
//     for it is not given in clean text; no shipped HUD document stores
//     it - 17 of 1,820 shipped bitmap records do, all `true`);
//  2. unscaled_size := (w,h) (re-seeded from the image's own size);
//  3. the auto_offset applier (Sec5.4, vtable +0xc4 - "re-fetched on
//     every size-changing path of bitmap") with the auto_offset value
//     stored at that moment.
// Later records of the same block then overwrite whatever they name,
// exactly as the real setters would. On a miss nothing happens (the
// real pool lookup returned null - no size change) and unscaled_size
// stays OPEN for vintQueryBox, as before. NOT modelled: image_crc
// records (resolving a CRC to an image needs the pool's CRC index) and
// whether auto_offset's OWN setter re-runs the applier (not given in
// clean text - a document storing auto_offset after image with no later
// `offset` record keeps the offset it had). Without `imageSize` the
// loader behaves exactly as before (every existing caller).
using VintImageSizeResolver = std::function<std::optional<std::pair<double, double>>(const std::string& imageName)>;

// --- Query-time geometry (spec-vint-render.md Sec7, 2026-10-03) ----------
//
// The axis-aligned box behind screen_nw/screen_se/screen_size, in the
// document's own authored coordinates (Sec7.2: the query chain starts
// from identity and zero, NOT the screen fit), recomputed on every call
// (Sec7.1 - nothing cached, culling plays no part):
//  * pos/M (0x00e272c0): recurse to the parent while it is element-family,
//    else start from pos 0, M = identity; then pos = P.pos + P.M*anchor,
//    M = P.M * R(rotation) * S(scale) (Sec5.2's compose, op-for-op; the
//    matrix products are computed in double and rounded to float, Sec5.2);
//  * the four corners pos + M*(offset + q), q over the unscaled_size box
//    (0x00e280d0), their axis-aligned box, unioned with every
//    element-family child's own box, recursively (0x00e28540) - children
//    in tree order via firstChildHandle/nextSiblingHandle; tween/animation
//    children are skipped (not element-family);
//  * group: the union of its element children's boxes only; a childless
//    group "collapses to a single point" whose position Sec7.1 does not
//    give -> nullopt here (its screen_size is then (0,0); screen_nw/se and
//    any parent union that includes it refuse, OPEN);
//  * text: OPEN (layout re-run, Sec7.1/Sec11 item 6) -> OpenStateError;
//  * unscaled_size of bitmap/gradient/text/bitmap_circle (sized by their
//    own paths, Sec5.4) is OPEN unless a test set it.
// auto_offset's applier (Sec5.4, vtable +0xc4) is NOT run here: the spec
// names when it fires only for the size-changing paths of bitmap/text/
// bitmap_circle, which this host does not model, so `offset` is whatever
// was stored. Throws OpenStateError for anything above that is OPEN.
struct VintBox { double minX = 0, minY = 0, maxX = 0, maxY = 0; };
// The query-time pos/M chain itself (0x00e272c0, Sec7.1) for one
// element, in document coordinates: `px,py` = the pivot position, `m` =
// the 2x2 matrix row-major {m00, m01, m10, m11} (x' = m00*x + m01*y).
// Sec7.1: an operation-for-operation re-implementation of the render-time
// compose step (Sec5.2), differing only in its starting frame (identity/
// zero instead of the screen fit, Sec7.2) - so a renderer gets the
// render-time state as fitOffset + fitScale*(px,py) and fitScale*m
// (the fit scale is uniform, Sec5.1). Exposed 2026-10-03 for the UI draw
// path (sr3_viewer vint-doc). Throws OpenStateError as vintQueryBox does
// for an unknown/untyped ancestor.
struct VintQueryTransform { double px = 0, py = 0; double m[4] = {1, 0, 0, 1}; };

// vint_options_remap_set_key_binding (Sec11.12, 0x007cd3d0): CONFIRMED
// structural pre-check - a key code of the sentinel -1 is a clean no-op
// (no gate even consulted); otherwise a gate (0x007c2060(0), HIGH
// CONFIDENCE "is a remap confirmation dialog already open," not
// independently traced to a real target) must be FALSE for the
// function to stage a pending (action, key, flag) triple and raise a
// confirmation dialog. OPEN until a test sets the gate.
struct VintRemapStaging {
    OpenValue<bool> dialogAlreadyOpen{"remap confirmation dialog already open (0x007c2060(0), HIGH CONFIDENCE reading)",
                                      "spec-lua-api-behaviour.md Sec11.12"};
    bool hasPending = false;
    int32_t pendingActionIndex0Based = 0;
    int32_t pendingKeyCode = 0;
    bool pendingFlag = false;
    uint64_t dialogRaisedCount = 0;
};

struct EngineStateVintData {
    // Former EngineState nested types (defined above at namespace scope), re-exported so
    // `EngineState::Type` keeps naming them.
    using VintImageSizeResolver = sr3luahost::VintImageSizeResolver;
    using VintBox = sr3luahost::VintBox;
    using VintQueryTransform = sr3luahost::VintQueryTransform;
    using VintRemapStaging = sr3luahost::VintRemapStaging;

public:

    std::vector<PegLoadRequest>& pegLoadRequests() { return pegLoadRequests_; }
    const std::vector<PegLoadRequest>& pegLoadRequests() const { return pegLoadRequests_; }

    // 0x00e0ceb0's own stand-in ("a 'current default document' global
    // lookup") - default 0 (this project's own "no default document yet"
    // sentinel, VdoObject's own doc comment). Test-only setter; no Lua
    // function among this task's 13 in-scope names ever changes it.
    OpenValue<uint32_t>& currentDefaultDocHandle() { return currentDefaultDocHandle_; }
    size_t loadedVintDocumentCount() const { return loadedVintDocuments_.size(); }

    // --- vint_get_time_index (Sec19.1) --------------------------------------

    // 0x00e0ceb0's own stand-in reused a third time (Sec19.1's own text:
    // "the third sighting in this document of the identical +0x14-field read
    // feeding a 'current document' resolution") - this project's host has no
    // document registry distinct from the resolved handle number itself, so
    // a genuine "0x00e1f330 failed to resolve" cannot be told apart from
    // "no test has supplied a time index for this handle yet"; both become
    // an OpenStateError refusal (never a fabricated number). The real
    // "zero Lua values pushed" failure path is therefore not separately
    // reachable in this minimal host - same "no failure condition to model
    // honestly" precedent as cloneVdoObject above.
    OpenValueMap<double>& vintTimeIndexByDoc() { return vintTimeIndexByDoc_; }
    const VintPropertyStats& vintPropertyStats() const { return vintPropertyStats_; }

    // --- UI resolution queries (spec-lua-api-behaviour.md Sec26.26) ---------

    // vint_is_std_res (0x00e1a150, CONFIRMED rule): the two signed integers
    // of the per-thread record (0x00e236f0 +4/+8) divided in double
    // precision; < 1.5 -> true, else true only when the display mode
    // 0x0132bd80 == 2. CONFIRMED (nnlt): the record's integers are copies of
    // the global width 0x02a5a180 / height 0x02a5a184, written by the UI
    // subsystem init 0x00e23910 (front-end bring-up) and the resolution-change
    // path 0x00e23a20, which both recompute the mode (0x00e23000) and the
    // layout index 0x0132c0ac. Before either writer runs all of these are
    // OPEN (the record is allocated zeroed on first use, but scripts only run
    // after the init). The record is per thread (HIGH CONFIDENCE: filled on
    // the UI thread only); this host has one thread and one record.
    OpenValue<int32_t>& vintRecordFirst() { return vintRecordFirst_; }
    OpenValue<int32_t>& vintRecordSecond() { return vintRecordSecond_; }
    OpenValue<int32_t>& vintDisplayMode() { return vintDisplayMode_; }
    OpenValue<int32_t>& vintGlobalWidth() { return vintGlobalWidth_; }
    OpenValue<int32_t>& vintGlobalHeight() { return vintGlobalHeight_; }
    OpenValue<int32_t>& vintLayoutIndex() { return vintLayoutIndex_; }
    int vintLibInitConstantsCalls() const { return vintLibInitConstantsCalls_; }
    int vintDocumentResetsNotModelled() const { return vintDocumentResetsNotModelled_; }
    // vint_get_safe_frame (0x00e1b570): shape CONFIRMED (4 numbers from the
    // integers a = +0x8, b = +0xc of (thread context +0x674)+0x14, scaled by
    // two double constants and rounded); order (c1*a, c1*b, c2*a, c2*b) HIGH
    // CONFIDENCE. The constants are CONFIRMED (nnlt): 0x0115ba60 =
    // 0x3FB3333340000000 (0.075f widened), 0x0116dfc0 = 0x3FED9999A0000000
    // (0.925f widened); applySpecInitialState sets them. a and b stay OPEN:
    // the object at (context +0x674)+0x14 has no specced writer (reading it
    // as width/height is HIGH CONFIDENCE only).
    OpenValue<int32_t>& vintSafeFrameA() { return vintSafeFrameA_; }
    OpenValue<int32_t>& vintSafeFrameB() { return vintSafeFrameB_; }
    OpenValue<double>& vintSafeFrameScale1() { return vintSafeFrameScale1_; }
    OpenValue<double>& vintSafeFrameScale2() { return vintSafeFrameScale2_; }
    // The two constants as their CONFIRMED bit patterns.
    static constexpr uint64_t kSafeFrameScale1Bits = 0x3FB3333340000000ull; // 0x0115ba60
    static constexpr uint64_t kSafeFrameScale2Bits = 0x3FED9999A0000000ull; // 0x0116dfc0

    // game_peg_unload (Sec2.7): this project has no real PEG-pointer-
    // array/load-state tracking anywhere (game_peg_load_with_cb, Sec8.24,
    // does not model one either - see PegLoadRequest's own doc comment
    // above) - a per-filename OPEN gate stands in for "is this resource
    // currently in one of the two CONFIRMED loaded states (2 or 3)",
    // test-settable; `known(name)==false` (never configured) is treated
    // as the CONFIRMED "not found/not loaded -> silent no-op" default
    // rather than a genuine OPEN refusal, since Sec2.7's own text already
    // states that outcome plainly for an unregistered/not-loaded name.
    OpenValueMap<bool>& pegInLoadedState() { return pegInLoadedState_; }
    void recordPegUnload(const std::string& name) { ++pegUnloadCount_[name]; }
    uint64_t documentOpenCount() const { return documentOpenCount_; }
    const std::string& lastDocumentOpened() const { return lastDocumentOpened_; }
    const std::string& lastDocumentOpenTrigger() const { return lastDocumentOpenTrigger_; }
    VintRemapStaging& vintRemapStaging() { return vintRemapStaging_; }

    // --- Sec46.6 vint_options_remap_reset_bindings --------------------------
    uint64_t& vintOptionsRemapResetCount() { return vintOptionsRemapResetCount_; }

    // --- open_vint_dialog_do (§50.2 CRASH) / open_vint_dialog_check_done
    // (§42.6, corrected §50.3). CONFIRMED the completion callback only
    // ever writes the literal -1 (never an option index), gated on its own
    // 2nd argument == -1; file-default -2 ("pending"). One shared global
    // models both ends of this mechanism.
    int32_t& vintDialogResult() { return vintDialogResult_; }
    // HARDENED (CHOSEN divergence): open_vint_dialog_do's own help-text
    // table pointer is never null-checked in the real engine; this project
    // has no help-text table at all to dereference, so the hazard has no
    // structural equivalent here (trivially satisfied, not a guessed fix).
    uint64_t openVintDialogDoCount() const { return openVintDialogDoCount_; }
    void recordOpenVintDialogDo() { ++openVintDialogDoCount_; }

protected: // private in EngineState before the split
    std::vector<PegLoadRequest> pegLoadRequests_;

    // std::map (not unordered): findVdoObject's searches walk handles in
    // ascending order, which for a loaded document is the file's pre-order
    // (loadVintDocument issues handles in that order) - see findVdoObject.
    std::map<uint32_t, VdoObject> vdoObjects_;
    uint32_t nextVdoObjectHandle_ = 1;
    std::map<uint32_t, LoadedVintDocument> loadedVintDocuments_;
    uint32_t nextVintDocumentHandle_ = 1;
    OpenValue<uint32_t> currentDefaultDocHandle_{"current default vint document (0x00e0ceb0)", "spec-lua-bindings.md Sec15"};

    // --- Vint UI API, Sec18-Sec21 batch (2026-10-02) - see each public
    // accessor's own doc comment above for citation/reasoning. ---
    OpenValueMap<double> vintTimeIndexByDoc_{"vint document time index (+0x574), by resolved document handle",
                                              "spec-lua-bindings.md Sec19.1"};
    std::unordered_map<uint32_t, VintDataItem> vintDataItems_;
    uint32_t nextVintDataItemHandle_ = 1;
    // Outer key: resolved VDO object handle; inner key: sr3save::nameHash of
    // the (case-folded-by-that-hash) property name (Sec20.1/Sec9.2).
    std::unordered_map<uint32_t, std::unordered_map<uint32_t, VintStoredProperty>> vintProperties_;
    // Counted from const getters too (vint_get_property), hence mutable.
    mutable VintPropertyStats vintPropertyStats_;
    struct VintPosM { double px = 0, py = 0; double m[4] = {1, 0, 0, 1}; };
    // Keyed by sr3save::nameHash of the data-responder name (Sec21's own
    // "hash the name with the engine's standard string-hash" text).
    std::unordered_map<uint32_t, VintDataResponderRecord> vintDataResponders_;

    // --- vint_is_std_res / vint_get_safe_frame (Sec26.26) ---
    OpenValue<int32_t> vintRecordFirst_{"per-thread record (0x00e236f0)+4 (copy of width 0x02a5a180; UI init 0x00e23910 not run)",
                                        "spec-lua-api-behaviour.md Sec26.26"};
    OpenValue<int32_t> vintRecordSecond_{"per-thread record (0x00e236f0)+8 (copy of height 0x02a5a184; UI init 0x00e23910 not run)",
                                         "spec-lua-api-behaviour.md Sec26.26"};
    OpenValue<int32_t> vintDisplayMode_{"0x0132bd80 (display mode)", "spec-lua-api-behaviour.md Sec26.26"};
    OpenValue<int32_t> vintGlobalWidth_{"0x02a5a180 (display width)", "spec-lua-api-behaviour.md Sec26.26"};
    OpenValue<int32_t> vintGlobalHeight_{"0x02a5a184 (display height)", "spec-lua-api-behaviour.md Sec26.26"};
    OpenValue<int32_t> vintLayoutIndex_{"0x0132c0ac (layout index: 1 standard, 0 wide)", "spec-lua-api-behaviour.md Sec26.26"};
    int vintLibInitConstantsCalls_ = 0;
    int vintDocumentResetsNotModelled_ = 0;
    OpenValue<int32_t> vintSafeFrameA_{"safe-frame source +0x8 ((context +0x674)+0x14)", "spec-lua-api-behaviour.md Sec26.26"};
    OpenValue<int32_t> vintSafeFrameB_{"safe-frame source +0xc ((context +0x674)+0x14)", "spec-lua-api-behaviour.md Sec26.26"};
    OpenValue<double> vintSafeFrameScale1_{"double constant 0x0115ba60", "spec-lua-api-behaviour.md Sec26.26"};
    OpenValue<double> vintSafeFrameScale2_{"double constant 0x0116dfc0", "spec-lua-api-behaviour.md Sec26.26"};
    uint64_t documentOpenCount_ = 0;
    std::string lastDocumentOpened_;
    std::string lastDocumentOpenTrigger_;
    OpenValueMap<bool> pegInLoadedState_{"PEG resource in a loaded state (2 or 3)", "spec-lua-api-behaviour.md Sec2.7"};
    std::unordered_map<std::string, uint64_t> pegUnloadCount_;
    VintRemapStaging vintRemapStaging_;
    uint64_t vintOptionsRemapResetCount_ = 0;
    int32_t vintDialogResult_ = -2;
    uint64_t openVintDialogDoCount_ = 0;
};

} // namespace sr3luahost
