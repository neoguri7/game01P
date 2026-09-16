#pragma once

#include "gameplay/components/FGridPosition.h"

#include <entt/entt.hpp>

namespace game::gameplay {

/// True when any unit already stands on that cell.
/// 미결(design §4): 쓰러진 유닛이 칸을 막는지 여부 — 지금은 막는다(부상·사망이 없어 시체가 남지 않으므로
/// 칸을 비우는 규칙을 지금 정하면 나중에 되돌리기 어렵다). 그 결정이 나오면 이 함수 한 곳만 고친다.
/// why a query here and not inside the move system: the target-selection/spawn validation will need the same
/// answer, and two copies of "is this cell free" is exactly the drift R12 forbids.
[[nodiscard]] inline bool gridCellOccupied(entt::registry& registry, int x, int y) {
    for (auto [entity, position] : registry.view<FGridPosition>().each()) {
        (void)entity;
        if (position.x == x && position.y == y) {
            return true;
        }
    }
    return false;
}

} // namespace game::gameplay
