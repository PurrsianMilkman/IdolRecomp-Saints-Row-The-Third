// EngineState method definitions moved verbatim out of include/sr3luahost/engine_state.h
// (structural refactor, step 1b: no behaviour change). Area: hud_ui.

#include "sr3luahost/engine_state.h"

namespace sr3luahost {

// Design notes moved verbatim from EngineState::cutscenePlayCheckDone's declaration in engine_state.h
// (refactor step 3a):
// CONFIRMED (Sec57.2): "treats 'no manager' as equivalent to
// 'finished'" - this host never allocates cutsceneManager_.present
// (the allocation path itself is OPEN/HYPOTHESIS, not modeled, see
// above), so this honestly and permanently reproduces the documented
// "reports done prematurely" bug rather than a simplification of it
// (same "minimum correct model" precedent as Sec35.6's scripted-
// request pool).
//
// No longer purely a query (2026-10-11, the m07 end-of-cutscene-
// placement task): when the done-check reports true, it now also fires
// cutscenePlayApplyEndPlacement() below (idempotent per destination
// write - see cutscenePlayPlacementApplied_'s own doc comment) -
// non-const for exactly this reason.
bool EngineState::cutscenePlayCheckDone() {
        const bool done = !(cutsceneManager_.present.known() && cutsceneManager_.present.get());
        if (done) cutscenePlayApplyEndPlacement();
        return done;
    }

const sr3cutscene::CameraScript* EngineState::cutsceneActiveCameraScript() const {
        return cutsceneCameraScript_ ? &*cutsceneCameraScript_ : nullptr;
    }

// Design notes moved verbatim from EngineState::audioPlaying's declaration in engine_state.h
// (refactor step 3a):
// audio_is_playing/audio_stop (Sec8.3/Sec8.9, 2026-10-07 batch): both
// key off the SAME fixed 32-slot audio-instance table - this project
// builds no registry for it (no in-scope function ever marks a
// numeric id "live"), so the honest, CONFIRMED-by-construction answer
// for every id is "not playing" (the same "a registry this project
// deliberately does not build is itself the confirmed fallback"
// reasoning vehicle_pathfind_check_done/group_create_check_done
// already establish) - a plain default-false map, not OpenValueMap,
// since there is no separate "resolve" step for a bare numeric id to
// genuinely refuse on.
bool EngineState::audioPlaying(int id) const {
        auto it = audioPlaying_.find(id);
        return it != audioPlaying_.end() && it->second;
    }

// Design notes moved verbatim from EngineState::hudBarSlot's declaration in engine_state.h
// (refactor step 3a):
// hud_bar_off/hud_bar_on/hud_bar_set_value (Sec8.14/8.15/8.16): fixed
// 4-slot array (0x022b6828, CONFIRMED "0-3" id range, Sec8.14). A
// script-controlled id outside that range would index past this fixed
// array on the real engine too (no bounds check is documented at any
// of these 3 call sites) - HARDENED here: an out-of-range id is
// refused (nullptr) rather than read/written OOB; see
// hudHardeningCounters() below for the per-site firing counts
// (HARDENING POLICY).
HudBarSlot* EngineState::hudBarSlot(int64_t id) {
        if (id < 0 || id >= kHudBarSlotCount) return nullptr;
        return &hudBarSlots_[static_cast<size_t>(id)];
    }

HudTimerSlot* EngineState::hudTimerSlot(int64_t id) {
        if (id < 0 || id >= kHudTimerSlotCount) return nullptr;
        return &hudTimerSlots_[static_cast<size_t>(id)];
    }

// Design notes moved verbatim from EngineState::recordRefractionSituationClear's declaration in engine_state.h
// (refactor step 3a):
// --- Batch 5 (ranks 251-350, spec-lua-api-behaviour.md Sec15/Sec16),
// 2026-10-08 - HUD-slot/customization/pause-menu cluster. See
// lua_spec_confirmed_stubs_b12_batch5_hud_customization.cpp's own doc
// comment above each stub_* function for the full citation/reasoning. ---

// refraction_situation_clear (Sec15.5): bit 0x1 of the flag arg (default
// 3) zeroes slot 0 of the global refraction-override array (CONFIRMED,
// no stated prior value - CHOSEN "off" semantics); bit 0x2 opens an
// unconditional (not host-gated, per Sec15.5's own text) record-and-
// replicate. Both can fire on the same call (default value 3 sets
// both bits).
void EngineState::recordRefractionSituationClear(int64_t flags) {
        if (flags & 0x1) { refractionGlobalOverrideCleared_ = true; ++refractionClearLocalCount_; }
        if (flags & 0x2) { EngineState::replicateStateChange("refraction_situation_clear", ""); ++refractionClearReplicateCount_; }
    }

void EngineState::qteCleanup() {
        for (auto& e : qteActiveSlots_) {
            if (e.linkedHudSlotId >= 0) hudDisplayRemoveState(e.linkedHudSlotId, false);
        }
        qteActiveSlots_.clear();
        ++qteCleanupCount_;
    }

void EngineState::qteStartActivate(const QteSlotEntry& entry) {
        for (auto& e : qteActiveSlots_) {
            if (!e.characterName.empty() && e.characterName == entry.characterName) {
                e = entry;
                return;
            }
        }
        qteActiveSlots_.push_back(entry);
    }

// Design notes moved verbatim from EngineState::personaClearGlobalSituationOverride's declaration in engine_state.h
// (refactor step 3a):
// persona_clear_global_situation_override (Sec15.7): CONFIRMED
// unconditional reset of the global override value to 0; the
// record-and-replicate call is CONFIRMED host-only (coopLocalIsHost(),
// CONFIRMED false in single player, same precedent as
// audio_conversation_end Sec8.2) - structurally reproduced via the gate
// below even though it never fires in this project's scope.
void EngineState::personaClearGlobalSituationOverride() {
        personaGlobalSituationOverride_ = 0;
        ++personaGlobalSituationOverrideResetCount_;
        if (coopLocalIsHost()) EngineState::replicateStateChange("persona_clear_global_situation_override", "");
    }

void EngineState::recordPauseMenuUpdateOption(int32_t category, int32_t optionId, bool flag, double value) {
        auto& rec = pauseMenuOptionRequests_[{category, optionId}];
        rec.optionId = optionId;
        rec.flag = flag;
        rec.value = value;
        ++rec.count;
    }

const EngineState::PauseMenuOptionRequest* EngineState::pauseMenuOptionRequest(int32_t category, int32_t optionId) const {
        auto it = pauseMenuOptionRequests_.find({category, optionId});
        return it != pauseMenuOptionRequests_.end() ? &it->second : nullptr;
    }

uint64_t EngineState::pauseMenuRestoreDefaultsCount(int32_t category) const {
        auto it = pauseMenuRestoreDefaultsCounts_.find(category);
        return it != pauseMenuRestoreDefaultsCounts_.end() ? it->second : 0;
    }

void EngineState::recordHudPrompt(const std::string& target, const std::string& a2, const std::string& a3) {
        hudPromptRequests_.push_back({target, a2, a3});
        hudPromptActive_ = true; // 0x00a370c0 stores a nonzero prompt id into 0x02703cac ("prompt active")
        hudPromptCallbackName_ = a3; // slot 9 (local) interned callback name, 0x005e46b0
    }

void EngineState::recordHudPromptDefaultReset() {
        ++hudPromptDefaultResetCount_;
        hudPromptActive_ = false; // 0x00a36e60(0): zeroes 0x02703cac, clears slots 9/10
        hudPromptCallbackName_.clear();
    }

std::string EngineState::takeHudPromptCallbackOnInteract() {
        if (!hudPromptActive_) return std::string();
        std::string name = std::move(hudPromptCallbackName_);
        hudPromptActive_ = false;
        hudPromptCallbackName_.clear();
        return name;
    }

// Design notes moved verbatim from EngineState::internHudSecondaryKey's declaration in engine_state.h
// (refactor step 3a):
// 0x00a1fcc0's own real mechanism (a ~350-slot fixed name->id registry
// with insertion-on-miss) IS modelled exactly, by sequential-id
// assignment on first use - the registry's own real starting id/base is
// not given, so this project's own monotonic counter starts at 1
// (CHOSEN, same "a fresh monotonically-increasing handle stands in for
// the real packed identifier" convention as nextVisualEffectHandle
// above).
int32_t EngineState::internHudSecondaryKey(const std::string& key) {
        auto it = hudCriticalTimerSecondaryKeyIds_.find(key);
        if (it != hudCriticalTimerSecondaryKeyIds_.end()) return it->second;
        int32_t id = nextHudCriticalTimerSecondaryKeyId_++;
        hudCriticalTimerSecondaryKeyIds_[key] = id;
        return id;
    }


int32_t EngineState::lookupAiPersonalityProfileCode(const std::string& name) const {
        auto it = aiPersonalityProfileCodes_.find(name);
        return it != aiPersonalityProfileCodes_.end() ? it->second : kAiPersonalityProfileFallback;
    }

void EngineState::personaSuppressAmbientLinesAdjust(bool suppress) {
        if (suppress) ++personaSuppressAmbientLinesCounter_;
        else if (personaSuppressAmbientLinesCounter_ > 0) --personaSuppressAmbientLinesCounter_;
    }

bool EngineState::clearSingAlongTrack(int32_t songIndex) { // false = rejected (index 0), matching the real native's own refusal
        if (songIndex == 0) return false;
        // CONFIRMED: clearing does NOT reset currentIndex (left stale) -
        // deliberately a no-op on currentIndex itself; only a separate
        // "is a sing-along currently selected" bit (not read by any
        // in-scope native) would actually change, so nothing is written.
        return true;
    }

void EngineState::setDialogSlotShowingIdForTesting(int slot, int32_t id) {
        if (slot >= 0 && slot < 4) dialogUi_.slotShowingId[static_cast<size_t>(slot)] = id;
    }

} // namespace sr3luahost
