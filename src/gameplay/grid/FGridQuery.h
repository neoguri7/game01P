#pragma once

#include <entt/entt.hpp>
#include <vector>

#include "ecs/components/FGridPosition.h"

namespace game {

[[nodiscard]] entt::entity FindGridOccupant(entt::registry& registry, int x, int y);
[[nodiscard]] bool IsGridCellOccupied(entt::registry& registry, int x, int y);
[[nodiscard]] std::vector<FGridPosition> ComputeReachableCells(
    entt::registry& registry,
    entt::entity unit,
    int moveAp
);

} // namespace game
