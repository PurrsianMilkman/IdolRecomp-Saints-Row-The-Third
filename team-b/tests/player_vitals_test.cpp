// Tests for include/sr3player/vitals.h (task T1): damage intake, death trigger, respawn timer, respawn action.
#include <cstdio>

#include "sr3player/vitals.h"

static int g_fail = 0;
#define CHECK(c)                                                     \
    do {                                                             \
        if (!(c)) {                                                  \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); \
            ++g_fail;                                                \
        }                                                            \
    } while (0)

using sr3player::PlayerVitals;

int main() {
    PlayerVitals v({100, 4000});
    CHECK(v.health() == 100 && !v.dead() && !v.controlsDisabled() && v.cameraMode() == 0);
    CHECK(!v.applyDamage(0.0, 0) && !v.applyDamage(-5.0, 0) && v.health() == 100);
    CHECK(!v.applyDamage(30.0, 10) && v.health() == 70 && !v.dead());
    CHECK(v.stats().damageEvents == 1 && v.stats().totalDamage == 30.0 && v.stats().minHealth == 70);
    // lethal, overkill clamps at 0
    CHECK(v.applyDamage(1000.0, 100) && v.health() == 0 && v.dead());
    CHECK(v.controlsDisabled() && v.cameraMode() == sr3player::kDeathCameraMode && v.cameraMode() == 9);
    CHECK(v.stats().deaths == 1 && v.diedAtMs() == 100);
    // dead players take no further damage; deaths do not double count
    CHECK(!v.applyDamage(10.0, 200) && v.stats().deaths == 1 && v.stats().damageEvents == 2);
    CHECK(!v.observeHealth(50, 200) && v.health() == 0);
    // respawn timer: due at died + delay exactly
    CHECK(!v.respawnDue(100) && !v.respawnDue(4099) && v.respawnDue(4100) && v.respawnDue(9999));
    CHECK(v.respawn());
    CHECK(v.health() == 100 && !v.dead() && !v.controlsDisabled() && v.cameraMode() == 0);
    CHECK(v.stats().respawns == 1 && v.stats().minHealth == 0);
    CHECK(!v.respawn());  // not dead: no-op
    CHECK(!v.respawnDue(99999));

    // host-driven intake
    PlayerVitals h({200, 4000});
    CHECK(!h.observeHealth(150, 5) && h.health() == 150 && h.stats().damageEvents == 1);
    CHECK(!h.observeHealth(180, 6) && h.health() == 180 && h.stats().damageEvents == 1);  // heal, not damage
    CHECK(!h.observeHealth(999, 7) && h.health() == 200);                                 // clamped to max
    CHECK(h.observeHealth(0, 8) && h.dead() && h.diedAtMs() == 8 && h.stats().deaths == 1);
    CHECK(h.respawnDue(4008) && !h.respawnDue(4007));

    // zero max health: nothing registers
    PlayerVitals z({0, 4000});
    CHECK(!z.applyDamage(5.0, 0) && !z.dead() && !z.observeHealth(0, 0) && !z.dead());

    // zero delay: due immediately
    PlayerVitals d({10, 0});
    CHECK(d.applyDamage(10.0, 50) && d.respawnDue(50));

    // CC-2: cash cap (0..2e9 cents), saturating adders
    {
        using sr3player::AddCashSaturating;
        CHECK(AddCashSaturating(100.0, 50.0) == 150.0);
        CHECK(AddCashSaturating(1999999990.0, 100.0) == 2000000000.0);       // overflow dropped
        CHECK(AddCashSaturating(2000000000.0, 2000000000.0) == 2000000000.0);  // legacy saturating helper (the engine uses AddCashWallet, D-22)
        CHECK(AddCashSaturating(5.0, 9.0e12) == 2000000000.0);               // huge grant saturates (x100 step)
        CHECK(AddCashSaturating(100.0, -500.0) == 0.0);                      // floored at 0
        CHECK(AddCashSaturating(100.0, -9.0e12) == 0.0);
        CHECK(AddCashSaturating(0.0, 0.0) == 0.0);
    }

    // V10-9: the 18 ordered respawn reset steps (spec-player-runtime 18.6 R3 note)
    {
        size_t n = 0;
        const sr3player::RespawnStepInfo* o = sr3player::RespawnResetOrder(n);
        CHECK(n == 18);
        bool seq = true;
        for (size_t i = 0; i < n; ++i) seq = seq && static_cast<int>(o[i].step) == static_cast<int>(i) + 1;
        CHECK(seq);
        auto idx = [&](sr3player::RespawnStep s) {
            for (size_t i = 0; i < n; ++i)
                if (o[i].step == s) return static_cast<int>(i);
            return -1;
        };
        using S = sr3player::RespawnStep;
        CHECK(idx(S::VehicleSeatExit) < idx(S::CrouchClear));
        CHECK(idx(S::HealthRestore) < idx(S::RagdollStop) && idx(S::RagdollStop) < idx(S::DownedLeave));
        CHECK(idx(S::Teleport) < idx(S::InventorySlot0) && idx(S::InventorySlot0) < idx(S::Notoriety));
        CHECK(idx(S::Notoriety) < idx(S::ControlsOn) && idx(S::ControlsOn) < idx(S::BustedClear));
        CHECK(idx(S::BustedClear) < idx(S::ReleaseHandles) && idx(S::ReleaseHandles) < idx(S::DeathLatchAndHook));
        CHECK(idx(S::DeathLatchAndHook) == 17 && idx(S::GateLocalPlayer) == 0);
    }

    if (g_fail) {
        std::printf("player_vitals_test: %d failure(s)\n", g_fail);
        return 1;
    }
    std::printf("player_vitals_test: all checks passed\n");
    return 0;
}
