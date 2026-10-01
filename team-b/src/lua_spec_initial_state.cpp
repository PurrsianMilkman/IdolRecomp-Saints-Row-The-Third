// Initial engine-state values from Team A's specs (manager prep 2026-10-01).
//
// Host's constructor calls applySpecInitialState() once, before any script
// runs. Each area below is where a confirmed answer goes when its spec is
// synced: set the slot (OpenValue::set / OpenValueMap::set / OpenBits32::setBits)
// with the spec section in a comment, flip the matching expectation in
// tests/synthetic_luahost_test.cpp (the open-slot inventory checks), and send
// bridge-jobs/02f_mission_drive_after_answer.json. lua_host_run's
// verdict_open_state.tsv then shows the slot as known.
//
// Nothing is set yet: every slot below is OPEN in the synced specs (only
// CONFIRMED values may go here; HYPOTHESIS/OPEN stay unset).
#include "sr3luahost/engine_state.h"

namespace sr3luahost {

void applySpecInitialState(EngineState& es) {
    (void)es;

    // 1. co-op session object 0x0087ba20 (spec-lua-api-behaviour.md Sec3.1,
    //    Sec8.27, Sec10.2, Sec10.9): es.coopActive(), es.isHost(),
    //    es.coopJoinType(). OPEN - awaiting Team A (answer order 1).

    // 2. tutorial table 0x00717780 (Sec10.4, Sec6.19): es.tutorialResolves()
    //    per id. If the answer is a rule rather than a list, the resolver
    //    goes in the tutorial stubs instead. OPEN - awaiting Team A (order 1).

    // 3. vehicle-store active flag 0x022cdf08 (Sec10.1): es.vehicleStoreActive().
    //    OPEN - awaiting Team A (order 1).

    // 4. zscene (Sec14.23, Sec8.21): es.zsceneBusyFlag() 0x0153b556,
    //    es.zsceneStateCode() 0x0153b51c, es.zsceneNameState() 0x00723d20,
    //    es.zsceneTableResolves() 0x00721be0. OPEN - awaiting Team A (order 3).

    // 5. screen-fade state machine 0x0059f8c0 (Sec26.23, Sec26.9):
    //    es.fadeState().g012e6aa0 / g012e6aa4 / g012e6aa8 / g013effc8 /
    //    g013effcc / g013effd0. OPEN - awaiting Team A (order 3).
}

} // namespace sr3luahost
