#pragma once

// sr3animruntime/crossfade.h - the cross-fade step of the controller update
// 0x004b3720 (spec-anim-runtime.md Sec6.7 step 2), as pure functions over a
// plain list of member weights so it can be tested without a controller.
// Controller::update() (controller.h) gathers the flag-0x02 slots and calls
// these; nothing here touches a slot.
//
// Numeric constants, all quoted from Sec6.7:
//   * frame time: the constant 1/30 at 0x0132a0ac (a float), NOT the
//     controller's time step +0x2c;
//   * restart override rates 0.0001 and 100.0;
//   * crowding factor 0.5 + 0.2 * (n - 1)^2, where 0.2 is "the double nearest
//     float 0.2" (0x012a2e30) and 0.5 is 0x012a2dc0;
//   * the 0.001 snap-to-zero margin of the redistribution walk.

#include <cstddef>
#include <optional>
#include <vector>

namespace sr3animruntime {

// 0x0132a0ac, float 1/30.
inline constexpr float kFrameTime = 1.0f / 30.0f;
// 0x012a2e30: the double nearest float 0.2 (NOT the double nearest 0.2).
inline constexpr double kCrowdingStep = static_cast<double>(0.2f);
// 0x012a2dc0.
inline constexpr double kCrowdingBase = 0.5;
inline constexpr float kRestartFrozenRate = 0.0001f;
inline constexpr float kRestartCompleteRate = 100.0f;
inline constexpr double kSnapMargin = 0.001;

// Step 2.1: r = 1.0 when the duration (+0x58) is exactly 0.0, else
// (1/30) / duration.
float CrossFadeBaseRate(float durationSeconds);

// Step 2.3: 0.5 + 0.2 * (n - 1)^2 for n >= 3 members (n counts the current
// slot), else 1.0. x1.3 at n = 3, x2.3 at n = 4, x3.7 at n = 5.
double CrossFadeCrowdingFactor(size_t memberCount);

// Steps 2.1-2.3 together - the value stored at +0x5c (step 2.4).
//   restartLayerWeight: the layer weight (0x004bdb50, layer +0x24) of the
//   FIRST in-use slot carrying slot flag 0x400, or nullopt when no slot
//   carries it. The override applies only when memberCount > 1: r = 0.0001
//   while that weight is below 1.0, else 100.0. The crowding factor is then
//   applied to whichever r resulted.
float CrossFadeRate(float durationSeconds, size_t memberCount, std::optional<float> restartLayerWeight);

// 0x004afe50's order: an insertion sort by DESCENDING weight that places a
// new element after equal weights - i.e. a stable descending sort. Returns
// indices into `weights`.
std::vector<size_t> CrossFadeSortOrder(const std::vector<float>& weights);

// Steps 2.5-2.6 on `weights` (one entry per cross-fade member, in slot
// order); `current` indexes the current slot (+0x50) among them.
//   * current: w <- min(w + r, 1.0);
//   * the others, sorted heaviest (k = 0) .. lightest (k = m - 1), with a
//     budget b = r, walked k = m - 1 down to 0: step s = b, EXCEPT s = b / 2
//     at k = 1; if s + 0.001 > w_k then b -= w_k, w_k = 0; else w_k -= s,
//     b -= s.
// While the others hold enough weight they lose exactly r between them, so
// the group's total is conserved. Note the halving is at k = 1 - the SECOND
// HEAVIEST of the others - which is the second-lightest only when there are
// two or three others.
void CrossFadeRedistribute(std::vector<float>& weights, size_t current, float rate);

}  // namespace sr3animruntime
