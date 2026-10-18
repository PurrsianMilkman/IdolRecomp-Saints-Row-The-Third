// Synthetic tests for the AI runtime units A0-A3, spec-ai-runtime.md
// Sec2-Sec4/Sec6 (A0), Sec9 (A1), Sec10 (A2), Sec11 (A3). Every expected
// value below is a rule quoted from that spec (cited per block); world
// facts come from plain struct fields set directly in each test - injected
// test values, not data.

#include "synthetic_ai_runtime_test_common.h"

namespace airtest {

// ===================================================================
// A2 - perception storage/producers sanity (Sec10), beyond what A1/A3
// tests above already exercise through real use.
// ===================================================================

void testPerceptionListMechanics() {
    PerceptionState p;
    // Capacity + score-ascending eviction (Sec10.1): list 8 has capacity 1.
    CHECK(p.postStimulus(plist::FriendlyFire, 1, {}, 50, 0, 1, 0, 0) == PostResult::Created);
    CHECK(p.postStimulus(plist::FriendlyFire, 2, {}, 80, 0, 1, 0, 0) == PostResult::Refused);  // worse, full
    CHECK(p.postStimulus(plist::FriendlyFire, 2, {}, 10, 0, 1, 0, 0) == PostResult::Created);  // better, evicts
    CHECK(p.entries(plist::FriendlyFire).size() == 1);
    CHECK(p.entries(plist::FriendlyFire)[0].handle == 2);

    // notice()/see() flag transitions (Sec10.3).
    p = PerceptionState{};
    CHECK(p.notice(10, {}, /*attitude=*/0, false, false, false, 0) == PostResult::Created);
    const PerceptionEntry* e = p.find(plist::KnownHostiles, 10);
    CHECK(e && (e->flags & pflag::ActivelyKnown) && !(e->flags & pflag::InView));
    CHECK(p.see(10, {}, 0, false, false, false, 100) == PostResult::Refreshed);
    e = p.find(plist::KnownHostiles, 10);
    CHECK(e && (e->flags & pflag::InView) && (e->flags & pflag::VisuallyConfirmedOnce));
    CHECK(p.hasEntry(plist::LastSightings, 10));  // "see" also writes list 1

    // Expiry (Sec10.2): list 2 has a 4000ms lifetime.
    p = PerceptionState{};
    p.postStimulus(plist::LineOfFire, 1, {}, 0, 0, 1, 0, 0);
    p.expireEntries(5000);
    CHECK(!p.hasEntry(plist::LineOfFire, 1));

    // The damage accumulator formula (Sec10.3's "the fade"): K=100, L=30000.
    p = PerceptionState{};
    PerceptionState::DamageProducerInput dmg;
    dmg.attackerHandle = 1;
    dmg.damage = 10;
    dmg.maxHealth = 100;
    dmg.attackerAttitude = 4;  // friendly-fire branch, avoids the notice-without-sight path
    p.recordDamage(dmg, 0);
    CHECK(p.damageAccumulator(0) > 0.09 && p.damageAccumulator(0) < 0.11);  // ~10% of max health
    CHECK(p.damageAccumulator(30000) < 1e-9);  // fully faded after 30s

    // The query (Sec10.7): priority by row order, then list order.
    p = PerceptionState{};
    p.postStimulus(plist::KnownHostiles, 1, {}, 0, 0, 1, 0, 0);
    p.postStimulus(plist::LastSightings, 2, {}, 0, 0, 1, 0, 0);
    std::vector<PerceptionState::QueryRow> rows;
    rows.push_back({plist::LastSightings, 0, -1, 0, 0, 0});
    rows.push_back({plist::KnownHostiles, 0, -1, 0, 0, 0});
    const PerceptionEntry* match = p.query(rows, 0);
    CHECK(match && match->handle == 2);  // first row (list 1) wins over the second (list 0)
}

}  // namespace airtest
