// IMPLEMENTED PRE-REVIEW, PENDING CLEARANCE (manager rule 2026-09-30): the
// spec-lua-api-behaviour.md / spec-lua-bindings.md sections this file rests on
// are marked "NOT yet cleared for implementation" by the 2026-09-30 desk
// review (115/122 and 57/58 review-status lines). Kept working, behaviour
// unchanged, until Team A clears them; no new behaviour from those sections
// before then (team-b/HANDOFF.md section A, standing rule).
//
// Minimal, in-memory engine-state layer backing the 13 Lua-visible names
// this task promotes from `stub_registry.h`'s generic logged-nil stub to
// real, spec-confirmed behavior (see `spec_confirmed_stubs.h`/.cpp for the
// registration side; this header is only the STATE the 13 trampolines
// read/write). Built ONLY from `spec-lua-api-behaviour.md`/
// `spec-lua-bindings.md` (sections cited per field below) - never from
// disassembly, never from `D:\SR3RTXREMIXCOMP`. 12 of the 13 are the
// original CharacterState-centric batch (spec-lua-api-behaviour.md);
// vint_object_find (spec-lua-bindings.md Sec13.7/Sec15, VdoObject in vint_state.h)
// was folded in mid-task once Team A found it was simply unregistered.
//
// Deliberately narrow, per this task's own brief: this is NOT a general
// "entity"/"object" system. There is no real handle-resolution mechanism
// anywhere in this project, and the 12 CharacterState-based functions in
// scope only ever refer to an object by the plain Lua name string they
// were themselves given - so `CharacterState` is keyed by that name string directly (a
// `std::string`), and every one of the spec's own documented sentinel
// forms (`"#PLAYER#"`, `"#FOLLOWER#"`/`1`/`2`/`3`, `"#CLOSEST_PLAYER#"`)
// is treated as just another distinct key, not resolved to some shared
// "current player" object. This is a stated, explicit simplification, not
// a claim that the real engine's own name-resolution chain
// (`spec-lua-api-behaviour.md` Sec3's own "Name-resolution sentinels" note,
// Sec4.13) works this way - inventing real object-identity aliasing beyond
// "look up by the name string these functions are already given" would be
// over-scoping this task, per its own brief.
//
// Only fields the 12 CharacterState-based functions actually read or write
// are here (VdoObject, in vint_state.h, is the separate vint_object_find
// state) - no generic "entity shape" was guessed. Each field cites the spec
// (sub)section it came from and its own confidence tier (CONFIRMED vs HIGH
// CONFIDENCE) so a later pass never silently treats a HIGH CONFIDENCE
// reading as settled.
#pragma once

// Shared includes and forward declarations for EngineState and its per-subsystem slices.
#include "sr3luahost/engine_state_common.h"

// Per-subsystem slices of EngineState (refactor step 3a, 2026-10-17): each header holds that
// subsystem's namespace-scope types and the EngineState<Subsystem>Data struct of its data
// members/accessors, which EngineState inherits publicly below.
#include "sr3luahost/engine_state_vint.h"
#include "sr3luahost/engine_state_audio.h"
#include "sr3luahost/engine_state_cutscene.h"
#include "sr3luahost/engine_state_hud.h"
#include "sr3luahost/engine_state_menus.h"
#include "sr3luahost/engine_state_stores.h"
#include "sr3luahost/engine_state_save.h"
#include "sr3luahost/engine_state_progression.h"
#include "sr3luahost/engine_state_vehicles.h"
#include "sr3luahost/engine_state_weapons.h"
#include "sr3luahost/engine_state_missions.h"
#include "sr3luahost/engine_state_ai.h"
#include "sr3luahost/engine_state_world.h"
#include "sr3luahost/engine_state_characters.h"

namespace sr3luahost {

// `Host` (host.h), matching `HitLog`/`ThreadScheduler`'s own existing
// per-Host-instance convention (stub_registry.h/thread_scheduler.h).
class EngineState : public EngineStateVintData,
                    public EngineStateAudioData,
                    public EngineStateCutsceneData,
                    public EngineStateHudData,
                    public EngineStateMenusData,
                    public EngineStateStoresData,
                    public EngineStateSaveData,
                    public EngineStateProgressionData,
                    public EngineStateVehiclesData,
                    public EngineStateWeaponsData,
                    public EngineStateMissionsData,
                    public EngineStateAiData,
                    public EngineStateWorldData,
                    public EngineStateCharactersData {
public:

    // ===== Core / cross-subsystem =====

    // Design notes: src/lua_engine_state.cpp, above the definition.
    void applyGuaranteedLethalHit(const std::string& targetName, const std::string& sourceName);
    // Optional sink for the AI transition log lines (mode changes), one string per line.
    std::function<void(const std::string&)> npcAiLogSink;
    // Line-of-sight seam for the NPC AI vision test and fire (Sec10 vision test, 0x005055a0 / 0x004eda70: one closest-hit
    // ray, true = CLEAR). (x0,y0,z0) is the NPC, (x1,y1,z1) the player; the host decides the eye offsets. Unset = clear,
    // which is the pre-AI-3 behaviour: a host with no world-space collision geometry leaves it unset (the playtest's
    // .clmesh_pc props sit at fake per-cell grid positions, so a ray against them would mean nothing for mission NPCs).
    std::function<bool(double, double, double, double, double, double)> npcAiLineOfSight;

    // Records the attacker of one damage application (spec B: rewritten on EVERY application, nil when the
    // blow had no attacker, frozen once the victim is dying/dead).
    void recordDamageAttacker(const std::string& victimName, const std::string& attackerName);
    std::vector<std::pair<std::string, std::string>> drainShieldPollQueries();

    // Design notes: src/lua_engine_state.cpp, above the definition.
    void markScriptNpcBoundForTesting(const std::string& name);

    // Design notes: src/lua_engine_state.cpp, above the definition.
    void applyScriptNpcHitPointsForTesting(const std::string& name, int32_t scriptNpcHp);

    // Design notes: src/lua_engine_state.cpp, above the definition.
    int32_t drawOrGetMaxHitPoints(const std::string& name);
    // Current HP (+0x1cb8): Sec34.2 "starts equal to the table value (step 2 of Sec34.1)" - the same value
    // maxHitPoints takes at spawn. A character whose current HP was never set takes its (drawn-or-known)
    // maximum. Stays OPEN (throws) exactly where drawOrGetMaxHitPoints() does (script-NPC override unknown).
    int32_t drawOrGetCurrentHitPoints(const std::string& name);

    // Design notes: src/lua_engine_state.cpp, above the definition.
    static void replicateStateChange(const std::string& fieldTag, const std::string& objectName);

    // --- Player runtime U0/U1 (spec-player-runtime.md Sec1/Sec2), batch
    // 2026-10-03 ----------------------------------------------------------
    //
    // The input query contract (control context 0x01411b18, binding
    // tables, every query) and the control-state machine (state table
    // 0x012e6bb8, request stack 0x012e6c58). Both are pure machinery in
    // sr3player (include/sr3player/*.h). The input system runs on the
    // headless NullDeviceSource: this host has no window, so per
    // spec-player-runtime.md Sec1.1 the context freezes in its cleared state
    // and every query reads "nothing" - the spec's own rule for an inactive
    // window, not a stand-in. Nothing in this host runs the per-frame latch
    // or the dispatcher yet (their callers are the world update, Sec1.1/
    // Sec2.3, not built here); the Lua natives below push/pop the real stack
    // and call the real "clear latched".
    sr3player::InputSystem& inputSystem() { return inputSystem_; }
    sr3player::ControlStateMachine& controlStateMachine() { return controlStateMachine_; }

    EngineState();
    // The input system and the control-state machine are wired to each
    // other through callbacks that capture `this` (rows 0/1 enter -> clear
    // latched; the latch's step-5 state getter), so an EngineState is not
    // copyable or movable.
    EngineState(const EngineState&) = delete;
    EngineState& operator=(const EngineState&) = delete;

    // Design notes: src/lua_engine_state.cpp, above the definition.
    static uint32_t multiply33XorHashBucket(const std::string& name, uint32_t bucketCount);
    // True when every reading Sec8.2 leaves OPEN gives the same bucket:
    // every byte < 0x80 (no signedness question on the load or the
    // lowercase fold), and either a power-of-two bucket count (the modulo
    // only keeps low bits) or a 32-bit hash < 2^31 (signed and unsigned
    // modulo agree on non-negative values).
    static bool multiply33XorHashBucketUnambiguous(const std::string& name, uint32_t bucketCount);
    static double doubleFromBits(uint64_t bits);
    void setTimeOfDay(int64_t newHour, int64_t newMinute); // .cpp
    void setQteSlotForTesting(int index, bool active, std::string owningPlayerName, std::vector<std::string> participants);

    // --- action_play_do / action_play_is_finished (spec-lua-api-behaviour.md
    // Sec22.29/Sec7.21), real-hit probe-mode batch (2026-10-03): see
    // lua_spec_confirmed_stubs.cpp's own doc comment above stub_action_play_do
    // for the full design and what this closes/leaves OPEN. ---------------

    // Sec22.29: "returns the FIXED sentinel -1.0 ... the SAME 0x012a3038
    // constant effect_play/effect_play_on_script_object (Sec9.1/Sec12.10)
    // already establish for their own failure case." Neither effect_play
    // nor effect_play_on_script_object is implemented anywhere in this
    // codebase yet (grepped directly - no stub_effect_play/
    // stub_effect_play_on_script_object exist), so this constant has no
    // prior shared home to reuse; defined here, named for a future
    // effect_play pass to reuse instead of re-deriving.
    static constexpr double kActionPlaySentinelResolveFail = -1.0;
    // Sec22.29: "a bare -1 ... a genuinely distinct sentinel from the
    // resolve-failure case above (a plain literal -1 here vs. the named
    // 0x012a3038 constant there - both textually '-1' but sourced from
    // different code paths)." Textually identical to -1.0 as a Lua number;
    // kept as its own named constant (not reused) to mirror that spec
    // distinction and so a test can tell which branch actually ran via the
    // counters below, not just the pushed value.
    static constexpr double kActionPlaySentinelDeadNotForced = -1.0;

    // Sec22.29's action-id lookup (0x004bf810, the animation-name->id
    // table) has ZERO confirmed entries anywhere in this project's spec -
    // the same boundary Sec29's named-object map documents for Killbane
    // ("producer (3)'s 173 call sites were never individually traced,
    // could register essentially any name"), here for animation names
    // instead of object names. This project therefore has no real table
    // data to look an action name up against, so every lookup takes the
    // "not found" path - the SAME "honest behavior of a never-populated
    // registry, not a simplification of the return contract" this project
    // already established for team_make_unfriendly's team-name registry
    // (Sec30.6, lua_spec_confirmed_stubs.cpp). Per Sec22.29's own AND
    // logic ("the final return code is 1 only if BOTH the action-lookup
    // succeeded earlier AND this dispatch itself succeeded"), a failed
    // lookup alone already forces a non-1 return regardless of what the
    // still-OPEN dispatch (0x00a275e0) would have done - so this project
    // never needs to know 0x00a275e0's own internal result to answer this
    // honestly. The EXACT non-1 numeric encoding is not itself given by
    // Sec22.29 (only the two CONFIRMED sentinels above are spelled out) -
    // CHOSEN here as 0, this document's own common boolean-as-number
    // "false" idiom used throughout (e.g. mode arguments elsewhere in
    // spec-lua-api-behaviour.md), NOT a claimed real value.
    static constexpr double kActionPlaySentinelLookupMiss = 0.0;

    // Design notes: src/engine_state_triggers_weapons.cpp, above the definition.
    bool pressShopCribInteract();

    // --- The 70-entry CAE combat-action table (0x012e1c60, Sec30.4) -----
    //
    // 0x004f2610's own case-insensitive name->index scan; CONFIRMED
    // sentinel 71 (0x47) for "not found" - which also covers the literal
    // "NONE" sentinel ai_suggest_action tests for separately in the real
    // engine (both land on the same index here, same no-op outcome, so
    // this project does not need a separate special case for it). Only the
    // name->index direction is modelled (what ai_suggest_action/ai_do_
    // scripted_advance actually need) - the reverse index->name mapping
    // (0x004f2840) is NOT implemented, since no in-scope native in this
    // batch calls it.
    static constexpr int32_t kCaeActionNotFound = 71;
    int32_t lookupCaeActionIndex(const std::string& name) const;

    // ai_set_action_delay (Sec7.30): the 70-entry action-name table
    // (0x012e1c60), case-insensitive per spec (__stricmp) - CONFIRMED
    // count (70) and CONFIRMED real "not found" sentinel (the count
    // itself). registerActionNameForTesting()'s own `name` is matched
    // case-insensitively by lookupActionNameIndex(), same as the real
    // table's own comparison.
    static constexpr int32_t kActionNameTableSize = 70;
    static constexpr int32_t kActionNameNotFound = kActionNameTableSize;
    int32_t lookupActionNameIndex(const std::string& name) const;

    // Design notes: src/engine_state_progression.cpp, above the definition.
    bool applyNewGameDefaultOutfit(const std::string& presetName);
    bool propNameResolves(const std::string& propName);

    // ===== Vint - data and types in engine_state_vint.h =====

    // Design notes: src/lua_engine_state.cpp, above the definition.
    const VdoObject* vdoObjectForTesting(uint32_t handle) const;
    // The same lookup for a non-test reader of the live tree (2026-10-03:
    // the UI draw path, sr3_viewer vint-doc, walks parent/first-child/
    // next-sibling links and reads type names). nullptr = no such handle.
    const VdoObject* vdoObject(uint32_t handle) const { return vdoObjectForTesting(handle); }

    // Design notes: src/lua_engine_state.cpp, above the definition.
    uint32_t vdoObjectFirstChild(uint32_t handle) const;
    uint32_t loadVintDocument(const std::string& docName, const sr3vintdoc::Document& doc,
                              const std::string& activeResolution = std::string(),
                              const VintImageSizeResolver& imageSize = VintImageSizeResolver());
    // nullptr for a handle loadVintDocument never issued.
    const LoadedVintDocument* loadedVintDocument(uint32_t docHandle) const;
    // `hasExplicitNonZeroDoc`/`explicitDoc`: CONFIRMED (Sec19.1) that absent,
    // nil, OR the literal number 0 all take the SAME fallback
    // (currentDefaultDocHandle()) path - a genuine difference from every
    // other optional numeric-handle argument elsewhere in this document,
    // which only fall back on absence/nil, not on an explicit 0.
    double vintGetTimeIndex(bool hasExplicitNonZeroDoc, uint32_t explicitDoc) const;

    // Design notes: src/lua_engine_state.cpp, above the definition.
    uint32_t registerVintDataItemForTesting(std::vector<VintTaggedValue> fields);
    // Returns nullptr for a bad handle (CONFIRMED: "bad handle -> zero Lua
    // values pushed" - never populated by any Lua-visible call in this
    // pass's scope, so this is itself the real, honest default), else every
    // populated field in order.
    const std::vector<VintTaggedValue>* findVintDataItemFields(uint32_t handle) const;

    // Design notes: src/lua_engine_state.cpp, above the definition.
    void setVintProperty(uint32_t handle, const std::string& propertyName, std::vector<VintTaggedValue> values);
    // vint_get_property's value list: nullopt = zero Lua values (bad handle,
    // name miss - CONFIRMED Sec20.2); throws OpenStateError as described
    // above.
    std::optional<std::vector<VintTaggedValue>> getVintProperty(uint32_t handle, const std::string& propertyName) const;
    // Raw store access (tests, diagnostics): nullptr when nothing is stored
    // under that name; the stored values exactly, with no type dispatch.
    const std::vector<VintTaggedValue>* findVintProperty(uint32_t handle, const std::string& propertyName) const;
    const VintStoredProperty* findVintStoredProperty(uint32_t handle, const std::string& propertyName) const;
    // Test/setup-only stand-in for a type-specific size path this host does
    // not model (bitmap/gradient/bitmap_circle size from their image, text
    // layout - spec-vint-render.md Sec5.4/Sec11 item 6): stores
    // unscaled_size directly (Lua cannot - its setter is the fail-stub).
    void setVintUnscaledSizeForTesting(uint32_t handle, double w, double h);
    std::optional<VintBox> vintQueryBox(uint32_t handle) const;
    VintQueryTransform vintQueryTransform(uint32_t handle) const;
    // Design notes: src/lua_engine_state.cpp, above the definition.
    void setVintScreenSize(uint32_t handle, double requestedW, double requestedH);

    // Design notes: src/lua_engine_state.cpp, above the definition.
    void registerDataResponderForTesting(const std::string& name, bool finished);
    // CONFIRMED (Sec21.1), including the real, honestly-flagged quirk: no
    // record for `name` at all -> true ("finished"), not false and not a
    // refusal - this project's never-populated-by-default registry already
    // IS that real "no record" state, so this is itself the real, honest
    // default, same "never-populated object space" precedent as VdoObject.
    bool dataResponderFinished(const std::string& name) const;
    // Design notes: src/lua_engine_state.cpp, above the definition.
    void dataResponderRequest(const std::string& name, bool validCallback, bool validMax);
    int dataResponderDispatchAttempts(const std::string& name) const;
    bool vintIsStdRes() const;
    // Design notes: src/lua_vint_resolution.cpp, above the definition.
    static int32_t vintDisplayModeLadder(int32_t a, int32_t b);
    // 0x00e2ad30 (CONFIRMED: the same rule as vint_is_std_res on the given
    // pair and mode): 1 = standard, 0 = wide (labels HIGH CONFIDENCE).
    static int32_t vintLayoutIndexFor(int32_t width, int32_t height, int32_t mode);
    // UI subsystem init 0x00e23910 (CONFIRMED, called by 0x008489e0 during
    // front-end bring-up; the pair comes from its second argument - the
    // display resolution, a HOST INPUT here: lua_host_run's --display). Stores
    // the globals and the record copy, then the mode and the layout index.
    // A ratio the ladder refuses leaves the mode and layout index OPEN.
    void vintUiSubsystemInit(int32_t width, int32_t height);
    // Design notes: src/lua_vint_resolution.cpp, above the definition.
    bool vintResolutionChange(int32_t width, int32_t height);
    // round(c * v) to nearest (Sec26.26 "round to nearest"). With the
    // CONFIRMED constants no integer below 2^23 ties; an exact tie (mode not
    // stated) or a result outside int32 is refused as OPEN.
    static double vintSafeFrameRound(double product);
    uint64_t pegUnloadCount(const std::string& name) const;

    // ===== Audio - data and types in engine_state_audio.h =====

    // Design notes: src/engine_state_hud_ui.cpp, above the definition.
    bool audioPlaying(int id) const;

    // Design notes: src/engine_state_hud_ui.cpp, above the definition.
    void personaClearGlobalSituationOverride();
    int32_t lookupAiPersonalityProfileCode(const std::string& name) const;
    // Sec14.22 "Per-slot query, exact" (2026-10-12, FINAL) port rule: report true while the conversation's
    // lines are playing; a host with no voice playback reports false. A slot nobody marked playing is false.
    bool conversationSlotActive(const std::string& slotKey) const;

    // persona_suppress_ambient_lines (Sec20.11): a reference-counted
    // "suppress" counter (floor-at-zero decrement, NOT a plain toggle) -
    // CONFIRMED shape; the counter itself is test-observable, its real
    // consumer is OPEN (not modeled).
    void personaSuppressAmbientLinesAdjust(bool suppress);

    // --- radio_get_station (Sec40.2) --------------------------------------
    // CONFIRMED: "-1 if unresolved, -1 if resolved but tuned to no
    // station, else the 1-based station index." Default (no entry) is "no
    // station tuned," a REAL confirmed answer, not OPEN - a plain map,
    // not an OpenValueMap.
    int32_t radioStationForVehicle(const std::string& name) const;
    // CONFIRMED: explicitly rejects index 0; "leaves currentIndex stale"
    // on any OTHER index (i.e. this never resets currentIndex to -1).
    bool clearSingAlongTrack(int32_t songIndex);

    // ===== Cutscene - data and types in engine_state_cutscene.h =====
    void installZsceneTable(std::vector<ZsceneTableRow> rows);
    static std::string zsceneTableKey(const std::string& name);
    // zscene_prep (Sec8.21 / Sec26.25 lifecycle step 1-2, CONFIRMED): the gate
    // 0x007232e0, the stub 0x0101b530 (nothing), the teardown
    // 0x00721c20(1, 0, 0) of the current scene, then pending := entry and
    // the two per-object parameters := the zero constants. All reads happen
    // before any write, so an OPEN read refuses with no partial update.
    void zscenePrep(const std::string& name);
    // Design notes: src/lua_cutscene.cpp, above the definition.
    bool zsceneIsLoaded(bool hasName, const std::string& name);

    // Design notes: src/lua_cutscene.cpp, above the definition.
    void cutsceneHostFrame();
    // "frames:N blocked_on_open:N promotions:N completions:N ..." for lua_host_run.
    std::string cutsceneFrameSummary() const;
    // Init 0x0059fa30 (CONFIRMED body): document found -> id loaded, state :=
    // 2, target := 2, flag := 1; not found -> id stays -1.
    void screenFadeInit(bool documentFound);

    // Request helpers 0x0059f8c0 (out = true) / 0x0059fc40 (out = false),
    // CONFIRMED full bodies, (durationMs, completionCallback, flag). The
    // audio-id posts are HYPOTHESIS and not modelled. Throws OpenStateError
    // (before any write) on an OPEN read.
    void screenFadeRequest(bool out, int32_t durationMs, FadeCallback cb, uint32_t flag);
    // Body of Screen_fade_transition_complete 0x005a0110 (CONFIRMED).
    // Returns true when it flipped a running transition (0 -> 2, 1 -> 3).
    bool screenFadeCompletionBody();
    // The Lua-visible native: the body, counted as the real completion path.
    void screenFadeCompletionNative();

    // Design notes: src/lua_screen_fade.cpp, above the definition.
    void screenFadeHostFrame(int64_t elapsedMs);
    // "real:N fallback_undefined:N fallback_no_callback:N" (lua_host_run's
    // verdict_summary.txt line fade_completion_path=...).
    std::string screenFadeCompletionPathSummary() const;
    // Bit 0x2: the broadcast helpers 0x005a0270 / 0x005a0400 (CONFIRMED):
    // nothing unless the host gate holds (session present, +0x5c == +0x58);
    // then an opcode-0x53 record to the peers (no networking here: counted)
    // and byte 0x013effc5 := 1 (out) / 0 (in). Throws on an OPEN gate read.
    void screenFadeBroadcast(bool out);
    // Host wiring, same "attach once" convention as attachScreenFadeUiState()
    // above. Both null (the default) means push/pop still track depth/reason
    // correctly but never touch the scheduler - exercised that way by any
    // test that does not call this.
    void attachGamePauseScheduler(BareThreadTable* threads, lua_State* gameplayL);
    void pushGamePauseReason(int32_t reason);
    void popGamePauseReason(int32_t reason);

    // Design notes: src/lua_cutscene.cpp, above the definition.
    void cutscenePlayDo(const std::string& name, std::vector<std::string> destinations);
    // Design notes: src/engine_state_hud_ui.cpp, above the definition.
    bool cutscenePlayCheckDone();

    // Design notes: src/lua_cutscene.cpp, above the definition.
    void cutscenePlayApplyEndPlacement();
    // non-null only when cutsceneCameraScriptOutcome() == Loaded.
    const sr3cutscene::CameraScript* cutsceneActiveCameraScript() const;
    // Design notes: src/lua_cutscene.cpp, above the definition.
    sr3cutscene::CameraSample sampleCutsceneCamera(double t) const;

    // Design notes: src/engine_state_hud_ui.cpp, above the definition.
    void recordRefractionSituationClear(int64_t flags);
    MachinimaPathResult buildMachinimaClipPath(const std::string& clipName);

    // ===== Hud - data and types in engine_state_hud.h =====
    // HUD screen popped (spec Sec24.24 / blocker m16): calls the screen's stored exit hook with
    // (screen_name, player_name). Returns whether a callback was queued.
    bool popHudScreen(const std::string& screenName, const std::string& playerName);
    // The name at `index`, or nullptr for index 176 (OPEN) / out of range.
    static const char* tutorialName(int index);
    // 0x00717780 over the 209 known names. couldBeIndex176 is set when no
    // known name matches (or one matches only after index 176) and `name`
    // could be the OPEN string (shorter than 4 characters or non-ASCII).
    static TutorialLookup tutorialLookup(const std::string& name);
    // tutorial_advance's success path builds and dispatches a named UI
    // message (HYPOTHESIS: to the tutorial UI document); not modelled, only
    // counted per resolved index.
    int tutorialAdvanceCount(int index) const;
    void recordTutorialAdvance(int index);

    // Design notes: src/engine_state_progression.cpp, above the definition.
    static int32_t tutorialDescriptorDefaultParam20(int32_t index);
    bool qteHumanIsUsed(const std::string& name) const; // .cpp

    // Design notes: src/lua_engine_state.cpp, above the definition.
    int tutorialLockCount(int index) const;
    void recordTutorialLock(int index);

    // Design notes: src/engine_state_world.cpp, above the definition.
    void recordObjectiveTextClear(int64_t slotIndex);
    uint64_t objectiveTextClearCount(int64_t slotIndex) const;

    // Design notes: src/engine_state_hud_ui.cpp, above the definition.
    HudBarSlot* hudBarSlot(int64_t id);

    // hud_timer_set/hud_timer_stop (Sec8.19/8.20): same fixed-size-array
    // shape and the same undocumented-bounds-check gap as the bar-slot
    // family above (Sec8.19's own text: "the slot table isn't already
    // full (0x022b6600 < 4)" - CONFIRMING a 4-slot cap) - HARDENED the
    // same way.
    HudTimerSlot* hudTimerSlot(int64_t id);
    int hudDisplayCreateState();
    void hudDisplayRemoveState(int slotId, bool stashBool);
    void hudDisplayCommitState(int slotId, bool stashBool);
    void qteCleanup();
    // qte_start (Sec20.9, wave 2 batch W2-1): the authoritative branch's slot
    // activation. HIGH CONFIDENCE one slot per character (Sec20.9: the slot
    // index is "the character's own capability-byte-derived slot index"), so
    // a second qte_start for the same character overwrites its entry instead
    // of claiming another. See QteSlotEntry's own doc comment.
    void qteStartActivate(const QteSlotEntry& entry);
    void recordHudPrompt(const std::string& target, const std::string& a2, const std::string& a3);
    void recordHudPromptDefaultReset();
    std::string takeHudPromptCallbackOnInteract();
    // Design notes: src/engine_state_hud_ui.cpp, above the definition.
    int32_t internHudSecondaryKey(const std::string& key);

    // Design notes: src/engine_state_progression.cpp, above the definition.
    int tutorialUnlockCount(int index) const;

    // ===== Menus - data and types in engine_state_menus.h =====

    // Mission cellphone calls (spec V.3).
    void beginMissionCellphoneCall(const std::string& callId);
    bool answerMissionCellphoneCall(const std::string& callId);   // fires cb1 (no args)
    bool endMissionCellphoneCall(const std::string& callId);      // fires cb2 (no args)
    void removeMissionCellphoneCall(const std::string& callId);   // clears slots, NEVER fires cb2
    void resetMissionCellphoneCalls();                            // player reset/respawn/clear-all: cb2 for each live call
    // coop_is_active's core predicate 0x00867830 (Sec3.1, CONFIRMED): session
    // present; host check first, else the client gate; member count >= 2;
    // the member walk. Throws OpenStateError on an OPEN read it needs.
    bool coopIsActive() const;
    // game_get_is_host (Sec8.27) and the host gate of the 0x53 fade
    // broadcasts (Sec26.24): session present and +0x5c == +0x58.
    bool coopLocalIsHost() const;
    // Completion_is_client (Sec10.2): session present and +0x5c != +0x58.
    bool coopLocalIsClient() const;

    // Test-only direct fixture for the hovered-zone global's own confirmed
    // gate fields (see PauseMapHoveredZone's own doc comment for why this is
    // test-only - no cursor/pointer-input system exists in this host).
    void setPauseMapHoveredZoneForTesting(uint32_t zoneHandle, bool hasParentDistrict, int32_t fieldPlus0x54);

    // Design notes: src/lua_engine_state.cpp, above the definition.
    double pauseMapZoneControlFraction(uint32_t zoneHandle) const;

    // pause_map_set_gps's own CONFIRMED stag-mode branch (Sec31.3) - see this
    // method's own .cpp doc comment for the full gate. The real non-stag
    // GPS-route behavior is not traced by this section and is not modeled
    // (this project's own stated simplification - see the .cpp doc comment).
    void pauseMapSetGpsStagBranch();

    // Design notes: src/lua_engine_state.cpp, above the definition.
    void pauseMapStagTakeover();

    // The real +0x88 bit this task was asked to report on directly:
    // "did this mission's own completion flag (bit 0x4) get set." See
    // writeMissionCompletionFlagBits()'s own doc comment (below, Sec15.9/
    // Sec15.10's own section) for the single real writer this reads back.
    bool missionCompletionFlagBitSet(const std::string& name) const;
    void writeMissionCompletionFlagBits(const std::string& name);
    std::string languageString() const;
    void completionLoadLut(const std::string& name);
    void recordPauseMenuUpdateOption(int32_t category, int32_t optionId, bool flag, double value);
    const PauseMenuOptionRequest* pauseMenuOptionRequest(int32_t category, int32_t optionId) const;
    uint64_t pauseMenuRestoreDefaultsCount(int32_t category) const;
    void setDialogSlotShowingIdForTesting(int slot, int32_t id);

    // ===== Stores - data and types in engine_state_stores.h =====
    // Design notes: src/engine_state_world.cpp, above the definition.
    void selectVcustComponentForTesting(int32_t index, double price);
    std::optional<ShopCribTriggerKind> shopCribTriggerKind(const std::string& triggerName) const;
    void storeWeaponHideWeapon();
    std::vector<CustomizationOutfitWearRequest>& customizationOutfitWearRequests();
    std::vector<CustomizationItemWearRequest>& customizationItemWearRequests();
    static std::string pcrMorphKey(const std::string& sessionLower, const std::string& slotKey);

    // mission_store_purchased (Sec25.3, f752324 answer): a pure QUERY; the
    // boolean arg is a KIND selector (false=store, true=property), not a
    // value. Holds the sticky purchased flag of the first mission-tagged
    // record of each kind; nullopt = no such record loaded (query answers
    // true). Nothing in the host loads shop records yet, so nullopt default.
    std::optional<bool>& missionStoreRecordPurchased(bool isProperty);

    // ===== Progression - data and types in engine_state_progression.h =====
    // Returns the set of unlock names just granted (for callers/tests that
    // want the data-driven result without re-scanning the table
    // themselves). Additive - does not clear any previously-granted name.
    const std::unordered_set<std::string>& grantNewGameAutoUnlocks(
        const std::vector<sr3tables_progression::Unlockable>& unlockables);
    void dlc2ClapboardsReset(int32_t rawCount);
    // Returns -1 for "no value" (the real native's own CONFIRMED out-of-
    // range answer - nil, not false - and the HOST-SAFETY-refused case
    // below); otherwise 0 or 1, the flag byte at the resolved index.
    int dlc2ClapboardsGet(int32_t clapboardNumber);
    void writeMissionUnlockedFlagBit(const std::string& name);
    void recordMissionUnlock(const std::string& name) { writeMissionUnlockedFlagBit(name); }
    bool missionUnlockedValue(const std::string& name) const;

    // Design notes: src/engine_state_progression.cpp, above the definition.
    void setGameChoiceState(uint8_t index, bool state);
    void setDlcAchievementCounter(int64_t index, int32_t value);
    int32_t dlcAchievementCounterValue(int64_t index) const;
    void recordRespectGrant(bool targeted, bool notifyRequested);

    // 0x0060eb40(n): record n's Respect cost, or 22000 (0x55F0) for n past
    // the loaded table (CONFIRMED, Sec5.2) - the exact accessor 0x0060eb60
    // wraps for the live path (Sec5.4). `n` is the 0-based record index
    // equal to the number of levels already gained (Sec5.2's own
    // "L = levels gained, and record L... is the cost of the next level").
    uint32_t respectLevelCost(uint32_t level) const;

    // Design notes: src/engine_state_progression.cpp, above the definition.
    bool respectUnlockApplied(const std::string& name) const;

    // Design notes: src/engine_state_progression.cpp, above the definition.
    uint64_t statRecordOpenCount(int32_t statId) const;

    // Design notes: src/engine_state_progression.cpp, above the definition.
    uint32_t respectLifetimeCap() const;
    void awardRespect(int32_t amount);

    // Design notes: src/engine_state_progression.cpp, above the definition.
    void applyRespectLevelUp(uint32_t amount);
    // RP-6: applies the effects of library level-ups (unlock flags, HomieSlot1, decade milestone, hint 0xA9).
    void commitRealRespectLevelUps(const std::vector<sr3progression::LevelUp>& ups);

    // Design notes: src/engine_state_progression.cpp, above the definition.
    void consumeRespectLevelUnlocks(uint32_t row);

    // Level-up notification (Sec5.4): hint id 0xa9 posted through
    // 0x007169e0 - the SAME 210-message HUD hint/tutorial dispatcher this
    // project already counts via recordTutorialAdvance/tutorialAdvanceCount
    // above (Sec10.4) for ordinary first-use tutorial tips - reused
    // directly, not a second, bespoke "level up!" popup widget.
    void postRespectLevelUpHint() { recordTutorialAdvance(kRespectLevelUpHintId); }

    void runRespectDecadeMilestone(uint32_t newLevel);

    // Design notes: src/engine_state_progression.cpp, above the definition.
    bool dlcAchievementCounterGet(int64_t index, double& outValue);

    // Design notes: src/lua_engine_state.cpp, above the definition.
    void dlc2ClapboardsSet(int32_t clapboardNumber, bool value); // .cpp

    // Design notes: src/engine_state_world.cpp, above the definition.
    float damageDealtDifficultyMultiplier() const;

    // ===== Vehicles - data and types in engine_state_vehicles.h =====

    // Vehicle destroyed (spec D): queues on_vehicle_destroyed(vehicle_name, nil) on the vehicle's record slot
    // (or the player's slot 0x12 when the vehicle has no script record and a player is aboard), BEFORE the
    // occupants are killed, once per vehicle. Reached from vehicle_detonate.
    void notifyVehicleDestroyed(const std::string& vehicleName);

    // Vehicle seating commit / exit commit (spec V.1). Order: vehicle record hook first, then the occupant's
    // hook. seat < 0 picks the lowest free seat (0 = driver). Returns the seat used.
    int commitVehicleSeat(const std::string& occupantName, const std::string& vehicleName, int seat);
    void commitVehicleExit(const std::string& occupantName, const std::string& vehicleName);
    // on_vehicle_exit registration (player slot 0x17 / NPC slot 0x18 / vehicle slot 6), kept by target name;
    // persistent, "" clears.
    void setOnVehicleExitHook(const std::string& targetName, const std::string& callbackName);
    // RC vehicles (spec Sec18.10, blocker m20): per-vehicle controller, null by default. RC start bails when the
    // character already has an active vehicle; stop clears the vehicle's controller.
    bool beginRcControl(const std::string& vehicleName, const std::string& characterName);
    void endRcControl(const std::string& vehicleName);
    bool playerIsRcDriver(const std::string& characterName, const std::string& vehicleNameOrEmpty);
    std::vector<std::pair<std::string, std::string>> drainRcDriverPollQueries();

    // Design notes: src/engine_state_world.cpp, above the definition.
    void installVehicleTypeClassTable(std::unordered_map<std::string, int32_t> classByLowerTypeName);
    // The vehicle's `Special_Vehicle_Type` (xtbl +0x4C4: none 0, Bus 1, Limo 2, Firetruck 3, Tow Truck 4, SWAT Van 5,
    // Police 6, FBI 7, Ambulance 8, News Van 9, Taxi 10, Meter Maid 11, Industrial 12, ATV 13, Tank 14; a missing
    // tag is 0 - spec-lua-api-behaviour.md Sec33.1, blocker m10, FINAL), keyed by the lower-cased vehicle type name.
    // Applied to a zone-placed script_vehicle's VehicleState::vehicleClass at zone ingest (same join as the
    // Vehicle_Type class table above). Never installed -> vehicleClass stays OPEN, exactly as before.
    void installVehicleSpecialTypeTable(std::unordered_map<std::string, int32_t> specialByLowerTypeName);

    // Design notes: src/lua_engine_state.cpp, above the definition.
    VehicleState& getOrCreateVehicle(const std::string& name);
    bool hasVehicle(const std::string& name) const;
    void bridgeLiveVehiclePosition(const std::string& name, const WorldPosition& pos);

    // Design notes: src/lua_engine_state.cpp, above the definition.
    void setVehiclePathfindResolvableForTesting(const std::string& vehicleName, bool resolvable);
    // Pushed AS A NUMBER by the real native (CONFIRMED, Sec9.10 - NOT a
    // boolean, unlike the three siblings above): the raw 0/1/2 status code.
    double vehiclePathfindCheckDoneCode(const std::string& vehicleName);
    // Sec9.10a (spec e8dc04d): vehicle path-follow arrival condition. Called by
    // vehicle_pathfind_to_do once it accepts a request: registers a PENDING kind-2
    // request keyed by the vehicle, remembering the goal object name.
    void registerVehiclePathfindRequest(const std::string& vehicleName, const std::string& targetName);
    // Pure arrival test, Sec9.10a item 1 (0x00b4ea00, last node): horizontal (X/Z
    // only, Y ignored) squared distance <= threshold; threshold 4.0 (sub +0x1c bit 0
    // set) else 16.0; mode 2 overrides to 2500.0.
    static bool vehiclePathfindArrived(const WorldPosition& vehicle, const WorldPosition& goal,
                                       bool tightThreshold = false, bool mode2 = false);
    bool trafficLanesEnabledValue(const std::string& name) const;

    // Design notes: src/engine_state_world.cpp, above the definition.
    static bool looksLikeVehicleNameForPlayerClassGate(const std::string& name);

    // Design notes: src/lua_engine_state.cpp, above the definition.
    void stopVehicle(const std::string& vehicleName, bool authorityPasses);
    int64_t createRoadblock(const std::string& templateName, const std::string& placementObjectName); // .cpp
    bool destroyRoadblock(int64_t handle); // .cpp: CONFIRMED requires the roadblock-kind descriptor bit (modelled as
    double setRcMaxSignalRangeReturningPrevious(double newRange);

    // Design notes: src/lua_engine_state.cpp, above the definition.
    uint64_t despawnAllVehicles(); // .cpp
    void registerVehicleTeamIdForTesting(const std::string& teamName, int32_t id);
    int32_t lookupVehicleTeamId(const std::string& teamName) const;

    // ===== Weapons - data and types in engine_state_weapons.h =====
    // Real weapons.xtbl (+ loaded DLC) row names (spec-lua-api-behaviour.md
    // Sec4.10 "Table contents"): every name resolves true, and once installed
    // the table is COMPLETE, so any other name resolves false (native does
    // nothing) instead of OPEN-throwing. Names are case-folded here.
    void installWeaponDefinitionNames(const std::vector<std::string>& names);
    void setWeaponDefinitionClass(const std::string& name, int32_t weaponClass);
    // CHOSEN rule per spec-lua-api-behaviour.md Sec55.4 (2026-10-12, FINAL as guidance): true when the
    // character holds a weapon of the thrown class (Weapon_Class == 5; the 2026-10-12 second pass drops the
    // ammo condition and adds a carried-throwable-prop case this host does not model); false otherwise. Only the
    // temporary-grant inventory (inv_weapon_add_temporary) is modelled here - the character's
    // permanent inventory is not.
    bool characterHoldsThrownWeapon(const std::string& character) const;
    // Resolve a case-folded weapon name; OPEN-throws only if no real table installed.
    bool weaponDefinitionResolve(const std::string& foldedName);

    // The real per-target, per-weapon inventory-slot record both natives
    // write/clear (WeaponTemporaryInventorySlot, misc_state.h - see its own
    // doc comment for exactly which fields are CONFIRMED vs. HIGH-
    // CONFIDENCE-only raw storage). Outer key: target name (NOT case-
    // folded - matches objectResolves()'s own exact-case convention, since
    // the target resolves through the generic/character resolver, not the
    // weapon-definition one). Inner key: AsciiLowerFold(weapon name) -
    // matches the real `_stricmp` lookup weaponDefinitionResolves() above
    // already models, so "Gdhc" and "GDHC" hit the SAME slot, exactly as
    // the real engine's shared weapon-definition table would.
    std::unordered_map<std::string, std::unordered_map<std::string, WeaponTemporaryInventorySlot>>&
    weaponTemporaryInventory();
    void satelliteWeaponExit(bool remote); // .cpp
    std::vector<OpenSlotStatus> openSlotInventory() const;
    static bool IsHeavyPickupOnlyWeaponClass(int weaponClass);

    // Design notes: src/engine_state_triggers_weapons.cpp, above the definition.
    void giveEquipWeapon(uint32_t weaponDefId, int32_t primaryAmmo, int32_t roundsInClip,
                          int32_t secondaryAmmo = 0, uint8_t flag0 = 0, uint8_t flag1 = 0);
    bool hasWeapon(uint32_t weaponDefId) const { return weaponSlot(weaponDefId) != nullptr; }
    const WeaponInventoryItem* weaponSlot(uint32_t weaponDefId) const;
    void registerWeaponDrop(const std::string& dropName, double x, double y, double z,
                             uint32_t weaponDefId, int weaponClass, int32_t primaryAmmo, int32_t roundsInClip);
    // Design notes: src/engine_state_triggers_weapons.cpp, above the definition.
    void updateWeaponDropPickup(double playerX, double playerY, double playerZ);
    bool isWeaponDropConsumed(const std::string& dropName) const;
    void registerHeavyWeaponPickupTrigger(const std::string& triggerName, uint32_t weaponDefId,
                                           int32_t primaryAmmo, int32_t roundsInClip);
    // Design notes: src/engine_state_triggers_weapons.cpp, above the definition.
    const std::string& updateHeavyWeaponPickupPrompt(double playerX, double playerY, double playerZ);

    // Design notes: src/engine_state_triggers_weapons.cpp, above the definition.
    bool pressHeavyWeaponPickupInteract();
    int32_t resolveInventorySlotId(const std::string& slotName) const;
    // spec-lua-api-behaviour.md Sec17.25 "Item lookup source" (2026-10-12, FINAL): the lookup scans the
    // items_inventory.xtbl rows; the placeholder "none" is not a row, so it resolves null and the removal
    // is skipped silently. Any other unregistered name stays OPEN (the 110 row names are not installed).
    bool inventoryItemDefinitionResolve(const std::string& name) const;

    // Real items_inventory.xtbl (+ DLC) row names (spec-lua-api-behaviour.md Sec17.25 "Item lookup source",
    // spec-tables-vehicle-world.md Sec15): _stricmp resolver; the table is then COMPLETE, so any other name
    // resolves false (removal skipped silently). Names are case-folded.
    void installInventoryItemNames(const std::vector<std::string>& names);
    // f752324 (Sec17.13 answer): the prop name resolver is the items_3d.xtbl
    // catalogue; once its names are installed the table is COMPLETE, so any
    // other name resolves false (silent no-op). Case-folded.
    void installItems3dNames(const std::vector<std::string>& names);

    // ===== Missions - data and types in engine_state_missions.h =====

    // Spec "Engine callback dispatch" F (FINAL), characters: fires on_take_damage(victim, attacker|nil, hp_fraction,
    // from_explosion (0x8), from_melee (0x1), from_sonic (0x4000)) on the victim's hook (player slot 4, scripted NPC
    // slot 6) unless `flags` has bit 0x200. hp_fraction is the health after the hit over max health, <= 0 on the
    // lethal hit. Also evaluates on_damage's threshold crossing (no arguments). Queues threads only, like every
    // engine callback (never runs Lua inside the caller).
    void fireCharacterHitCallbacks(const std::string& victimName, const std::string& attackerName, double oldFraction,
                                   double newFraction, uint32_t flags);
    // Returns true when the event was accepted (a thread was created, or - with no spawner installed - the
    // event was kept in pendingEngineCallbacks()). An empty name or a spawner refusal loses the event silently.
    bool fireEngineCallback(const std::string& callbackName, std::vector<CallbackArg> args, const char* source);

    // Triggers (spec V.4). Called by trigger_enable. Disabling emits on_trigger_exit for a human that is
    // inside; enabling a disabled trigger starts a fresh overlap set (a human already inside enters on the
    // next overlap pass).
    void applyTriggerEnableTransition(const std::string& triggerName, bool enabled);
    void recordTriggerWaitFrame(const std::string& triggerName);

    // Registration order (FIFO, first-seen-wins, no duplicates) of every
    // trigger name runPlaytest()'s own tryAutoRegister() lambda has
    // successfully registered geometry for this session - deterministic
    // "which trigger is the mission most likely waiting on first" ordering
    // that an unordered_map's own iteration order cannot give.
    void noteTriggerRegistrationOrder(const std::string& triggerName);
    // Sec20.14/Sec27.2 port pointer (2026-10-12, FINAL, m22): on a new game, or a harness starting a mission
    // directly, no mission is complete: false unless the host's own state (missionComplete().set) says otherwise.
    bool missionIsComplete(const std::string& name) const;
    // Shared body for guardian_angel_start_zone_pinning / _clear_zone_pinning
    // (Sec53.3): `zoneSetName` empty or absent means "clear only" (the
    // _clear_ entry point). See GuardianAngel::currentZonePinSetName's own
    // doc comment above for the unconditional-release-first shape.
    void guardianAngelStartZonePinning(const std::string& zoneSetName); // .cpp
    // Spec sync e6691f1 (Sec33.4): "mission_conv" active slot starts at 0xff (no conversation), so
    // audio_any_conversation_playing is false until a conversation is played. An explicit .set() wins.
    bool missionConvActive() const;

    // Design notes: src/engine_state_world.cpp, above the definition.
    void setGuardianAngelGroupRegistered(const std::string& name, bool registered);
    bool guardianAngelGroupRegistered(const std::string& name) const;

    // Design notes: src/engine_state_progression.cpp, above the definition.
    void setMissionPhase(int32_t newPhase);

    // Design notes: src/engine_state_progression.cpp, above the definition.
    void bindMissionPhaseMachine(const std::string& name, int32_t type = 0, bool isRestart = false);

    // Design notes: src/engine_state_progression.cpp, above the definition.
    bool missionEndDispatch(const std::string& name, bool successFlag);

    // Design notes: src/engine_state_progression.cpp, above the definition.
    void armMissionEndTransitionTimer();

    // Design notes: src/engine_state_progression.cpp, above the definition.
    void dispatchMissionEndPresentation(bool isSuccess, const std::string& presentationText);

    // Design notes: src/engine_state_progression.cpp, above the definition.
    void beginMissionPhaseTracking(const std::string& name = std::string(), int32_t type = 0,
                                    bool isRestart = false);

    // Design notes: src/engine_state_triggers_weapons.cpp, above the definition.
    void recordTriggerEnable(const std::string& name, bool enabled, int64_t flags);
    bool triggerEnableValue(const std::string& name) const;
    int64_t triggerEnableFlagsValue(const std::string& name) const;

    // Design notes: src/engine_state_triggers_weapons.cpp, above the definition.
    void registerOnTriggerCallback(const std::string& triggerName, const std::string& callbackName);
    const std::string& onTriggerCallbackValue(const std::string& triggerName) const;

    // Design notes: src/engine_state_triggers_weapons.cpp, above the definition.
    void registerOnTriggerExitCallback(const std::string& triggerName, const std::string& callbackName);
    const std::string& onTriggerExitCallbackValue(const std::string& triggerName) const;

    // trigger_get_radius final value (Sec21.13, e6691f1). Cylinder returns the +0x94 field (stored in
    // `length`, the on-disk trigger_shape_param2), per the spec's corrected offsets.
    static double triggerRadiusForGeometry(const TriggerVolumeGeometry& g);
    void registerTriggerGeometry(const std::string& triggerName, const TriggerVolumeGeometry& geometry);
    // Registers geometry for `triggerName` from the loaded zone data (real position + real shape when
    // decodable, else the CHOSEN radius-3 sphere fallback). Returns false when the name does not resolve
    // or has no position property. `note` (optional) receives a one-line description for logging.
    // Shared by the viewer's per-frame auto-register and object_is_in_trigger (Sec1.12: the same geometry
    // serves the containment test and the ENTER/EXIT dispatch).
    bool registerTriggerGeometryFromZoneData(const std::string& triggerName, std::string* note = nullptr);
    const TriggerVolumeGeometry* triggerGeometry(const std::string& triggerName) const;

    // Design notes: src/engine_state_triggers_weapons.cpp, above the definition.
    static bool TriggerVolumeContainsPoint(const TriggerVolumeGeometry& g, double x, double y, double z);

    // Design notes: src/engine_state_triggers_weapons.cpp, above the definition.
    static WorldPosition TriggerVolumeRepresentativePoint(const TriggerVolumeGeometry& g);
    static std::string triggerContainsObjectKey(const std::string& triggerName, const std::string& objectName);
    void registerShopCribTrigger(const std::string& triggerName, ShopCribTriggerKind kind = ShopCribTriggerKind::kShop);
    bool isShopCribTrigger(const std::string& triggerName) const;

    // Design notes: src/engine_state_triggers_weapons.cpp, above the definition.
    const std::string& updateShopCribTriggerPrompt(double playerX, double playerY, double playerZ);

    // "store_common" for a kShop trigger, "store_crib" for a kCrib trigger -
    // see the ShopCribTriggerKind doc comment above for which is REAL vs
    // CHOSEN. Falls back to "store_common" for an unregistered name (should
    // never happen given this is only ever called with a key already present
    // in shopCribTrigger_; never throws regardless).
    std::string openTargetForTrigger(const std::string& triggerName) const;
    // Design notes: src/engine_state_progression.cpp, above the definition.
    void recordMissionSetCompleted(const std::string& name, bool resolved);
    bool missionCompletedValue(const std::string& name) const;

    // Design notes: src/engine_state_world.cpp, above the definition.
    std::string namedHookCallback(const std::string& compositeKey) const;
    void setNamedHookCallback(const std::string& compositeKey, const std::string& callbackName);
    void bossBattleKbEnable(bool enable);
    void bossBattleMarsKillbaneActive(bool active);

    // ===== Ai - data and types in engine_state_ai.h =====
    // Design notes: src/lua_engine_state.cpp, above the definition.
    void applyCharacterSpawnDefaults(const std::string& name, uint32_t hitPoints, double multiplier = 1.0);

    // Design notes: src/lua_engine_npc_ai.cpp, above the definition.
    void enableNpcAi(std::shared_ptr<const sr3airuntime::NpcAiTables> tables, const sr3airuntime::NpcAiConfig& cfg);
    bool npcAiEnabled() const { return npcAi_ != nullptr; }
    sr3airuntime::NpcAiRuntime* npcAi() { return npcAi_.get(); }
    uint64_t npcAiHandleOf(const std::string& name) const;
    // AI-7: cover_node placements (positions) collected by loadZoneObjectsSection and fed to the NPC cover search.
    size_t npcAiCoverNodeCount() const { return npcAiCoverNodes_.size(); }
    void tickNpcAi();
    void beginSyncedActionInstance(const std::string& a, const std::string& b, double durationSeconds = kSyncedActionStandInSeconds);
    void endSyncedActionInstance(const std::string& name);
    // finished(char) of 0x00a3c0c0: no active instance -> true, else playback fraction >= threshold.
    bool syncedActionFinishedFor(const std::string& name, double threshold);
    void registerSyncedActionForTesting(const std::string& name, int32_t index);
    int32_t lookupSyncedActionIndex(const std::string& name) const;

    // Design notes: src/engine_state_world.cpp, above the definition.
    const std::string* groupFirstNpcName(const std::string& groupName) const;
    // Returns the member immediately after currentName in groupName's own
    // registered order, or nullptr if currentName is not a registered member,
    // is the last registered member (wraps back to the head - Sec28.5's own
    // documented stop condition), or the group has no registered members.
    const std::string* groupNextNpcName(const std::string& groupName, const std::string& currentName) const;
    int resolveTeamId(const std::string& name) const;
    bool setTeamRelationIfInBounds(int id1, int id2, int value);
    // Test-only direct read; -2 (never a real relation value this project
    // writes) for an out-of-bounds pair, else whatever was last set (-1 =
    // "never written" default, this project's own sentinel, not a spec fact -
    // the real matrix's own true at-rest contents are OPEN).
    int teamRelationForTesting(int id1, int id2) const;
    void actionSequenceEnd();
    void resetSpawnRegionMaxSpawnDist(); // .cpp: FLT_MAX, CONFIRMED "reset" value
    // ce4556f Sec29 (m19 brutes): also flips any placed Script_Npc member registered as "pending group spawn" to resolvable.
    void markGroupCreated(const std::string& name);
    void registerNotorietyAliasTrackCodeForTesting(const std::string& name, int32_t code);
    int32_t lookupNotorietyAliasTrackCode(const std::string& name) const;
    void registerNotorietyTrackCodeForTesting(const std::string& name, int32_t code);
    int32_t lookupNotorietyTrackCode(const std::string& name) const;
    bool notorietyTrackFlagValue(uint32_t trackBaseAddress) const;
    NotorietySpawnGroupState& getOrCreateNotorietySpawnGroup(const std::string& name);
    int32_t lookupBehaviorSetCode(const std::string& name) const;
    // The id -> name table 0x02623a80 (spec 20.23 blocker m09, FINAL; filled by the static initializer 0x01004970):
    // 0 Playas, 1 Luchadores, 2 Deckers, 3 Morningstar, 4 Neutral Gang, 5 Police, 6 STAG, 7 Civilian, 9 None.
    // An id outside the table (including 8 and the unset 9) reads "None". Compared case-sensitively by scripts.
    static const char* teamNameForId(int id);
    // A zone `script_npc_team` string -> the canonical table name. The loader maps name -> id through 0x00dab330's
    // case-insensitive hash (spec 20.23, 2026-10-15), then reads the id back through the table above; a string not in
    // the table (the one `Brotherhood` record) leaves the unset id 9, which reads "None".
    static const char* canonicalTeamName(const std::string& authored);

    // The character's team byte 0..7 (the table 0x02623a80), 9 when unknown: the authored team name wins, else the
    // currentTeamCode. Shared by the NPC AI host and the downed-state get-up heal (team byte 0 = human side).
    static int teamIdOfCharacter(const CharacterState& c);
    ContinuousSpawnOrder& getOrCreateContinuousSpawnOrder(const std::string& groupName);
    static int32_t continuousSpawnOrderTypeIndex(const std::string& orderTypeName);

    // Design notes: src/engine_state_world.cpp, above the definition.
    void spawnOverrideClearOverrideForHood(const std::string& hood);
    uint64_t despawnAllPedestrians(); // .cpp
    // spec-lua-api-behaviour.md Sec4.13.0 (2026-10-12, FINAL): a light-group name resolves only if a live,
    // registered light group carries it; an unresolved group is a no-op, never an error. This host has no
    // light-group registry, so a name nobody registered is unresolved (false) rather than OPEN.
    bool lightGroupResolve(const std::string& name) const;
    void registerGroupMemberForTesting(const std::string& groupName, const std::string& memberName);
    // Counts how many registered members are NOT dead (CharacterState::
    // isDeadHighConfidence, the SAME already-established "is dead"
    // predicate, reused directly rather than duplicated).
    int32_t groupMembersAliveCount(const std::string& groupName);

    // ===== World - data and types in engine_state_world.h =====
    // Design notes: src/lua_engine_callbacks.cpp, above the definition.
    void tickScriptedOrders();
    // Downed-state expiry (spec-ai-runtime.md Sec21.10.1); run by tickCharacterDeaths before the death finalizer.
    void tickDownedExpiry();
    // Mover destroyed (spec E): HP went from > 0 to 0 -> on_mover_destroyed(mover_name, attacker|nil).
    void notifyMoverDestroyed(const std::string& moverName, const std::string& attackerName);
    LiveObjectKind liveObjectKind(const std::string& name) const;
    bool isLiveObject(const std::string& name) const { return liveObjectKind(name) != LiveObjectKind::kNone; }

    // Design notes: src/engine_state_world.cpp, above the definition.
    void bridgeLiveCharacterPosition(const std::string& name, const WorldPosition& pos);

    // Design notes: src/lua_engine_state.cpp, above the definition.
    uint32_t registerVdoObjectForTesting(const std::string& name, uint32_t parentHandle, uint32_t docHandle);

    // Design notes: src/lua_engine_state.cpp, above the definition.
    uint32_t findVdoObject(const std::string& name, bool hasParent, uint32_t parentHandle,
                           bool hasDoc, uint32_t docHandle) const;

    // --- vint_object_first_child / vint_object_clone (spec-lua-bindings.md
    // Sec18) ------------------------------------------------------------

    // Test/setup-only entry point (see VdoObject::firstChildHandle's own doc
    // comment). No-op if `parentHandle` is not a registered object.
    void setVdoObjectFirstChildForTesting(uint32_t parentHandle, uint32_t childHandle);

    // Design notes: src/lua_engine_state.cpp, above the definition.
    uint32_t cloneVdoObject(uint32_t origHandle, bool hasParentArg, uint32_t parentHandleArg);

    // Test/setup-only: a VdoObject of a registered type, attached as the last
    // child of `parentHandle` (0 = none) and seeded with the element
    // defaults when it is element-family - the same shape loadVintDocument
    // builds, without a file.
    uint32_t registerTypedVdoObjectForTesting(const std::string& name, const std::string& typeName,
                                              uint32_t parentHandle, uint32_t docHandle);

    // Test/setup-only entry point (see this section's own doc comment above
    // on why no in-scope Lua call ever populates this pool).
    void registerScriptedRequestForTesting(const std::string& name, int kind, bool done);

    // The shared status query itself (Sec35.2's own 3-code result, CONFIRMED,
    // corrected polarity), consuming (erasing) the record on a genuine "done"
    // read, exactly like the real engine's `0x006f6380`/`0x006f6040` pair.
    int scriptedRequestStatusCode(const std::string& name, int kind);

    // Design notes: src/engine_state_world.cpp, above the definition.
    bool scriptedRequestCheckDone(const std::string& name, int kind);

    // Design notes: src/lua_engine_state.cpp, above the definition.
    void loadZoneObjectsSection(const sr3zone::ObjectsSection& section);
    WorldPosition worldPositionOr(const OpenValue<WorldPosition>& v, const WorldPosition& fallback);

    // Design notes: src/engine_state_world.cpp, above the definition.
    bool tryResolveWorldPositionFromZoneData(const std::string& name, OpenValue<WorldPosition>& v);
    void recordGetDistCall(const std::string& a, const std::string& b,
                            const WorldPosition& pa, bool knownA,
                            const WorldPosition& pb, bool knownB);

    // Test-only zone-membership fixture (see ZoneMember's own doc comment
    // above for why this registry is test-only).
    void registerZoneMemberForTesting(uint32_t zoneHandle, float weight, bool owned);
    // Test-only direct read, so a test can verify pause_map_stag_takeover_do_reward's
    // own claiming effect (ZoneMember::owned flipping true). Returns nullptr
    // for a zone with no registered members (including zone handle 0).
    const std::vector<ZoneMember>* zoneMembersForTesting(uint32_t zoneHandle) const;
    void gameClockAdvanceFrame(double realSeconds); // .cpp - Sec48.3
    const PlacedObjectState* findPlacedObject(const std::string& name) const;
    bool cityZoneSwapValue(const std::string& name) const;
    bool sidewalkNodesDisabledValue(const std::string& name) const;
    void recordWaypointRemove(int32_t mode);
    void setDrawbridgeAreaDisabled(int32_t area1Based, bool disabled);

    // Design notes: src/lua_engine_state.cpp, above the definition.
    int32_t allocateScriptRequestId(const std::string& objectName, int channel);
    void releaseScriptRequestId(const std::string& objectName, int channel);
    // Read-only test/diagnostic view of the allocator above (wave 2 batch
    // W2-1): whether (object, channel) currently holds a tracked request id,
    // and that id (0 = none). No native reads it - the request-pool QUERY
    // natives (teleport_check_done/move_to_check_done/...) use the separate
    // Sec35.2 pool, which stays on Sec35.6's "minimum correct model".
    bool hasActiveScriptRequest(const std::string& objectName, int channel) const;
    int32_t activeScriptRequestIdOf(const std::string& objectName, int channel) const;
    int32_t allocateMoveToActionId();
    // CONFIRMED exact algorithm (Sec22.30): magnitude = sqrt(x^2+y^2+y^2); near-
    // zero (< 1e-7) or non-finite -> (1,0,0); else each component /= magnitude.
    static void normalizeWindVector(double x, double y, double z, double& outX, double& outY, double& outZ);

    // ingame_effect_remove_trigger (Sec22.17): per-trigger cached "current
    // effect index" (-1 = no effect, CONFIRMED sentinel reused from
    // vehicle_pathfind_check_done's own -1/"no active request" family,
    // Sec9.10/Sec22.3) - a plain per-name map (missing = -1, same default).
    int32_t ingameEffectIndex(const std::string& triggerName) const;

    // ===== Characters - data and types in engine_state_characters.h =====

    // Design notes: src/lua_engine_state.cpp, above the definition.
    void applyCharacterDamage(const std::string& targetName, const std::string& sourceName, float amount,
                              uint32_t flags = 0x300u);

    // Design notes: src/lua_engine_callbacks.cpp, above the definition.
    void tickCharacterDeaths();
    // Review N-7 (2026-10-15): the player's 4 s on_death timer is armed by the death FINALIZER (the lethal sink /
    // character_kill), not by the first per-human update that happens to see the death. No-op for non-players and
    // when already armed.
    void armPlayerDeathTimer(const std::string& name);
    // True when the character reads as dead for the death finalizer.
    static bool characterReadsDead(const CharacterState& c);
    DamageModelResult applyDamageModelHit(const std::string& victimName, const std::string& attackerName, float amount,
                                          uint32_t flags = 0);

    // ---- blocker values answered by spec syncs 727a33f / bfe0542 / 98375ea (src/lua_engine_open_values.cpp) ----
    // Human shield grab start (0x009acb20(holder, hostage), spec Sec13.14 blocker dlc3_m01/m12): writes each
    // party's partner link, sets the hostage's "I am the hostage" bit, clears the holder's, and sets both states to 2.
    void beginHumanShield(const std::string& holderName, const std::string& hostageName);
    // The single-player port rule of character_has_specific_human_shield(char1, char2).
    bool characterHasSpecificHumanShield(const std::string& char1, const std::string& char2);

    // Design notes: src/lua_engine_state.cpp, above the definition.
    CharacterState& getOrCreateCharacter(const std::string& name);
    bool hasCharacter(const std::string& name) const;

    // mission_end_success/mission_end_to_activity/mission_set_cancel_warp_
    // location (Sec6.12/6.20/6.21) - reuse MissionContext (Sec1.5/1.6,
    // below) for their shared "mission is active" gate, now that it exists;
    // see that struct's own new fields (missionEndSuccessDispatchCount etc.)
    // and MissionCancelWarpLocation (further below) for the rest.

    // --- Team-A-cleared batch, spec-lua-api-behaviour.md Sec1.1/1.2/1.4/
    // 1.5/1.6/2.1/2.5/2.6/3.2/3.3/3.5/3.6 (commit 97a9b8d), implemented
    // 2026-10-07/2026-10-08 ------------------------------------------------

    // on_vehicle_destroyed (Sec3.3) / character_is_dead (Sec3.5): the
    // generic/`#PLAYER#` chain's own class-bit gate (`0x00a27e20`: the
    // resolved object's class-descriptor row `0x02cc9900[+0x34]`, byte
    // `+0xa` bit `0x2` - independently established project-wide as the
    // PLAYER CLASS bit, Sec3.3's own 2026-10-07 refutation of a
    // "kind-agnostic" hypothesis, FOUR independent confirmations cited
    // directly in Sec3.3's own text, re-confirmed again by Sec4.13's
    // "type-gated resolver family" cross-function pass: `0x00a27e20` is one
    // of five siblings, each testing one bit of the SAME per-object-type
    // descriptor row). Needed wherever a function's own CONFIRMED text makes
    // a genuinely different observable choice depending on WHICH of the two
    // resolve chains (this player-class-gated one vs. the plain/vehicle-
    // specific fallback, modeled by the flat objectResolves() map below)
    // answered - on_vehicle_destroyed (character-vs-vehicle hook target) and
    // character_is_dead (mission-end-timer override applies only on this
    // branch) are Sec3.3/Sec3.5's own two call sites, but §4.13 confirms
    // `0x00a280c0` (which embeds this exact gate as its own first-tier fast
    // path) is the base resolver reached directly by many more natives this
    // project has since implemented: on_attack_performed (Sec3.12),
    // teleport/teleport_to_object (Sec4.3/Sec22.4), on_ai_do_action_complete
    // (Sec14.7), on_mount (Sec42.3), vehicle_enter_do/vehicle_enter_check_done
    // (Sec38.1/Sec22.3), party_dismiss_do (Sec50.5), character_kill (Sec3.15)
    // and on_vehicle_enter (Sec4.12) all branch on it too (grep
    // playerClassChainResolves( across src/ for the full, current call-site
    // list - this comment is not re-counted mechanically on every edit).
    // Every OTHER in-scope function that also cites "generic/`#PLAYER#`
    // chain then fallback" (on_death, Sec3.2) does the SAME thing regardless
    // of which path resolved, so it keeps using the flat objectResolves()
    // gate alone - this map is deliberately NOT a universal replacement for
    // objectResolves(), only the narrower "which path" fact these functions'
    // own text needs.
    //
    // playerClassChainResolves() (0-arg): the raw per-name override map, for
    // a test to .set()/.forget()/.known() exactly as before - unchanged.
    //
    // playerClassChainResolves(name) (1-arg, below): a test's explicit
    // .set() value always wins when present; otherwise, instead of throwing
    // OpenStateError for every untouched name (this map's ORIGINAL shape,
    // before 2026-10-08's player-class-chain-gate fix), this applies a REAL
    // default now that the bit's own MEANING is spec-confirmed (Sec3.3/
    // Sec4.13 above): TRUE for a character/human-class name, FALSE for a
    // vehicle (the two kinds this bit actually discriminates - no other
    // object kind reaches this particular gate in any call site this
    // project has traced). Classifying an arbitrary mission-script name
    // STRING as character-vs-vehicle is, unlike the bit's meaning, NOT given
    // by any spec - this project's own tooling was checked first and has no
    // reusable answer: tools/lua_all_registered_1490_tagged.txt tags Lua
    // NATIVE FUNCTION names (e.g. "gameplay"/"ui"), not mission OBJECT
    // names; tools/mission_package_per_container.tsv maps scripts to
    // containers, no object-kind column at all; releaseToWorldKind()/
    // onVehicleEnterResolverKind() (both OpenValueMap<int32_t>, Sec4.4/
    // Sec4.12) are a DIFFERENT real mechanism's own per-name classification
    // (confirmed independently, not assumed to share data with this bit) and
    // are, in practice, only ever .set() by tests - never populated from
    // real probe/mission data either; getOrCreateCharacter()/hasCharacter()
    // auto-create on first touch, so they are circular here (this gate is
    // typically the FIRST touch of a name a mission script references).
    // CHOSEN (NOT spec-confirmed - flagged plainly, unlike the bit's own
    // meaning above) heuristic, calibrated directly against this project's
    // own mission-drive probe output (results/verify_probe_blockers_ranked_
    // 20261008_post_missioncontextname/verdict_mission_drive_summary.txt):
    // every character name the probe's own 49-mission corpus hit this gate
    // for ('NPC Pierce', 'npc_zimos', 'npc_pierce', 'char_stag_main_stage
    // 005', 'char_stag_grunt_dead 001', 'Pierce', 'Angel') contains neither
    // signal below; both vehicle names it hit ('debris_vehicle 001', 'Veh CP
    // SJ Spotlight Heli') do, and so do this project's own release_to_world-
    // adjacent vehicle names ('Starting_Vehicles', 'Script_Vehicle 001',
    // Sec4.4) - see looksLikeVehicleNameForPlayerClassGate()'s own doc
    // comment just below for the exact two-part string match.
    // REUSED AGAIN, 2026-10-09 (spec-player-runtime.md Sec7.4a step 7, the
    // vehicle-enter eligibility gate 0x00b0ca60): a SECOND class-row bit on
    // the SAME 0x02cc9900 row, +0xa bit 0x04 ("has the standard AI-state
    // block at all," HIGH CONFIDENCE by analogy to this row's own
    // already-named +0xa bit 0x02 "is-player" test this map already stands
    // in for) - exactly the teleport (Sec4.3) precedent just above: NOT a
    // claim the two bits are the SAME physical bit, only that this project
    // answers both class-row questions with the one map it already has for
    // the purpose. In every real call site this gate reaches, the "character"
    // argument has already resolved as a character (never a vehicle) before
    // the gate runs, so in practice this almost always reads true for a
    // legitimate vehicle-enter attempt - consistent with "has an AI-state
    // block at all" being closer to a baseline capability than a rare
    // exception, and with this project not modelling any non-AI-capable
    // character class.
    OpenValueMap<bool>& playerClassChainResolves() { return playerClassChainResolves_; }

    bool playerClassChainResolves(const std::string& name);
    int32_t lookupAnimationStateId(const std::string& name) const;

    // skydive_set_player_dive_fall_speed (Sec15.18): global 0x0130a8e0,
    // unconditionally overwritten every call; the record-and-replicate
    // call is CONFIRMED host-only (same coopLocalIsHost()-false-in-single-
    // player precedent as persona_clear_global_situation_override above).
    void setPlayerSkydiveFallSpeed(double value);

private:

    // ===== Core / cross-subsystem =====
    std::shared_ptr<sr3airuntime::NpcAiRuntime> npcAi_;          // tickNpcAi (enableNpcAi)
    std::vector<std::array<double, 3>> npcAiCoverNodes_;         // zone cover_node positions (loadZoneObjectsSection)
    void pushNpcAiCoverNodes();
    sr3player::InputSystem inputSystem_;
    sr3player::ControlStateMachine controlStateMachine_;

    // ===== Vint - data and types in engine_state_vint.h =====
    // Typed-path helpers (src/lua_engine_state.cpp).
    void seedVintElementDefaults(uint32_t handle, const std::string& typeName);
    // Numeric components of a stored typed value, or OpenStateError naming
    // what is missing.
    std::vector<double> vintNumbers(uint32_t handle, const char* propertyName, size_t count) const;
    std::vector<double> vintUnscaledSize(uint32_t handle) const;
    VintPosM vintQueryPosM(uint32_t handle) const;
    // Sec5.4 auto_offset applier (vtable +0xc4, 0x00e27530) on the stored
    // auto_offset/unscaled_size - used by loadVintDocument's image path.
    void vintApplyAutoOffset(uint32_t handle);
    std::optional<uint8_t> vintTweenBorrowedCode(uint32_t tweenHandle) const;

    // ===== Cutscene - data and types in engine_state_cutscene.h =====
    ZsceneResolved zsceneResolve(const std::string& name) const;
    TeardownKind zsceneTeardownPlan(int c) const;
    void zsceneTeardownApply(TeardownKind kind, int a, int b);
    ResetPlan zsceneResetPlan() const;
    void zsceneResetApply(const ResetPlan& plan);
    bool zsceneCutsceneGuard() const;
    bool zsceneSoundtrackFinished() const;
    GatePlan zsceneGatePlan(const std::string& name) const;
    void zsceneGateApply(const GatePlan& plan);
    void zscenePromotionStep();
    void zsceneCompletionStep();
    void cutsceneMachineStep();       // 0x0072d660
    void cutsceneSecondStep();        // 0x007258a0
    void cutsceneSetState(int32_t s);

    // Design notes: src/lua_cutscene.cpp, above the definition.
    void cutsceneCameraScriptStep(const std::string& name);
    void screenFadeFrameRoutine();
    bool screenFadeCallUi(const char* name, int nargs, double a0, double a1, double a2, bool* errored);

    // ===== World - data and types in engine_state_world.h =====
    // Sec21.10.1 (FINAL) downed entry: a lethal hit tries the selector first; true = the human is now downed (stance 6)
    // instead of dying. `hpAfter` is the unclamped HP left after the killing hit (<= 0), the spec's +0x1588.
    bool tryEnterDownedOnLethalHit(CharacterState& c, const std::string& targetName, double hpAfter);
    // Damage while downed (stance 6): types 0/2/3/4 ignore it; type 1 runs the overkill buffer. Returns true when the
    // victim is downed (the hit is then fully handled here and must not reach the normal HP path).
    bool absorbDamageWhileDowned(CharacterState& c, const std::string& targetName, double damage);
    // Death of a downed human (0x009a7610 then the finalizer 0x00952760): HP 0, stance 5.
    void killDownedCharacter(CharacterState& c, const std::string& targetName);

    // ===== Characters - data and types in engine_state_characters.h =====
    // The death-transition/ragdoll-or-finalize tail shared by
    // applyCharacterDamage() and applyGuaranteedLethalHit() above (spec-
    // player-runtime.md Sec15.3a) - called once a caller has already
    // established `c`'s health is at/below the zero threshold (or, for a
    // guaranteed lethal hit with no known health, unconditionally).
    void finalizeLethalCharacter(CharacterState& c, const std::string& targetName);
};

// Sets the initial values Team A's specs confirm (src/lua_spec_initial_state.cpp).
// Host's constructor calls it once; slots it does not set stay OPEN.
void applySpecInitialState(EngineState& es);

// ===========================================================================
// Sec32.1/Sec49.2 (spec sync 3ecbf84, 2026-10-09): the shared reader
// `0x00931980(local_player_role, flag_mask)` - the ambient spawn-region
// candidate chooser's own min/max ANNULUS filter, ported directly. NOT the
// near-field (fully-simulated) ambient ped/vehicle population spawners
// (spec-ai-runtime.md Sec17.4c/Sec20, sr3airruntime/near_field_*_
// population.h) - a deliberate scope call, not an oversight: a whole-project
// address/global cross-check (both directions) found ZERO overlap between
// this reader's own addresses/globals (0x00931980, 0x005ed7d0,
// 0x013092ec/0x013092f0) and either near-field spawner's own addresses
// (0x0090b0a0 ped creation; 0x0090f2c0/0x0090ee10 vehicle maintenance) -
// spec-ai-runtime.md never once mentions this reader or its globals, and
// spec-lua-api-behaviour.md's own text names this reader's caller
// (`0x005ed7d0`) an "ambient spawn-region candidate chooser" walking the
// active WORLD-OBJECT table, the SAME "spawn-region" terminology
// `continuous_spawn_start`/`continuous_spawn_stop` (Sec4.7/Sec4.8) already
// use for their own mission-script-registered region table - structurally
// the Lua-scriptable spawn-region family this project's own near-field-
// spawner task explicitly places OUT of scope ("mission-script-driven
// continuous_spawn_start... roadblocks, etc."), not the always-on near-field
// ambient population. This filter is therefore implemented here, in the Lua
// native/engine-state layer where its own sibling max-dist field already
// lives (spawnRegionMaxSpawnDistSquared_ above), not bolted onto either
// near-field spawner file. See scratchpad note left for this task's own
// report for the full cross-check.
// ===========================================================================

// Per-candidate flag-byte bits the min-dist floor's own escape hatch tests
// (Sec32.1: "unless the candidate's flag byte has bit 0x08 set and bit 0x10
// clear"). The MAX cap has no such escape - CONFIRMED, no bit tests it.
inline constexpr uint8_t kSpawnRegionMinDistEscapeBit = 0x08;
inline constexpr uint8_t kSpawnRegionMinDistEscapeSuppressBit = 0x10;

// CONFIRMED per-player test (Sec32.1): `dist² < minSq` -> too close,
// REJECTED, *unless* the escape bits above hold, in which case a secondary
// override check (`0x00da38e0`/`0x00a11e10`, "record-copy + predicate call,
// field-level semantics OPEN" per spec) decides instead; `maxSq < dist²` ->
// too far, REJECTED unconditionally, no escape of any kind.
// `secondaryOverrideAdmits`: the escape's own OPEN downstream predicate -
// CHOSEN default true (same "an OPEN gate defaults to passing" idiom this
// project uses throughout, e.g. sr3airruntime::NearFieldPedSpawnGates::
// difficultyGatePasses) - a caller that independently traces the real
// predicate's field-level meaning supplies it here instead.
inline bool SpawnRegionAnnulusEligibleForPlayer(
    double distSq, float minSpawnDistSquared, float maxSpawnDistSquared, uint8_t candidateFlags,
    const std::function<bool()>& secondaryOverrideAdmits = [] { return true; }) {
    if (distSq > static_cast<double>(maxSpawnDistSquared)) return false;  // too far - unconditional, no escape
    if (distSq < static_cast<double>(minSpawnDistSquared)) {
        const bool escapeHolds = (candidateFlags & kSpawnRegionMinDistEscapeBit) != 0 &&
                                  (candidateFlags & kSpawnRegionMinDistEscapeSuppressBit) == 0;
        if (!escapeHolds) return false;  // too close, no escape
        return secondaryOverrideAdmits();  // too close, but escape holds - secondary check decides
    }
    return true;  // inside the annulus (or NaN distSq - CONFIRMED edge: NaN fails both comparisons above, so it reaches here, matching the setter's own "NaN silently disables the floor" note)
}

// CONFIRMED (Sec32.1): "applying both tests against each" local AND remote
// co-op player, and "candidates passing both tests (against both players,
// when both exist) are collected" - a candidate must be eligible against
// EVERY resolved player position, not merely one.
inline bool SpawnRegionAnnulusEligible(
    const std::vector<double>& distSqToEachPlayer, float minSpawnDistSquared, float maxSpawnDistSquared,
    uint8_t candidateFlags, const std::function<bool()>& secondaryOverrideAdmits = [] { return true; }) {
    for (double distSq : distSqToEachPlayer) {
        if (!SpawnRegionAnnulusEligibleForPlayer(distSq, minSpawnDistSquared, maxSpawnDistSquared, candidateFlags,
                                                  secondaryOverrideAdmits))
            return false;
    }
    return true;
}

} // namespace sr3luahost
