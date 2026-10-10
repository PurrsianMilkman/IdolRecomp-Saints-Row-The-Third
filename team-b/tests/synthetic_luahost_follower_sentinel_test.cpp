// Synthetic tests for the "#FOLLOWER#"/"#FOLLOWER1#"/"#FOLLOWER2#"/
// "#FOLLOWER3#" name-resolution sentinel family (spec-lua-api-behaviour.md
// Sec3 front matter, line ~1469-1470, "Name-resolution sentinels" - the
// 2nd-5th of the 8-sentinel CLOSED inventory commit 1681666 establishes by
// a whole-`.rdata` regex sweep plus a raw byte search) and the shared
// resolver helper this pass (2026-10-08, follower-sentinel-resolver pass)
// adds - isFollowerSentinelLiteral()/resolveFollowerAwareObject()
// (lua_spec_confirmed_stubs_common.h) - and wires into roughly two dozen
// existing natives across many different stub batches (see those natives'
// own doc comments, each citing "wired 2026-10-08, follower-sentinel-
// resolver pass", for the full per-call-site list).
//
// What this file does NOT claim, and tests accordingly:
//
//   - The exact list-index each of the 4 literals maps to is NOT spec-
//     confirmed (see EngineState::PartyState::followerSentinelIndex's own
//     doc comment, engine_state.h, for the full citation trail on the two
//     genuinely open questions: bare-#FOLLOWER#-vs-#FOLLOWER1# aliasing,
//     and 0- vs 1-indexing). Every index this file sets below
//     (kFollower1Index/kFollower2Index/kFollower3Index/kFollowerIndex) is
//     this TEST's OWN arbitrary choice to exercise the mechanism end-to-
//     end once an index IS known - NOT an assertion about the real
//     engine's convention. The refusal tests further below (before any
//     index is set) are the actual spec-confirmed behavior for this case.
//
//   - The backing companion list itself (EngineState::PartyState::
//     membersByLeader, keyed "#PLAYER1#") is this project's own CHOSEN,
//     HIGH-CONFIDENCE-but-not-spec-confirmed stand-in for "a linked
//     companion/homie list" (Sec3's own words) - see the same doc comment.
//
// Same hand-written-fixture convention as the other per-batch luahost
// tests: a small RegisteredName fixture, no real tagged-registration file
// or archive read. The 4 natives exercised below (npc_is_in_party,
// ai_set_in_scripted_cover, set_always_cower_flag, follower_make_
// independent) are chosen because each was individually wired this same
// pass and each already has its OWN, separate synthetic coverage for its
// non-follower behavior elsewhere - this file only adds the follower-
// sentinel angle, not a re-test of each native's full behavior.
#include <iostream>
#include <string>
#include <vector>

#include "sr3luahost/engine_state.h"
#include "sr3luahost/host.h"
#include "sr3luahost/stub_registry.h"

using namespace sr3luahost;

namespace {

int g_failures = 0;
#define CHECK(cond)                                                                           \
    do {                                                                                      \
        if (!(cond)) {                                                                        \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                     \
        }                                                                                     \
    } while (0)

void ok(Host& host, lua_State* L, const std::string& code) {
    auto r = host.runChunk(L, code, "follower_sentinel_test.lua");
    if (!(r.loadOk && r.pcallOk)) {
        std::cerr << "chunk failed: " << (r.loadOk ? r.pcallError : r.loadError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

void fails(Host& host, lua_State* L, const std::string& code, const std::string& needle) {
    auto r = host.runChunk(L, code, "follower_sentinel_test.lua");
    bool good = r.loadOk && !r.pcallOk && r.pcallError.find(needle) != std::string::npos;
    if (!good) {
        std::cerr << "expected error containing '" << needle << "', got: "
                  << (r.pcallOk ? std::string("(success)") : r.pcallError) << "\n  in: " << code << "\n";
        ++g_failures;
    }
}

std::vector<RegisteredName> fixture() {
    return {
        {"npc_is_in_party", "gameplay"},
        {"ai_set_in_scripted_cover", "gameplay"},
        {"set_always_cower_flag", "gameplay"},
        {"follower_make_independent", "gameplay"},
    };
}

} // namespace

int main() {
    Host host(fixture());
    lua_State* gp = host.gameplayState();
    EngineState& es = host.engineState();

    // ==================================================================
    // Sentinel path takes PRIORITY over the ordinary flat name registry:
    // deliberately pre-register "#FOLLOWER1#" itself as a resolvable
    // ordinary name (exactly the mistake a test - or a stray real zone
    // record - could make) and confirm it is NEVER consulted: resolution
    // of the literal must go exclusively through the companion-list path,
    // not objectResolves().get("#FOLLOWER1#").
    // ==================================================================
    es.objectResolves().set("#FOLLOWER1#", true); // a deliberately WRONG registry entry - must be ignored

    // ==================================================================
    // OPEN refusal: each of the 4 literals refuses (OpenStateError, via
    // openGuard, surfaces as a Lua error naming the literal) until ITS OWN
    // index slot is set - independently of the others, and independently
    // of the bogus objectResolves() entry above.
    // ==================================================================
    fails(host, gp, "npc_is_in_party('#FOLLOWER1#')", "#FOLLOWER1#");
    fails(host, gp, "npc_is_in_party('#FOLLOWER2#')", "#FOLLOWER2#");
    fails(host, gp, "npc_is_in_party('#FOLLOWER3#')", "#FOLLOWER3#");
    fails(host, gp, "npc_is_in_party('#FOLLOWER#')", "#FOLLOWER#");
    // Case-insensitive recognition (same portable __stricmp-equivalent
    // convention every other sentinel literal in this project uses) -
    // still refuses as OPEN, not silently treated as an ordinary name.
    fails(host, gp, "npc_is_in_party('#follower2#')", "#FOLLOWER2#");

    // ==================================================================
    // Set each literal's own index (this test's OWN arbitrary choice - see
    // this file's own top comment). Bare "#FOLLOWER#" is given the SAME
    // index as "#FOLLOWER1#" here purely as one exercised convention this
    // test picks for coverage - NOT a claim that this is the real engine's
    // choice (genuinely OPEN per spec, see followerSentinelIndex's own doc
    // comment).
    // ==================================================================
    constexpr int kFollower1Index = 0;
    constexpr int kFollower2Index = 1;
    constexpr int kFollower3Index = 2;
    constexpr int kFollowerIndex = 0; // test's own choice: aliased to index 0, same as #FOLLOWER1# above
    es.party().followerSentinelIndex.set("#FOLLOWER1#", kFollower1Index);
    es.party().followerSentinelIndex.set("#FOLLOWER2#", kFollower2Index);
    es.party().followerSentinelIndex.set("#FOLLOWER3#", kFollower3Index);
    es.party().followerSentinelIndex.set("#FOLLOWER#", kFollowerIndex);

    // ==================================================================
    // Empty companion list (size 0): the index IS now known, but there is
    // no live entry there - a genuine resolve FAILURE (pushes false), NOT
    // an OPEN refusal. This is also the proof that the bogus
    // objectResolves().set("#FOLLOWER1#", true) entry above is never what
    // makes resolution succeed: if it were consulted, these would wrongly
    // report true.
    // ==================================================================
    CHECK(es.party().membersByLeader["#PLAYER1#"].empty());
    ok(host, gp, "assert(npc_is_in_party('#FOLLOWER1#') == false)");
    ok(host, gp, "assert(npc_is_in_party('#FOLLOWER2#') == false)");
    ok(host, gp, "assert(npc_is_in_party('#FOLLOWER3#') == false)");
    ok(host, gp, "assert(npc_is_in_party('#FOLLOWER#') == false)");

    // ==================================================================
    // Size 1: only index 0 (#FOLLOWER1# and, by this test's own chosen
    // aliasing, bare #FOLLOWER#) has a live entry.
    // ==================================================================
    es.objectResolves().set("homie_a", true);
    es.party().membersByLeader["#PLAYER1#"] = {"homie_a"};
    ok(host, gp, "assert(npc_is_in_party('#FOLLOWER1#') == true)");
    ok(host, gp, "assert(npc_is_in_party('#FOLLOWER#') == true)");
    ok(host, gp, "assert(npc_is_in_party('#FOLLOWER2#') == false)");
    ok(host, gp, "assert(npc_is_in_party('#FOLLOWER3#') == false)");

    // ==================================================================
    // Size 2: indices 0/1 now live.
    // ==================================================================
    es.objectResolves().set("homie_b", true);
    es.party().membersByLeader["#PLAYER1#"] = {"homie_a", "homie_b"};
    ok(host, gp, "assert(npc_is_in_party('#FOLLOWER1#') == true)");
    ok(host, gp, "assert(npc_is_in_party('#FOLLOWER2#') == true)");
    ok(host, gp, "assert(npc_is_in_party('#FOLLOWER3#') == false)");

    // ==================================================================
    // Size 3+ (4 members seeded, only indices 0-2 exercised by the 3
    // numbered literals): every numbered literal now resolves.
    // ==================================================================
    es.objectResolves().set("homie_c", true);
    es.objectResolves().set("homie_d", true);
    es.party().membersByLeader["#PLAYER1#"] = {"homie_a", "homie_b", "homie_c", "homie_d"};
    ok(host, gp, "assert(npc_is_in_party('#FOLLOWER1#') == true)");
    ok(host, gp, "assert(npc_is_in_party('#FOLLOWER2#') == true)");
    ok(host, gp, "assert(npc_is_in_party('#FOLLOWER3#') == true)");

    // ==================================================================
    // A plain, ordinary (non-sentinel) name is completely unaffected by
    // any of the above - still goes through the ordinary objectResolves()
    // path exactly as before this pass, proving the new sentinel handling
    // is additive, not a behavior change for real registered names.
    // ==================================================================
    es.objectResolves().set("ed_ordinary", true);
    ok(host, gp, "assert(npc_is_in_party('ed_ordinary') == false)"); // resolves fine, just not a party member
    es.objectResolves().set("ed_unresolved", false);
    ok(host, gp, "assert(npc_is_in_party('ed_unresolved') == false)");

    // ==================================================================
    // The RESOLVED companion's own name is what downstream state keys on -
    // never the raw literal string - verified via two different setters
    // that write into CharacterState, and by confirming the literal itself
    // never becomes a CharacterState key.
    // ==================================================================
    ok(host, gp, "set_always_cower_flag('#FOLLOWER2#', true)");
    CHECK(es.hasCharacter("homie_b"));
    CHECK(es.getOrCreateCharacter("homie_b").alwaysCowerFlag == true);
    CHECK(!es.hasCharacter("#FOLLOWER2#")); // the raw literal must never become a CharacterState key in its own right

    ok(host, gp, "assert(ai_set_in_scripted_cover('#FOLLOWER1#', true) == true)");
    CHECK(es.getOrCreateCharacter("homie_a").inScriptedCover.known() &&
          es.getOrCreateCharacter("homie_a").inScriptedCover.get() == true);

    ok(host, gp, "follower_make_independent('#FOLLOWER3#', true)");
    CHECK(es.getOrCreateCharacter("homie_c").independentFollower.known() &&
          es.getOrCreateCharacter("homie_c").independentFollower.get() == true);
    CHECK(!es.hasCharacter("#FOLLOWER3#"));

    // ==================================================================
    // Bare "#FOLLOWER#" independently refuses/resolves on its OWN slot,
    // not by inheriting #FOLLOWER1#'s - demonstrated by forgetting only
    // #FOLLOWER#'s own index and confirming #FOLLOWER1# still resolves
    // fine while bare #FOLLOWER# now refuses again.
    // ==================================================================
    es.party().followerSentinelIndex.forget("#FOLLOWER#");
    fails(host, gp, "npc_is_in_party('#FOLLOWER#')", "#FOLLOWER#");
    ok(host, gp, "assert(npc_is_in_party('#FOLLOWER1#') == true)"); // unaffected - independent OPEN slot

    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "synthetic_luahost_follower_sentinel_test: OK\n";
    return 0;
}
