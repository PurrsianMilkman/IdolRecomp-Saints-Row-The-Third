#pragma once

// sr3airuntime/notoriety_escalation.h - the notoriety GAIN/DECAY recipe of
// spec-ai-runtime.md Sec19.8 ("Escalation rules - port recipe", FINAL), as a
// pure session object, plus the CHOSEN crime records the free-roam playtest
// reports (T5, 2026-10-18).
//
// NOTE: this replaced the former NotorietyDriver (removed 2026-10-18), whose
// AddPointsRaw()/DecayTick() used CHOSEN stand-ins that the FINAL recipe
// contradicts (cumulative-threshold spans, integer decay, no pending queue /
// G3 / G4 / G5). This class reuses the shared tables (LevelTableForTrack) and
// CrimeActivity records in notoriety.h and adds the FINAL rules.
//
// IMPLEMENTED (Sec19.8, FINAL/CONFIRMED): G1 enqueue ring (40 entries, the
// 5 same-key dedupe activities, activity 20's pending cap of 5, full-ring
// immediate processing), G2 x3 bonus flag (caller supplied), G3 award incl.
// Min_Level / Max_Level / witnessed (P/4) rules and pointsTo(), G4 roll-up
// with the max-bound clamp and the 0.25 level-entry floor, G5 first-decay
// timer armed by the crime, the decay tick (DecayRate x saveMult every
// 1000 ms x difficulty recip, roll-down floor, finish rule), the
// decay-suppress and vehicle-type gates, and the per-frame order (drain,
// decay).
//
// NOT IMPLEMENTED here (no consumer in the viewer): the 500 ms ramp-to-
// minimum tick (needs notoriety_set_min callers), the AI-engagement hold
// flag (needs hostile AI engaging the player; the setter exists below),
// per-AI "seen within 15 s" re-arms, co-op sync.
//
// CHOSEN readings of ambiguous Sec19.8 text (each is a Team A question):
//  (1) the 0.25 x limit(level+1) floor is applied only when the level
//      actually changed ("a fresh level starts at least a quarter of the
//      way"), not on every award;
//  (2) "Reports are skipped when Points <= 0" is read literally, so the one
//      negative activity (enter owned store, -20) has NO effect here;
//  (3) "on reaching the floor set fraction = 0" is applied when a decay step
//      rolled the level down onto the floor or drove the fraction negative at
//      the floor, not when the fraction is merely positive at the floor.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>

#include "sr3airuntime/notoriety.h"

namespace sr3airuntime {

// ---------------------------------------------------------------------------
// CHOSEN crime records. The activity NAMES are confirmed (spec-tables-
// progression.md Sec6.1 lists all 30), but the spec text quotes real values
// only for the five kConfirmed* records in notoriety.h. Every number below is
// a CHOSEN stand-in (labelled), with Check_Detection = true on both sub-
// blocks as Sec14.6 states for "civilian assault/shooting/kill" and
// "car assault". Gang points are 0 ("most civilian activities are police-
// only", Sec14.6). Delay 500 ms is the Sec14.6 value for 28/30 activities.
// Which activity a given in-world event reports is itself OPEN (Team A).
// ---------------------------------------------------------------------------
inline const CrimeActivity kChosenCivilianShooting = {
    "civilian shooting", 500,
    /*gang=*/{0, 0, kNotorietyMaxLevel, true},
    /*police=*/{25, 0, kNotorietyMaxLevel, true},  // CHOSEN points 25 (value not in spec)
};
inline const CrimeActivity kChosenCivilianKill = {
    "civilian kill", 500,
    /*gang=*/{0, 0, kNotorietyMaxLevel, true},
    /*police=*/{100, 0, kNotorietyMaxLevel, true},  // CHOSEN points 100 (value not in spec)
};
// "Hit a pedestrian with a vehicle": CHOSEN to report "civilian assault".
inline const CrimeActivity kChosenCivilianAssault = {
    "civilian assault", 500,
    /*gang=*/{0, 0, kNotorietyMaxLevel, true},
    /*police=*/{60, 0, kNotorietyMaxLevel, true},  // CHOSEN points 60 (value not in spec)
};

// notoriety_levels.xtbl Notoriety_Decay_Mult for normal difficulty is 1.0
// (spec-tables-progression.md Sec14.7); the stored value is the reciprocal.
inline constexpr double kChosenNormalDifficultyDecayRecip = 1.0;  // CHOSEN: difficulty not selectable in the viewer

struct NotorietyLevelEvent {
    NotorietyTrack track = NotorietyTrack::kPolice;
    int32_t oldLevel = 0;
    int32_t newLevel = 0;
    int64_t atMs = 0;
};

class NotorietyEscalation {
public:
    // Sec19.8 "saveMult": the four save-file decay multipliers, default 1.0.
    std::array<double, kNotorietyTrackCount> saveMult = {1.0, 1.0, 1.0, 1.0};
    double difficultyDecayRecip = kChosenNormalDifficultyDecayRecip;
    bool gainSuppressed = false;   // 0x014A4C0D (Sec19.2)
    bool decaySuppressed = false;  // 0x014A4C0F (Sec19.2/19.8 gate)
    std::array<int32_t, kNotorietyTrackCount> minBound = {0, 0, 0, 0};
    std::array<int32_t, kNotorietyTrackCount> maxBound = {5, 5, 5, 5};
    std::function<void(const NotorietyLevelEvent&)> onLevelChange;

    static constexpr size_t kPendingRingSize = 40;                   // G1
    static constexpr double kLevelEntryFloorFraction = 0.25;         // G4
    static constexpr int64_t kDecayIntervalMs = 1000;                // Sec19.8 decay re-arm
    static constexpr int64_t kFirstDecayCapMs = 5000;                // G5 mode-byte cap (mode byte OPEN, not used)

    int32_t level(NotorietyTrack t) const { return rec(t).level; }
    double fraction(NotorietyTrack t) const { return rec(t).fraction; }
    bool decayArmed(NotorietyTrack t) const { return rec(t).decayT.isArmed(); }
    size_t pendingCount() const { return pending_.size(); }

    // limit(set, L), L = 1..5: points banked inside level L-1 to reach L.
    static int32_t Limit(NotorietyTrack t, int32_t L) {
        return LevelTableForTrack(t).levelLimit[static_cast<size_t>(std::clamp(L, 1, 5))];
    }
    static int32_t FirstDecayMs(NotorietyTrack t, int32_t lvl) {
        return LevelTableForTrack(t).firstDecayMs[static_cast<size_t>(std::clamp(lvl, 0, 5))];
    }
    static int32_t DecayRate(NotorietyTrack t, int32_t lvl) {
        return LevelTableForTrack(t).decayRate[static_cast<size_t>(std::clamp(lvl, 0, 5))];
    }

    // 0x00604f90: points still needed to reach level T (0 when level >= T).
    int32_t PointsTo(NotorietyTrack t, int32_t target) const {
        const Record& r = rec(t);
        if (r.level >= target) return 0;
        int32_t p = Limit(t, r.level + 1) - static_cast<int32_t>(r.fraction);
        for (int32_t k = r.level + 2; k <= target; ++k) p += Limit(t, k);
        return p;
    }

    // G1: enqueue a crime report. It is applied by Tick() once its Delay
    // elapses (or immediately when the ring is full). `witnessed` is the
    // caller's answer to the Check_Detection test (G3 "D"); `bonusX3` is the
    // G2 pending-flag bit 1 (victim descriptor flag; real meaning OPEN, so
    // callers pass false).
    void ReportCrime(NotorietyTrack t, const CrimeActivity& activity, bool witnessed, bool bonusX3, int64_t nowMs) {
        if (IsDedupeActivity(activity.name)) {
            for (const auto& p : pending_)
                if (p.track == t && p.activity.name == activity.name) return;
        }
        if (activity.name == "mover dislodge") {
            int n = 0;
            for (const auto& p : pending_) n += (p.activity.name == activity.name);
            if (n >= 5) return;
        }
        Pending p{t, activity, witnessed, bonusX3, nowMs + activity.delayMs};
        if (pending_.size() >= kPendingRingSize) {
            Apply(p, nowMs);
            return;
        }
        pending_.push_back(std::move(p));
    }

    // Script-style absolute write (a Lua notoriety_set seen through the
    // EngineState alias track): CHOSEN adoption - set the level, zero the
    // fraction, arm the first-decay timer. Not part of Sec19.8's recipe.
    void SetLevel(NotorietyTrack t, int32_t newLevel, int64_t nowMs) {
        Record& r = rec(t);
        const int32_t nl = std::clamp(newLevel, 0, kNotorietyMaxLevel);
        const int32_t old = r.level;
        r.level = nl;
        r.fraction = 0.0;
        if (nl > 0) r.decayT.arm(nowMs, FirstDecayDelay(t, nl));
        else r.decayT.disarm();
        if (nl != old) Notify(t, old, nl, nowMs);
    }

    // spec-player-runtime.md Sec18.6: a respawn clears the 40-entry alert ring (0x006097a0) before resetting the tracks.
    void ClearPending() { pending_.clear(); }

    // +0x18 bit 0, "hold at level >= 1 this frame" (AI engaging the player).
    void SetHoldFlag(NotorietyTrack t) { rec(t).hold = true; }

    // Seen-by-cops re-arm (Sec19.8 "lost the cops" / 0x00605020).
    void RearmFirstDecay(NotorietyTrack t, int64_t nowMs) {
        Record& r = rec(t);
        r.decayT.arm(nowMs, FirstDecayDelay(t, r.level));
    }

    // The per-frame order of 0x00609dc0 (without the ramp tick): drain due
    // crimes, then run decay per track, then clear the hold flags.
    // `playerInDecayBlockingVehicle`: local player in a Vehicle_Type 2-5
    // vehicle (airplane/helicopter/VTOL/watercraft) - the gate 0x006096e0.
    void Tick(int64_t nowMs, bool playerInDecayBlockingVehicle = false) {
        while (!pending_.empty()) {
            // Drain due records (ring order; each record has its own due time).
            bool any = false;
            for (auto it = pending_.begin(); it != pending_.end();) {
                if (it->dueMs <= nowMs) {
                    Pending p = std::move(*it);
                    it = pending_.erase(it);
                    Apply(p, nowMs);
                    any = true;
                } else {
                    ++it;
                }
            }
            if (!any) break;
        }
        for (int32_t i = 0; i < kNotorietyTrackCount; ++i) {
            const auto t = static_cast<NotorietyTrack>(i);
            if (!decaySuppressed && !playerInDecayBlockingVehicle) DecayStep(t, nowMs);
            rec(t).hold = false;
        }
    }

private:
    struct Record {
        int32_t level = 0;
        double fraction = 0.0;
        EngineTimer decayT;
        bool hold = false;
    };
    struct Pending {
        NotorietyTrack track;
        CrimeActivity activity;
        bool witnessed;
        bool bonusX3;
        int64_t dueMs;
    };
    std::array<Record, kNotorietyTrackCount> records_;
    std::deque<Pending> pending_;

    Record& rec(NotorietyTrack t) { return records_[static_cast<size_t>(t)]; }
    const Record& rec(NotorietyTrack t) const { return records_[static_cast<size_t>(t)]; }

    static bool IsDedupeActivity(const std::string& n) {
        // Activities 0, 3, 4, 11, 23: armored truck assault, car assault,
        // civilian assault, gang assault, police assault (Sec6.1 index order).
        return n == "armored truck assault" || n == "car assault" || n == "civilian assault" ||
               n == "gang assault" || n == "police assault";
    }

    int64_t FirstDecayDelay(NotorietyTrack t, int32_t lvl) const {
        return static_cast<int64_t>(FirstDecayMs(t, lvl) * difficultyDecayRecip);
    }

    void Notify(NotorietyTrack t, int32_t oldL, int32_t newL, int64_t nowMs) {
        if (onLevelChange) onLevelChange(NotorietyLevelEvent{t, oldL, newL, nowMs});
    }

    // G3 + G4 + G5 for one due report.
    void Apply(const Pending& p, int64_t nowMs) {
        const NotorietyTrack t = p.track;
        Record& r = rec(t);
        const CrimeActivitySubBlock& blk = (t == NotorietyTrack::kPolice) ? p.activity.police : p.activity.gang;
        const int32_t P = blk.points * (p.bonusX3 ? 3 : 1);
        // G5: the crime re-arms the first-decay timer before the award. D-21 rule 1: a report with Points <= 0 only skips this
        // re-arm; the points still go through the award.
        if (P > 0) r.decayT.arm(nowMs, FirstDecayDelay(t, r.level));
        if (gainSuppressed && P > 0) return;  // G3: gain-suppress byte, positive delta
        const int32_t old = r.level;
        if (P < 0) {
            // D-21 rule 1: negative points are applied as they are (no Min_Level lift, no witness test; above the
            // Max_Level / max bound the report has no effect, as for positive points) and roll the level down exactly like
            // decay, with the same floor rule.
            if (r.level > maxBound[static_cast<size_t>(t)] || r.level > blk.maxLevel) return;
            const int32_t mn = minBound[static_cast<size_t>(t)];
            RollDown(r, t, -static_cast<double>(P), mn);
            if (r.level != old) Notify(t, old, r.level, nowMs);
            return;
        }
        const bool D = p.witnessed || !blk.checkDetection;
        int32_t A;
        if (P > 0 && (r.level >= maxBound[static_cast<size_t>(t)] || r.level >= blk.maxLevel)) {
            A = 0;
        } else if (r.level < blk.minLevel) {
            // D-21 rule 2: a zero-point report still runs the award (witnessed: lifted to Min_Level; unwitnessed: minimum award 1).
            A = D ? std::max(P, PointsTo(t, blk.minLevel)) : std::max(1, P / 4);
        } else {
            A = D ? P : std::max(1, P / 4);
        }
        if (P == 0 && r.level >= blk.maxLevel) A = 0;
        A = std::min(A, PointsTo(t, blk.maxLevel));
        // G4 roll-up.
        if (P <= 0 && A > 0) r.decayT.arm(nowMs, FirstDecayDelay(t, r.level));  // a zero-point report that awards points arms the timer
        r.fraction += A;
        while (r.level < kNotorietyMaxLevel &&
               static_cast<int32_t>(r.fraction) >= Limit(t, r.level + 1)) {
            r.fraction -= Limit(t, r.level + 1);
            ++r.level;
        }
        const int32_t mx = maxBound[static_cast<size_t>(t)];
        if (r.level > mx) {
            r.level = mx;
            r.fraction = (mx < 5) ? Limit(t, mx + 1) - 1 : 0.0;
        }
        if (r.level >= kNotorietyMaxLevel) r.fraction = 0.0;
        if (r.level != old) {
            Notify(t, old, r.level, nowMs);
            if (r.level < kNotorietyMaxLevel) {  // D-21 rule 4: only on a level change; the floor uses the fractional part too
                const double frac = r.fraction - std::trunc(r.fraction);
                r.fraction = std::max(r.fraction, kLevelEntryFloorFraction * (Limit(t, r.level + 1) + frac));
            }
        }
    }

    // D-21 rule 3: while the fraction is negative add limit(level) and drop one level; only a drop BELOW the floor sets the level to
    // the floor with fraction 0. Landing exactly on the floor keeps the positive remainder (decay continues at the floor level's
    // rate until the fraction reaches 0).
    void RollDown(Record& r, NotorietyTrack t, double amount, int32_t floorL) {
        r.fraction -= amount;
        while (r.fraction < 0.0) {
            if (r.level <= 0) { r.level = std::max(0, floorL); r.fraction = 0.0; break; }
            r.fraction += Limit(t, r.level);
            --r.level;
            if (r.level < floorL) { r.level = floorL; r.fraction = 0.0; break; }
        }
    }

    void DecayStep(NotorietyTrack t, int64_t nowMs) {
        Record& r = rec(t);
        if (!r.decayT.expired(nowMs)) return;
        const int32_t old = r.level;
        const int32_t mn = minBound[static_cast<size_t>(t)];
        const int32_t floorL = (mn < 1 && r.hold) ? 1 : mn;
        RollDown(r, t, DecayRate(t, r.level) * saveMult[static_cast<size_t>(t)], floorL);
        if (r.level <= mn && r.fraction <= 0.0) {
            r.decayT.disarm();  // decay finished
        } else {
            r.decayT.arm(nowMs, static_cast<int64_t>(kDecayIntervalMs * difficultyDecayRecip));
        }
        if (r.level != old) Notify(t, old, r.level, nowMs);
    }
};

}  // namespace sr3airuntime
