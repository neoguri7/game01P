#pragma once

#include "gameplay/components/FGridPosition.h"

#include <cstdlib>

namespace game::gameplay {

/// Distance between two cells, used for every 사거리 check.
/// why one function: the *numbers* are data (미결(design §4): 사거리 수치) but the *metric* must be declared
/// once — switching Chebyshev → Manhattan later is a one-file change with one place to re-review (R12).
/// Metric: Chebyshev (max of the axis deltas), i.e. 대각선도 1칸으로 센다.
[[nodiscard]] inline int gridDistance(const FGridPosition& from, const FGridPosition& to) {
    const int dx = std::abs(from.x - to.x);
    const int dy = std::abs(from.y - to.y);
    return dx > dy ? dx : dy;
}

} // namespace game::gameplay
