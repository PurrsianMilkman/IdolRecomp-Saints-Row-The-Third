// Animation runtime U1: the cross-fade step (spec-anim-runtime.md Sec6.7
// step 2). See crossfade.h for the rules and constants.
#include "sr3animruntime/crossfade.h"

#include <stdexcept>

namespace sr3animruntime {

float CrossFadeBaseRate(float durationSeconds) {
    if (durationSeconds == 0.0f) return 1.0f;
    return kFrameTime / durationSeconds;
}

double CrossFadeCrowdingFactor(size_t memberCount) {
    if (memberCount < 3) return 1.0;
    const double nm1 = static_cast<double>(memberCount - 1);
    return kCrowdingBase + kCrowdingStep * nm1 * nm1;
}

float CrossFadeRate(float durationSeconds, size_t memberCount, std::optional<float> restartLayerWeight) {
    float r = CrossFadeBaseRate(durationSeconds);
    if (restartLayerWeight && memberCount > 1)
        r = (*restartLayerWeight < 1.0f) ? kRestartFrozenRate : kRestartCompleteRate;
    if (memberCount >= 3) r = static_cast<float>(static_cast<double>(r) * CrossFadeCrowdingFactor(memberCount));
    return r;
}

std::vector<size_t> CrossFadeSortOrder(const std::vector<float>& weights) {
    std::vector<size_t> order;
    order.reserve(weights.size());
    for (size_t i = 0; i < weights.size(); ++i) {
        // Insert after every element whose weight is >= this one (after equals).
        size_t pos = order.size();
        while (pos > 0 && weights[order[pos - 1]] < weights[i]) --pos;
        order.insert(order.begin() + static_cast<std::ptrdiff_t>(pos), i);
    }
    return order;
}

void CrossFadeRedistribute(std::vector<float>& weights, size_t current, float rate) {
    if (current >= weights.size()) throw std::out_of_range("CrossFadeRedistribute: current index");
    const std::vector<size_t> sorted = CrossFadeSortOrder(weights);

    float& cw = weights[current];
    cw = cw + rate;
    if (cw > 1.0f) cw = 1.0f;

    std::vector<size_t> others;
    others.reserve(sorted.size());
    for (size_t idx : sorted)
        if (idx != current) others.push_back(idx);

    float b = rate;
    for (size_t k = others.size(); k-- > 0;) {
        float& w = weights[others[k]];
        const float s = (k == 1) ? b / 2.0f : b;
        if (static_cast<double>(s) + kSnapMargin > static_cast<double>(w)) {
            b -= w;
            w = 0.0f;
        } else {
            w -= s;
            b -= s;
        }
    }
}

}  // namespace sr3animruntime
