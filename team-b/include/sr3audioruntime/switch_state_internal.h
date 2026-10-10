#pragma once

// sr3audioruntime - Switch/State internal apply (spec-audio-runtime.md
// Sec10.2, unit U-W3's Switch/State half; Sec10.1's RTPC half is NOT
// re-modelled here - it is already covered, at the public-call-wrapper
// layer this project models, by global_rtpc.h's GlobalRtpcCache/
// SmoothedGlobalRtpc/MixRtpcBank/SingleValueRtpc, and Sec10.1.1 step 5
// itself cross-checks that exact cache against this pass's own reading
// with no correction needed).
//
// Boundary note: wwise_backend.h's existing scope comment says "Nothing
// about Wwise's own internals is modelled" - that was written for the
// PUBLIC-call-only shape of the original merge. Sec10.2 is new,
// CONFIRMED-by-disassembly material about what Wwise does INSIDE
// CAkRTPCMgr/the State manager after SetSwitch/SetState is called, which
// this extension starts modelling because a later real backend (noted in
// the original merge commit: "a real Wwise backend comes later as a Team A
// spec") needs exactly this fidelity. Nothing below claims to be the public
// SDK; see each class's own citation.
//
// Switch (Sec10.2.2, CAkRTPCMgr::SetSwitchInternal, un-exported 0x00f6f810):
// per (object, group) -> state is PURE BOOKKEEPING - no curve, no property
// application, and CONFIRMED to have NO "unchanged" short-circuit (unlike
// RTPC's exact-equality check, Sec10.1.1 step5, and State's own old-vs-new
// compare, Sec10.2.3 step3): every setSwitch() call stores and notifies,
// even when the new state equals the stored one. Two independent notify
// primitives exist in the real engine and are both modelled: the "set"
// path's own list (filtered by GROUP only - Sec10.2.2 step3) and a
// standalone "just notify" sibling, 0x00f542c0 (filtered by GROUP+OBJECT
// exactly - Sec10.2.2 step4), which does not touch storage at all.
//
// State (Sec10.2.3, a SEPARATE manager on singleton 0x02cc5318,
// 0x00f547a0): structurally CONFIRMED to track, per StateGroupID, up to
// four numeric properties (HYPOTHESIS names Volume/Pitch/LPF/HPF - "the
// count matches exactly" but no string/export in the binary ties a name to
// any of the four), compared old-vs-new by exact equality, and either
// ramped (via a genuinely separate ramp-manager singleton, 0x02cc5324) or
// snapped immediately. The exact criterion that decides ramp-vs-snap
// (0x00f5ef40) and the ramp's own curve/timing (0x00f537f0/0x00f53c40) were
// NOT traced (Sec10 OPEN items 35/40) - inventing either would violate this
// project's "never invent a value" rule, so StateGroupEngine takes the
// ramp-vs-snap decision as an injected TransitionPolicy (default: always
// snap, this project's own conservative choice, not a modelled engine fact)
// and never advances a ramp on its own - a caller/test resolves one
// explicitly via completeRamp(), the same "test/harness hook" convention
// wwise_backend.h already uses for fireEventCallback()/completeBankLoad().
//
// Sec10.2.2's notify filtering only needs the group id to reach a watcher;
// Sec10.2.3's id-only layer (currentStateId) is fully derivable from the
// public SetState(group,state) call alone with no bank data - that part IS
// wired into NullWwiseBackend::setState (wwise_backend.cpp). The 4-property
// ramp layer (applyProperties) needs the AUTHORED new value-set as an
// explicit argument from whoever has it (bank/HIRC loading is a separate,
// out-of-scope unit per Sec6/Sec10 OPEN items) - NullWwiseBackend does not
// call it, since it has no bank data to supply; never invented here.

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <unordered_map>
#include <vector>

#include "sr3audioruntime/types.h"
#include "sr3luahost/open_state.h"

namespace sr3audioruntime {

// CAkRTPCMgr::SetSwitchInternal's own table (Sec10.2.2): a
// (object, SwitchGroupID) -> SwitchStateID map, plus the two independent
// notify primitives the real engine fires through.
class SwitchGroupTable {
public:
    // Fired for every setSwitch() call on a matching group, for ANY object
    // (Sec10.2.2 step3: the notification list at CAkRTPCMgr+0x91c is
    // filtered by group only, not by object - the watcher itself is handed
    // which object changed).
    using GroupWatcher = std::function<void(GameObjectId object, uint32_t newState)>;
    // Fired only by notifyExactOnly(), for a specific (object, group) pair
    // (Sec10.2.2 step4, the standalone sibling 0x00f542c0 - filtered by
    // group AND object together, via a different list, CAkRTPCMgr+0x2c).
    using ExactWatcher = std::function<void(uint32_t newState)>;

    // Sec10.2.2 steps 1-3: find-or-create the (object,group) slot, store
    // the new state UNCONDITIONALLY (CONFIRMED: no change-filter, unlike
    // RTPC/State), then fire every GroupWatcher registered for `group`.
    void setSwitch(GameObjectId object, uint32_t group, uint32_t state) {
        key_.first = object;
        key_.second = group;
        table_[key_] = state;
        auto it = groupWatchers_.find(group);
        if (it != groupWatchers_.end()) {
            for (auto& cb : it->second) cb(object, state);
        }
    }

    // Sec10.2.2 step4 (0x00f542c0): fires ONLY the ExactWatcher(s)
    // registered for this exact (object,group) pair - does not touch
    // storage at all, a genuinely standalone "just notify" primitive.
    void notifyExactOnly(GameObjectId object, uint32_t group, uint32_t state) const {
        auto it = exactWatchers_.find({object, group});
        if (it != exactWatchers_.end()) {
            for (auto& cb : it->second) cb(state);
        }
    }

    void registerGroupWatcher(uint32_t group, GroupWatcher cb) { groupWatchers_[group].push_back(std::move(cb)); }
    void registerExactWatcher(GameObjectId object, uint32_t group, ExactWatcher cb) {
        exactWatchers_[{object, group}].push_back(std::move(cb));
    }

    bool hasSwitch(GameObjectId object, uint32_t group) const { return table_.count({object, group}) != 0; }

    // Sec10 OPEN item 34: the "Get"-side consumer (a switch container
    // reading this table at decision time) was never located, so there is
    // no CONFIRMED fallback for "what does an unset (object,group) read
    // as" - throwing names the open item instead of inventing a bank-
    // authored default switch value.
    uint32_t currentSwitch(GameObjectId object, uint32_t group) const {
        auto it = table_.find({object, group});
        if (it == table_.end()) {
            throw sr3luahost::OpenStateError(
                "SwitchGroupTable(object=" + std::to_string(object) + ",group=" + std::to_string(group) + ")",
                "spec-audio-runtime.md Sec10.2.2/Sec10 OPEN item 34 (no authored default switch value modelled)");
        }
        return it->second;
    }

    void clear() {
        table_.clear();
        groupWatchers_.clear();
        exactWatchers_.clear();
    }

private:
    std::pair<GameObjectId, uint32_t> key_{};
    std::map<std::pair<GameObjectId, uint32_t>, uint32_t> table_;
    std::unordered_map<uint32_t, std::vector<GroupWatcher>> groupWatchers_;
    std::map<std::pair<GameObjectId, uint32_t>, std::vector<ExactWatcher>> exactWatchers_;
};

// The separate State manager (Sec10.2.3, singleton 0x02cc5318, 0x00f547a0).
class StateGroupEngine {
public:
    // Sec10.2.3 step3: "up to four numeric properties" - HYPOTHESIS names
    // only (public Wwise's well-known Volume/Pitch/LPF/HPF set; the count
    // (four) is CONFIRMED, no name is).
    static constexpr size_t kPropertyCount = 4;

    // Sec10.2.3 step3's own ramp-vs-snap branch; the real criterion
    // (0x00f5ef40) was not traced (OPEN) - see this header's own doc
    // comment. Default (AlwaysImmediate below) is this project's own
    // conservative choice, not a modelled engine fact.
    class TransitionPolicy {
    public:
        virtual ~TransitionPolicy() = default;
        virtual bool shouldRamp(uint32_t group, size_t propertyIndex, float oldValue, float newValue) const = 0;
    };
    class AlwaysImmediate : public TransitionPolicy {
    public:
        bool shouldRamp(uint32_t /*group*/, size_t /*propertyIndex*/, float /*oldValue*/,
                         float /*newValue*/) const override {
            return false;
        }
    };

    // The id-only layer: fully derivable from the public SetState(group,
    // state) call alone (no bank data needed) - this is what
    // NullWwiseBackend::setState maintains automatically.
    void setState(uint32_t group, uint32_t newStateId) { groups_[group].stateId = newStateId; }

    uint32_t currentStateId(uint32_t group) const {
        auto it = groups_.find(group);
        if (it == groups_.end()) {
            throw sr3luahost::OpenStateError("StateGroupEngine::currentStateId(group=" + std::to_string(group) + ")",
                                              "spec-audio-runtime.md Sec10.2.3 (no SetState seen yet for this group)");
        }
        return it->second.stateId;
    }

    // Applies the AUTHORED new value-set for `newStateId` (Sec10.2.3 steps
    // 2-3): compares old-vs-new per property by EXACT equality (same idiom
    // as RTPC's own check, Sec10.1.1 step5), and for each CHANGED property
    // either registers a ramp (policy.shouldRamp == true) or snaps
    // immediately. A group's first-ever call has no "old" to compare
    // against, so it establishes the baseline directly (the same
    // find-or-create precedent used throughout this spec, e.g. Sec10.1.1
    // step3's per-RTPC-ID record creation) - no ramp on that first call.
    void applyProperties(uint32_t group, uint32_t newStateId, const std::array<float, kPropertyCount>& newValues,
                          const TransitionPolicy& policy = AlwaysImmediate{}) {
        GroupRecord& rec = groups_[group];
        rec.stateId = newStateId;
        for (size_t i = 0; i < kPropertyCount; ++i) {
            if (!rec.valuesKnown[i]) {
                rec.values[i] = newValues[i];
                rec.valuesKnown[i] = true;
                rec.ramping[i] = false;
                continue;
            }
            if (rec.values[i] == newValues[i]) continue;  // Sec10.2.3 step3: unchanged, nothing to do
            if (policy.shouldRamp(group, i, rec.values[i], newValues[i])) {
                rec.ramping[i] = true;
                rec.rampTarget[i] = newValues[i];
            } else {
                rec.values[i] = newValues[i];
                rec.ramping[i] = false;
            }
        }
    }

    float currentValue(uint32_t group, size_t propertyIndex) const {
        const GroupRecord& rec = groupRecordOrThrow(group);
        if (!rec.valuesKnown[propertyIndex]) {
            throw sr3luahost::OpenStateError(
                "StateGroupEngine::currentValue(group=" + std::to_string(group) +
                    ",prop=" + std::to_string(propertyIndex) + ")",
                "spec-audio-runtime.md Sec10.2.3 (applyProperties never called for this group)");
        }
        return rec.values[propertyIndex];
    }

    bool isRamping(uint32_t group, size_t propertyIndex) const {
        auto it = groups_.find(group);
        if (it == groups_.end()) return false;
        return it->second.ramping[propertyIndex];
    }

    // Test/harness hook (same convention as WwiseBackend::fireEventCallback/
    // completeBankLoad): the real ramp curve/timing was never traced (Sec10
    // OPEN item 40 / this header's own note), so this library never
    // advances a ramp by itself - a caller/test resolves one explicitly
    // once it decides the ramp has finished.
    void completeRamp(uint32_t group, size_t propertyIndex) {
        auto it = groups_.find(group);
        if (it == groups_.end()) return;
        GroupRecord& rec = it->second;
        if (!rec.ramping[propertyIndex]) return;
        rec.values[propertyIndex] = rec.rampTarget[propertyIndex];
        rec.ramping[propertyIndex] = false;
    }

    void clear() { groups_.clear(); }

private:
    struct GroupRecord {
        uint32_t stateId = 0;
        std::array<float, kPropertyCount> values{};
        std::array<bool, kPropertyCount> valuesKnown{};
        std::array<bool, kPropertyCount> ramping{};
        std::array<float, kPropertyCount> rampTarget{};
    };

    const GroupRecord& groupRecordOrThrow(uint32_t group) const {
        auto it = groups_.find(group);
        if (it == groups_.end()) {
            throw sr3luahost::OpenStateError("StateGroupEngine(group=" + std::to_string(group) + ")",
                                              "spec-audio-runtime.md Sec10.2.3 (no applyProperties seen yet for this group)");
        }
        return it->second;
    }

    std::unordered_map<uint32_t, GroupRecord> groups_;
};

}  // namespace sr3audioruntime
