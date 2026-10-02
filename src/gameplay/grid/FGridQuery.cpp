#include "gameplay/grid/FGridQuery.h"

#include "gameplay/grid/FGridTypes.h"

namespace game {

entt::entity FindGridOccupant(entt::registry& registry, int x, int y) {
    const auto view = registry.view<const FGridPosition>();
    for (const auto entity : view) {
        const auto& position = view.get<const FGridPosition>(entity);
        if (position.x == x && position.y == y) return entity;
    }
    return entt::null;
}

bool IsGridCellOccupied(entt::registry& registry, int x, int y) {
    return FindGridOccupant(registry, x, y) != entt::null;
}

std::vector<FGridPosition> ComputeReachableCells(entt::registry& registry, entt::entity unit, int moveAp) {
    if (moveAp <= 0 || !registry.valid(unit) || !registry.all_of<FGridPosition>(unit)) return {};

    const auto& position = registry.get<FGridPosition>(unit);
    const auto* metrics = registry.ctx().find<FGridMetrics>();
    const FGridMetrics defaultMetrics{};
    const auto& grid = metrics ? *metrics : defaultMetrics;
    std::vector<FGridPosition> reachable;
    for (const auto neighbor : OrthogonalNeighbors(position)) {
        if (IsInsideGrid(grid, neighbor.x, neighbor.y) &&
            !IsGridCellOccupied(registry, neighbor.x, neighbor.y)) {
            reachable.push_back(neighbor);
        }
    }
    return reachable;
}

} // namespace game
