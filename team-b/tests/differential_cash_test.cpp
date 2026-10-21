// Team D differential vectors, cash wallet add (spec-differential-results.md D-22, spec-differential-vectors-cash.md).
#include <cstdint>
#include <cstdio>

#include "diff_vectors.h"
#include "sr3player/vitals.h"

int main() {
    int total = 0, fail = 0;
    for (const auto& r : dv::rows("spec-differential-vectors-cash.md")) {
        double cash, add, mult, exp;
        if (r.size() < 4 || !dv::num(r[0], cash) || !dv::num(r[1], add) || !dv::num(r[2], mult) || !dv::num(r[3], exp)) continue;
        ++total;
        const int32_t got = sr3player::AddCashWallet(static_cast<int32_t>(cash), static_cast<int32_t>(add), static_cast<float>(mult), true);
        if (got != static_cast<int32_t>(exp)) {
            if (++fail <= 10) std::printf("FAIL cash %.0f add %.0f mult %s: got %d expected %.0f\n", cash, add, r[2].c_str(), got, exp);
        }
    }
    // Non-local players and negative amounts are not scaled.
    if (sr3player::AddCashWallet(1000, 500, 1.35f, false) != 1500) { std::printf("FAIL non-local\n"); ++fail; }
    if (sr3player::AddCashWallet(1000, -500, 1.35f, true) != 500) { std::printf("FAIL negative\n"); ++fail; }
    std::printf("cash vectors: %d, failures: %d\n", total, fail);
    return (fail || total < 600) ? 1 : 0;
}
