// EngineState slice: audio, radio, conversation, persona lines, playlists.
//
// Split out of engine_state.h (refactor step 3a, 2026-10-17) as a zero-behaviour-change move: every
// type, field, initialiser and comment below moved verbatim (namespace-scope types re-indented one
// level). Types that were nested in EngineState are now namespace-scope types in sr3luahost;
// EngineStateAudioData re-exports each one under its old name, so `EngineState::Type` still
// resolves. EngineState inherits EngineStateAudioData publicly, so `state.member`,
// `EngineState::kConstant` and `state.accessor()` are unchanged for every caller. Members that
// were private in EngineState are `protected` here (the only access change). Method
// declarations stay in EngineState (engine_state.h); bodies are in src/*.cpp.
#pragma once

#include "sr3luahost/engine_state_common.h"

namespace sr3luahost {

// Sec32.7 `radio_set_station`: per-vehicle(-or-character) radio state.
// `hasRadio`/`stationCount`/`stationRefused` are all OPEN (no real per-
// vehicle radio-table data this project parses); `currentStation` is
// this project's own plain tracked field (0 = "no station tuned").
struct VehicleRadio {
    OpenValue<bool> hasRadio{"vehicle has a radio", "spec-lua-api-behaviour.md Sec32.7"};
    OpenValue<int32_t> stationCount{"vehicle radio station count", "spec-lua-api-behaviour.md Sec32.7"};
    OpenValueMap<bool> stationRefused{"vehicle radio station refused, by station number",
                                      "spec-lua-api-behaviour.md Sec32.7"};
    int32_t currentStation = 0; // 0 = "no station tuned" (this project's own choice; stations are CONFIRMED 1-based)
    uint64_t recordSentCount = 0;
};

// audio_play_for_mission_cellphone (Sec12.2, 0x00a3daa0): CONFIRMED
// registers up to two named Lua callbacks (args 4/5) into their own two
// hook slots on a per-cellphone-audio-record basis, reusing the SAME
// callback-name->hook-reference primitive (0x00a1fcc0) the character
// event-hook family uses elsewhere in this project, but on a NEW record
// kind this project has no other state for. The record-allocation
// internals (0x00a38f30) and the exact real-world role of each hook
// slot are OPEN - only the two named-callback writes are tracked, keyed
// by the cellphone-audio identifier string (arg 1).
struct MissionCellphoneAudio {
    std::string hookSlot1; // arg 4, "" = none registered
    std::string hookSlot2; // arg 5, "" = none registered
    // Call lifecycle (spec V.3). `live` from audio_play_for_mission_cellphone until the call ends or the script
    // removes it; cb1 fires once on answer, cb2 on the end of the call. Neither is fired by the removal native.
    bool live = false;
    bool answered = false;
    bool ended = false;
    bool removed = false;
    double personaHandle = 0.0; // arg 6 (spec V.3 correction: the audio_persona_load_2d handle)
};

// --- radio_set_sing_along_track / radio_clear_sing_along_track
// (Sec51.3) - a real cross-machine set/clear divergence ------------
// CONFIRMED: "song index 0 can be set... but can never be cleared from
// script (the clear function explicitly rejects index 0)," and "the
// clear also leaves a separate 'current sing-along index' global
// stale rather than resetting it." currentIndex below models ONLY the
// local-apply half (the spec's own explicit "the second [network]
// argument is never actually read by the local-apply logic at all" -
// the cross-machine divergence this tranche documents is a NETWORK-
// layer effect this single-process host has no second machine to
// exhibit, so it is not fabricated here, only the local half is).
struct SingAlongTrack {
    int32_t currentIndex = -1; // -1 = none (CHOSEN initial; file-default not stated)
};

// --- Sec51.2/Sec51.3 playlist_play_track/_stop_track: CONFIRMED null-
// read crash on an out-of-range/no-songs-loaded index - hardened here.
struct PlaylistState {
    OpenValue<uint32_t> loadedSongCount{"playlist loaded-song count", "spec-lua-api-behaviour.md Sec51.2"};
    int32_t currentTrack = -1;
    bool playing = false;
    uint64_t nullSongGuardCount = 0;
    uint64_t stopCount = 0;
};

struct EngineStateAudioData {
    // Former EngineState nested types (defined above at namespace scope), re-exported so
    // `EngineState::Type` keeps naming them.
    using VehicleRadio = sr3luahost::VehicleRadio;
    using MissionCellphoneAudio = sr3luahost::MissionCellphoneAudio;
    using SingAlongTrack = sr3luahost::SingAlongTrack;
    using PlaylistState = sr3luahost::PlaylistState;

public:

    // game_UI_audio_play (Sec2.2): "Returns the resulting voice/play-
    // instance handle as a Lua number" - the spec confirms a handle is
    // always returned (regardless of whether a sound argument resolved)
    // but not the real engine's own numeric scheme. This project's own
    // chosen stand-in: a plain, monotonically-incrementing counter
    // starting at 1, matching this project's own established convention
    // for an unconfirmed-but-plausible handle shape (see
    // ThreadScheduler::newThread's identical choice, thread_scheduler.h).
    int64_t nextAudioVoiceHandle() { return nextAudioVoiceHandle_++; }

    // audio_set_listener_override / audio_clear_listener_override
    // (Sec33.4): CONFIRMED - both always store an id (clear stores the
    // null id) and always send a record, no authority gate - this
    // project's own no-op replicateStateChange stand-in models the
    // "always sends" record. The per-frame position/orientation snapshot
    // and listener substitution are NOT modeled (no position/camera data
    // anywhere in this project, stated project-wide simplification).
    std::string& audioListenerOverrideTarget() { return audioListenerOverrideTarget_; }
    OpenValueMap<bool>& conversationChannelActive() { return conversationChannelActive_; }
    VehicleRadio& vehicleRadio(const std::string& name) { return vehicleRadios_[name]; }

    // radio_newsbreak_clear (Sec51.3, tranche 17). CONFIRMED only as "a
    // straightforward local-only state change[...], no further crash
    // shapes" - no further detail given, so this is modeled the same
    // minimal way as the Sec46.3 write-only flags above: one boolean,
    // write-only in this project's scope (no in-scope Lua getter reads it
    // back; a test can).
    OpenValue<bool>& radioNewsbreakActive() { return radioNewsbreakActive_; }

    // --- Native-implementation batch 2026-10-07 (fresh mission-drive stub-
    // hit ranking, cross-referenced against spec coverage) - see each
    // accessor's own doc comment and lua_spec_confirmed_stubs.cpp's own
    // batch header comment for the full per-function citation/reasoning. ---

    // audio_object_post_event (Sec2.4, individually executable-re-derived
    // and cleared by Sec60.3 despite sitting in the manager's own Sec1-5
    // "not yet cleared for implementation" freeze - Sec60.3's own text:
    // "re-verified with their prior OPEN items closed... no corrections
    // needed beyond that"). This project models no real Wwise ID table
    // anywhere (same stated gap game_audio_get_audio_id/Sec8.22 already
    // documents) - rather than throw on every real call (which would
    // regress every mission that currently passes this call silently,
    // unlike game_audio_get_audio_id whose whole job IS the id), this
    // follows game_UI_audio_play's own precedent (Sec2.2): treat every
    // event name as "resolves" and hand back a fresh instance handle from
    // the SAME shared counter, rather than modelling Wwise resolution
    // failure at all. CHOSEN simplification, not a guess at a specific
    // Wwise id.
    int64_t nextEffectPostHandle() { return nextAudioVoiceHandle(); }

    // audio_persona_line_playing (Sec8.4, 2026-10-07 batch): resolves via
    // the SAME shared objectResolves() map audio_play_persona_line
    // (Sec2.5) also uses; CONFIRMED a null/unresolved handle reads as
    // false "by convention" (not an error), so only the RESOLVED case's
    // own "is it actually playing" predicate is genuinely OPEN until a
    // test sets it (0x0094c9e0/0x0046afd0, neither decompiled).
    OpenValueMap<bool>& personaLinePlaying() { return personaLinePlaying_; }
    void setAudioPlaying(int id, bool playing) { audioPlaying_[id] = playing; }

    // door_open (Sec1.10): resolved through the SAME named-placed-object
    // registry door_close/door_lock/mesh_mover_* already share
    // (getOrCreatePlacedObject() above) - see PlacedObjectState's own new
    // door_open fields for the per-door state this native reads/writes.

    // audio_conversation_play (Sec2.10): see ConversationPlay's own doc
    // comment above for the full reasoning.
    ConversationPlay& conversationPlay() { return conversationPlay_; }

    // =====================================================================
    // Batch 2026-10-08 (Batch 7, dedicated agent - spec-lua-api-behaviour.md
    // Sec30, "ranking tranche 03", the 16 of its 25 names NOT already wired
    // by lua_spec_confirmed_stubs_b03_vintui_r03_r05.cpp's own 9). See each
    // stub's own doc comment (lua_spec_confirmed_stubs_b14_batch7_
    // tranche03.cpp) for the full per-function citation. The shared
    // vehicle-resolver/force-flag infrastructure above (objectResolves(),
    // getOrCreateVehicle(), VehicleState::forceFlagGatePasses,
    // applyVehicleForceFlagSetter in lua_spec_confirmed_stubs_common.h) is
    // REUSED as-is, not re-derived - see this batch's own stub file header
    // for which of b03's 9 siblings each new function was sanity-checked
    // against.
    // =====================================================================

    // --- audio_ambient_emitter_pause / audio_ambient_emitter_resume
    // (Sec30.3) ------------------------------------------------------------
    //
    // This project has no real world-object-manager ambient-emitter list
    // (global 0x03171a64, same "no world objects in this host" precedent
    // already established for several other Sec28/Sec31 fields) - same
    // never-populated-registry convention as VdoObject/VintDataResponder-
    // Record: a real script's call against any name this host has never
    // had registered honestly takes the real, CONFIRMED "no match -> do
    // nothing" path, which is itself correct behavior for a never-
    // populated list, not a simplification of the pause/resume contract.
    void registerAmbientEmitterForTesting(const std::string& name) { ambientEmitterNames_.insert(name); }
    bool ambientEmitterKnown(const std::string& name) const { return ambientEmitterNames_.count(name) != 0; }
    std::unordered_map<std::string, bool>& ambientEmitterPaused() { return ambientEmitterPaused_; }
    int32_t personaGlobalSituationOverride() const { return personaGlobalSituationOverride_; }
    uint64_t personaGlobalSituationOverrideResetCount() const { return personaGlobalSituationOverrideResetCount_; }

    // ai_set_personality (Sec13.27): see CharacterState::
    // aiPersonalityProfileCode above for the per-character write; this is
    // the name->code test-registration hook (same "never-populated object
    // space" precedent as lookupNotorietyTrackCode/lookupSyncedActionIndex
    // above), sentinel 0x14 (20) the CONFIRMED real fallback.
    static constexpr int32_t kAiPersonalityProfileFallback = 0x14;

    // audio_conversation_playing (Sec14.22): a GENUINELY DIFFERENT mechanism
    // from audio_any_conversation_playing/conversationChannelActive above
    // (Sec33.4's own per-session-channel flag) - this one walks a linked
    // list of SLOT-INDEXED conversation entries (0x006de990) or queries one
    // slot directly (0x00877dc0). Kept as its own, separately-keyed map -
    // same near-duplicate caution this header already applies elsewhere
    // (e.g. CharacterState::isAlive vs. isDeadHighConfidence).
    OpenValueMap<bool>& conversationSlotPlaying() { return conversationSlotPlaying_; }

    // game_audio_stop (Sec11.7, 0x00843ac0): CONFIRMED the ENTIRE function
    // has no Lua-visible effect this project can model - the real worker
    // only mutates a throwaway local copy of the id argument and two
    // internal status bits of a secondary/legacy audio-instance mechanism
    // this project builds no registry for anywhere (same "no in-scope
    // consumer" class as several audio natives elsewhere in this project).
    // Call-count only, for test observability.
    uint64_t gameAudioStopCount() const { return gameAudioStopCount_; }
    void recordGameAudioStop() { ++gameAudioStopCount_; }
    MissionCellphoneAudio& missionCellphoneAudio(const std::string& id) { return missionCellphoneAudio_[id]; }
    const std::unordered_map<std::string, MissionCellphoneAudio>& missionCellphoneAudioMap() const { return missionCellphoneAudio_; }

    // radio_block (Sec12.20, 0x00a5c290): CONFIRMED two-tier resolve - tried
    // first as a vehicle directly, then (on failure) as a character,
    // following their cached vehicle association (CharacterState::
    // currentVehicleName, the SAME field get_char_vehicle_name/Sec12.9
    // reads - "a confirmed reuse of the character's cached-vehicle-
    // association fields between two of this tranche's own functions").
    // This project's flat objectResolves() map cannot structurally
    // distinguish "is this name a vehicle" from "is this name a character"
    // the way the real two-tier resolver does - see
    // stub_radio_block's own doc comment for the CHOSEN tie-break (an
    // already-materialized VehicleState record, or no CharacterState record
    // at all, is read as "tier 1: a vehicle name directly"). The terminal
    // dispatch (0x00556680) is OPEN - only which vehicle got blocked is
    // tracked.
    std::unordered_set<std::string>& radioBlockedVehicles() { return radioBlockedVehicles_; }
    void recordRadioBlock(const std::string& vehicleName) { radioBlockedVehicles_.insert(vehicleName); }
    bool radioBlocked(const std::string& vehicleName) const { return radioBlockedVehicles_.count(vehicleName) != 0; }
    int64_t personaSuppressAmbientLinesCounter() const { return personaSuppressAmbientLinesCounter_; }

    // radio_newsbreak (Sec24.17): CHOSEN - the real hash/lookup helper
    // (0x00462960) is OPEN, so the raw newsbreak name itself is recorded,
    // honestly, standing in for "whatever resolved id results."
    std::string& currentRadioNewsbreak() { return currentRadioNewsbreak_; }
    void setRadioStationForVehicle(const std::string& name, int32_t station) { radioStationByVehicle_[name] = station; }

    // --- radio_set_sing_along_allowed_during_missions (Sec40.2) -----------
    // Bare boolean, mission-scoped synced variable "mission_sing_alongs" -
    // CHOSEN false initial (no CONFIRMED default; mission-reset mechanism
    // out of this batch's scope).
    bool& singAlongAllowedDuringMissions() { return singAlongAllowedDuringMissions_; }
    SingAlongTrack& singAlongTrack() { return singAlongTrack_; }
    // Song-name -> index resolver: this project has no real shipped radio
    // song table (same "no real data table" gap as elsewhere, e.g.
    // vcust's own customization-item tables) - OPEN until a test registers
    // a name; a real call with an unregistered song name honestly refuses
    // rather than fabricating an index.
    OpenValueMap<int32_t>& songNameToIndex() { return songNameToIndex_; }
    void setSingAlongTrack(int32_t songIndex) { singAlongTrack_.currentIndex = songIndex; }

    // --- radio_unblock (Sec51.3) -------------------------------------------
    // Inverse of the already-registered radio_block (radioBlockedVehicles()
    // above, Sec12.20) - reused directly, no new state.
    void recordRadioUnblock(const std::string& vehicleName) { radioBlockedVehicles_.erase(vehicleName); }

    // --- audio_suppress_ambient_player_lines (Sec45.6) --------------------
    // Host-only write, session-synced variable "persona_supress", "no
    // direct engine reader found anywhere." CHOSEN false initial.
    bool& audioSuppressAmbientPlayerLines() { return audioSuppressAmbientPlayerLines_; }

    // --- audio_play_do / audio_play_id_do / audio_play_id_for_navpoint_do
    // (event-based, 3 of 7) / audio_play_for_character_do / audio_play_
    // for_character_weapon_do / audio_play_for_mover_do / audio_play_id_
    // for_character_do (object-sound, 4 of 7) (Sec59.6) -------------------
    // CONFIRMED: "the other four... return a monotonically-incrementing
    // engine serial counter" - reuses the EXISTING nextAudioVoiceHandle()
    // shared counter directly (already reused by nextEffectPostHandle()
    // elsewhere), not a new counter. CONFIRMED: "three event-based
    // bindings... return a result that can be left uninitialized on the
    // stack if audio itself was never initialized." HARDENED (host-safety):
    // this host returns the documented failure value 0 instead (CHOSEN -
    // no specific numeric failure value is given for this event-based
    // trio by name; 0 reuses "the rest" convention the object-sound
    // bindings' own failure shape already establishes, Sec59.6/59.8) -
    // rather than reproducing an uninitialized stack read.
    bool& audioSystemInitialized() { return audioSystemInitialized_; } // CHOSEN true initial; a test flips it false
                                                                        // to exercise the hardened path above
    uint64_t& audioEventUninitializedGuardCount() { return audioEventUninitializedGuardCount_; }
    int64_t allocateAudioSerial() { return nextAudioVoiceHandle(); }

    // --- Sec26.17 game_audio_play: a name/id "resolves" through the same
    // never-populated-registry convention this project already uses for
    // Wwise-backed names (ambientEmitterKnown) - a name this host has never
    // had registered honestly takes the real CONFIRMED "no match" path. A
    // raw numeric id always resolves (no hash lookup needed).
    bool audioEventKnown(const std::string& name) const { return audioEventRegistry_.count(name) != 0; }
    void registerAudioEventForTesting(const std::string& name) { audioEventRegistry_.insert(name); }
    PlaylistState& playlist() { return playlist_; }

    // --- Sec57.4 cell_playlist_save_list: this batch's SECOND own named
    // memory-safety finding - drop out-of-range song ids, clamp a negative
    // table count to 0 before use (both §57's own fix recommendations).
    OpenValue<uint32_t>& radioSongTableCount() { return radioSongTableCount_; }
    uint64_t& playlistSaveOutOfRangeDroppedCount() { return playlistSaveOutOfRangeDroppedCount_; }
    uint64_t& playlistSaveNegativeCountClampedCount() { return playlistSaveNegativeCountClampedCount_; }
    // CHOSEN (hardening, adversarial-review follow-up, 2026-10-08): a very large
    // positive table count (e.g. 2^31-1) is no longer a memory-safety issue once
    // the negative-count underflow above is closed, but it is still a multi-
    // second hang/DoS shape - bounded at 65,535 (the real engine's own 16-bit
    // narrowing bug's accidental cap) in cell_playlist_save_list. Counted here.
    uint64_t& playlistSaveCountCappedCount() { return playlistSaveCountCappedCount_; }
    std::vector<std::vector<int32_t>>& savedPlaylists() { return savedPlaylists_; }

    // --- Sec55.3 game_audio_set_rtpc ----------------------------------------
    uint64_t& audioRtpcAppliedCount() { return audioRtpcAppliedCount_; }

    // --- player_get_custom_voice (§51.4 CRASH). CONFIRMED the resolver
    // result is never checked before being dereferenced. HARDENED (CHOSEN
    // divergence): refuses (returns the CHOSEN "no value" sentinel -1,
    // this project's own pervasive "no value" convention, rather than
    // crash) on an unresolved player name, counted below; a resolved name
    // reads a settable per-player voice id, CHOSEN -1 default.
    std::unordered_map<std::string, int32_t>& customVoiceIds() { return customVoiceIds_; }
    uint64_t playerGetCustomVoiceUnresolvedGuardCount() const { return playerGetCustomVoiceUnresolvedGuardCount_; }
    void recordPlayerGetCustomVoiceUnresolvedGuard() { ++playerGetCustomVoiceUnresolvedGuardCount_; }

    // radio_global_block (Sec18.24): a single global byte - distinct from
    // radio_block's own per-vehicle scoping (Sec12.20).
    bool& radioGlobalBlocked() { return radioGlobalBlocked_; }

    // persona_set_global_situation_override (Sec17.21): the SAME global
    // 0x0150447c persona_clear_global_situation_override (Sec15.7, wave-1
    // Batch 5) already models as personaGlobalSituationOverride_ above -
    // reused directly, not duplicated (this is its own first real writer of
    // an arbitrary value; Sec15.7's own method only ever resets it to 0). A
    // 9-value enum [1,9], range-checked (values outside silently ignored),
    // network-replicated only inside a coordinated co-op session (reuses
    // coopSession().present directly - the SAME real singleton, 0x0087ba20 -
    // CHOSEN false by default per the project's single-player-only stance,
    // so this branch is dead unless a test installs a session).
    void setPersonaGlobalSituationOverride(int32_t v) { personaGlobalSituationOverride_ = v; }

    // persona_set_dlc_bundle (Sec17.22): only acts if the value differs
    // from the cached global - change-detection modeled directly via a
    // plain comparison at the call site. CHOSEN 0 = "never set".
    int32_t& personaDlcBundle() { return personaDlcBundle_; }

protected: // private in EngineState before the split
    int64_t nextAudioVoiceHandle_ = 1;
    std::string audioListenerOverrideTarget_;
    OpenValueMap<bool> conversationChannelActive_{"per-session conversation channel active slot",
                                                   "spec-lua-api-behaviour.md Sec33.4"};
    std::unordered_map<std::string, VehicleRadio> vehicleRadios_;
    OpenValue<bool> radioNewsbreakActive_{"radio_newsbreak_clear global byte", "spec-lua-api-behaviour.md Sec51.3"};

    // --- Section 8 batch (2026-10-07, full-document spec sweep) - see each
    // public accessor's own doc comment above for citation/reasoning. ---
    OpenValueMap<bool> personaLinePlaying_{"audio persona line currently playing (0x0094c9e0/0x0046afd0)",
                                            "spec-lua-api-behaviour.md Sec8.4"};
    std::unordered_map<int, bool> audioPlaying_; // Sec8.3/Sec8.9
    ConversationPlay conversationPlay_;

    // --- Batch 2026-10-08 (Batch 7, dedicated agent), spec-lua-api-
    // behaviour.md Sec30 ranking tranche 03, the 16 names not already
    // wired by b03 - see each public accessor's own doc comment above for
    // citation/reasoning. ------------------------------------------------
    std::unordered_set<std::string> ambientEmitterNames_;         // Sec30.3, test-populated only
    std::unordered_map<std::string, bool> ambientEmitterPaused_;  // Sec30.3
    int32_t personaGlobalSituationOverride_ = 0;          // Sec15.7
    uint64_t personaGlobalSituationOverrideResetCount_ = 0;
    OpenValueMap<bool> conversationSlotPlaying_{"audio_conversation_playing per-slot active flag (0x00877dc0)",
                                                "spec-lua-api-behaviour.md Sec14.22"};
    uint64_t gameAudioStopCount_ = 0;
    std::unordered_map<std::string, MissionCellphoneAudio> missionCellphoneAudio_;
    std::unordered_set<std::string> radioBlockedVehicles_;
    int64_t personaSuppressAmbientLinesCounter_ = 0;
    std::string currentRadioNewsbreak_;
    std::unordered_map<std::string, int32_t> radioStationByVehicle_;
    bool singAlongAllowedDuringMissions_ = false;
    SingAlongTrack singAlongTrack_;
    OpenValueMap<int32_t> songNameToIndex_{"radio song-name -> index resolver (no real song table here)",
                                           "spec-lua-api-behaviour.md Sec51.3"};
    bool audioSuppressAmbientPlayerLines_ = false;
    bool audioSystemInitialized_ = true;
    uint64_t audioEventUninitializedGuardCount_ = 0;
    std::unordered_set<std::string> audioEventRegistry_;
    PlaylistState playlist_;
    OpenValue<uint32_t> radioSongTableCount_{"radio song table count", "spec-lua-api-behaviour.md Sec57.4"};
    uint64_t playlistSaveOutOfRangeDroppedCount_ = 0;
    uint64_t playlistSaveNegativeCountClampedCount_ = 0;
    uint64_t playlistSaveCountCappedCount_ = 0;
    std::vector<std::vector<int32_t>> savedPlaylists_;
    uint64_t audioRtpcAppliedCount_ = 0;
    std::unordered_map<std::string, int32_t> customVoiceIds_;
    uint64_t playerGetCustomVoiceUnresolvedGuardCount_ = 0;
    bool radioGlobalBlocked_ = false;
    int32_t personaDlcBundle_ = 0;
};

} // namespace sr3luahost
