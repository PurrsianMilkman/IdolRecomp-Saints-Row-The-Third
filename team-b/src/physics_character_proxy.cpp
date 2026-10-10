// sr3physics/character_proxy.h implementation (the non-template pieces).
// spec-physics-runtime.md Sec4.6 item 3 / spec-player-runtime.md Sec5.6.4 -
// see the header's own doc comments for citations.
#include "sr3physics/character_proxy.h"

namespace sr3physics {

int CharacterProxyStepper::run(double frameDt, const SolvePass& solve) {
    std::vector<ProxyState> proxies(1, ProxyState{frameDt, 0});
    const int passes = runBatch(proxies, solve);
    return passes;
}

int CharacterProxyStepper::runBatch(std::vector<ProxyState>& proxies, const SolvePass& solve) {
    int pass = 0;
    for (; pass < kMaxPasses; ++pass) {
        bool anyActive = false;
        for (auto& p : proxies) {
            if (p.remaining > kRemainingTimeEpsilon) {
                const double consumed = solve ? solve(p.remaining) : p.remaining;
                p.remaining -= consumed;
                ++p.passesRun;
                anyActive = true;
            }
        }
        if (!anyActive) break;  // no proxy had time remaining - stop before the cap
    }
    return pass;
}

}  // namespace sr3physics
