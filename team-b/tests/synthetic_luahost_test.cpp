// Synthetic tests for sr3luahost (include/sr3luahost/{lua_c_api,
// stub_registry, host}.h). Every fixture below is small and hand-written -
// no real shipped game data or real registration-list file is read here
// (that is exercised for real by tools/lua_host_run.cpp against the
// current real tagged registration list - originally
// tools/lua_all_registered_1430_tagged.txt, then
// tools/lua_all_registered_1435_tagged.txt, superseded again 2026-09-29
// (same day, later pass) by tools/lua_all_registered_1490_tagged.txt
// (the 55-name vint_* registrar, spec-lua-bindings.md Sec13.7) - and the
// real archives; this suite only proves the scaffold's own mechanics:
// parsing the tagged format, registering stubs into the right state, the
// stub's own logging contract, real luaL_loadbuffer/lua_pcall outcomes for
// both a clean script and a genuinely broken one, and the 22 names
// promoted from a generic logged-nil stub to real, spec-confirmed
// behavior (13 from the original pass + 9 from a same-shape follow-up
// pass, spec-lua-api-behaviour.md Sec10.1-Sec10.9) - see
// spec_confirmed_stubs.h/engine_state.h).

#include "synthetic_luahost/common.h"


// NOTE (2026-10-08, stack-overflow fix): this file used to be one giant
// main() holding all 22 test blocks' locals (many of them Host/EngineState
// by value, each ~40+ KB) in a single stack frame. EngineState has grown
// with every wave-2 batch merged so far and will keep growing with the
// batches still pending (W2-2, W2-8) - the cumulative frame crossed the
// default 1 MiB thread stack and crashed with STATUS_STACK_OVERFLOW before
// main() was even entered (verified: the same crash reproduces building
// this exact, unmodified file against the pre-W2-6 revision in isolation).
// Splitting each `{ ... }` block into its own function means only the
// largest SINGLE block's own locals are live on the stack at once, instead
// of the sum of all 22 - the correct fix for a problem that will otherwise
// recur with every future batch that adds more EngineState fields, not a
// one-off stack-size bump.

int main() {
    test_block_1();
    test_block_2();
    test_block_3();
    test_block_4();
    test_block_5();
    test_block_6();
    test_block_7();
    test_block_8();
    test_block_9();
    test_block_10();
    test_block_11();
    test_block_12();
    test_block_13();
    test_block_14();
    test_block_15();
    test_block_16();
    test_block_17();
    test_block_18();
    test_block_19();
    test_block_20();
    test_block_21();
    test_block_22();

    if (g_failures == 0) {
        std::cout << "ALL sr3luahost SYNTHETIC TESTS PASSED\n";
        return 0;
    }
    std::cerr << g_failures << " CHECK(S) FAILED\n";
    return 1;
}
