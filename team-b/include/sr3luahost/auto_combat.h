// sr3luahost/auto_combat.h - decision logic for the sr3_viewer `--auto-combat` TEST-HARNESS assist.
//
// Everything here is a CHOSEN harness heuristic, not engine behaviour (no spec basis; nothing here is called by the
// engine). It replaces the `--auto-objective` instant `character_kill` on hostile on_death mission targets with:
//   1. TELEPORT the player to a standoff point near the nearest awaited target (when farther than combatRange),
//   2. WAIT noticeFrames so the real NPC AI loop (EngineState::tickNpcAi) notices/engages the player,
//   3. FIRE at the nearest live target every fireIntervalFrames (the caller routes the shot through the real
//      sr3weapons::WeaponInstance::fire() hitscan + EngineState::applyDamageModelHit),
//   4. FALL BACK to character_kill only after fallbackFrames with no progress (a kill by damage or a new target).
// The function is pure (state in, decision out) so it is unit-testable without a window or a Host.
#pragma once

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace sr3luahost::autocombat {

struct Config {
    double combatRange = 60.0;        // CHOSEN: closer than this (xz metres) counts as "in combat range"
    double standoff = 25.0;           // CHOSEN: teleport lands this far (xz) from the target, on the player's side
    int noticeFrames = 150;           // CHOSEN: 5 s at the harness 30 Hz step, for the NPC AI to notice and fight
    int fireIntervalFrames = 12;      // CHOSEN: one trigger pull every 12 frames (~2.5 shots/s)
    int fallbackFrames = 1800;        // CHOSEN: 60 s with no progress -> character_kill fallback
    int teleportRetryFrames = 60;     // CHOSEN: re-teleport if still out of range this long after the last teleport
};

struct Target {
    std::string name;
    double x = 0, z = 0;
};

struct State {
    std::string current;       // the target the engagement counters belong to
    int inRangeFrames = 0;     // frames spent in range of `current`
    int sinceFire = 1 << 20;   // frames since the last trigger pull
    int sinceTeleport = 1 << 20;
    int noProgressFrames = 0;
    int lastKills = 0;
};

enum class Action { None, Teleport, Fire, Fallback };

struct Decision {
    Action action = Action::None;
    std::string target;
    double tx = 0, tz = 0;  // Teleport destination (xz)
};

// `targets`: live hostile awaited targets with known positions. `kills`: monotonic count of targets that have died.
inline Decision decide(State& s, const Config& cfg, double px, double pz, const std::vector<Target>& targets,
                       int kills) {
    Decision d;
    if (targets.empty()) {
        s = State{};
        s.lastKills = kills;
        return d;
    }
    if (kills != s.lastKills) {  // progress: a target died through the normal path
        s.lastKills = kills;
        s.noProgressFrames = 0;
        s.inRangeFrames = 0;
    }
    const Target* best = nullptr;
    double bestD2 = 0;
    for (const Target& t : targets) {
        const double dx = t.x - px, dz = t.z - pz, d2 = dx * dx + dz * dz;
        if (!best || d2 < bestD2) { best = &t; bestD2 = d2; }
    }
    if (best->name != s.current) {
        s.current = best->name;
        s.inRangeFrames = 0;
    }
    ++s.noProgressFrames;
    ++s.sinceFire;
    ++s.sinceTeleport;
    d.target = best->name;
    if (s.noProgressFrames >= cfg.fallbackFrames) {
        s.noProgressFrames = 0;
        d.action = Action::Fallback;
        return d;
    }
    const double dist = std::sqrt(bestD2);
    if (dist > cfg.combatRange) {
        s.inRangeFrames = 0;
        if (s.sinceTeleport >= cfg.teleportRetryFrames) {
            s.sinceTeleport = 0;
            d.action = Action::Teleport;
            // standoff point on the segment target -> player (the player's side), or +x when coincident
            const double ux = dist > 1e-6 ? (px - best->x) / dist : 1.0, uz = dist > 1e-6 ? (pz - best->z) / dist : 0.0;
            d.tx = best->x + ux * cfg.standoff;
            d.tz = best->z + uz * cfg.standoff;
        }
        return d;
    }
    ++s.inRangeFrames;
    if (s.inRangeFrames > cfg.noticeFrames && s.sinceFire >= cfg.fireIntervalFrames) {
        s.sinceFire = 0;
        d.action = Action::Fire;
    }
    return d;
}

}  // namespace sr3luahost::autocombat
