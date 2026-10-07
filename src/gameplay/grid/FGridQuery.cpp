#include "gameplay/grid/FGridQuery.h"

#include "gameplay/grid/FGridTypes.h"
#include "gameplay/grid/FGridTerrain.h"
#include "ecs/components/FDead.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <queue>

namespace game {

entt::entity FindGridOccupant(entt::registry& registry, int x, int y) {
    const auto view = registry.view<const FGridPosition>();
    for (const auto entity : view) {
        if (registry.all_of<FDead>(entity)) continue;
        const auto& position = view.get<const FGridPosition>(entity);
        if (position.x == x && position.y == y) return entity;
    }
    return entt::null;
}

bool IsGridCellOccupied(entt::registry& registry, int x, int y) {
    return FindGridOccupant(registry, x, y) != entt::null;
}

namespace {
struct FPathSearch {
    std::array<int, kGridWidth * kGridHeight> distance;
    std::array<int, kGridWidth * kGridHeight> parent;
};

FPathSearch Search(entt::registry& registry, entt::entity unit, int maxDistance) {
    FPathSearch result;
    result.distance.fill(-1);
    result.parent.fill(-1);
    if (!registry.valid(unit) || !registry.all_of<FGridPosition>(unit) || maxDistance <= 0) return result;
    const auto position = registry.get<FGridPosition>(unit);
    if (position.x < 0 || position.y < 0 || position.x >= kGridWidth || position.y >= kGridHeight) return result;
    const auto* metrics = registry.ctx().find<FGridMetrics>();
    const auto* terrain = registry.ctx().find<FGridTerrain>();
    const FGridMetrics defaultMetrics{};
    const auto& grid = metrics ? *metrics : defaultMetrics;
    std::queue<FGridPosition> pending;
    pending.push(position);
    result.distance[position.y * kGridWidth + position.x] = 0;
    while (!pending.empty()) {
        const auto current = pending.front();
        pending.pop();
        const int index = current.y * kGridWidth + current.x;
        if (result.distance[index] >= maxDistance) continue;
        for (const auto next : OrthogonalNeighbors(current)) {
            if (!IsInsideGrid(grid, next.x, next.y) || next.x >= kGridWidth || next.y >= kGridHeight ||
                (terrain && terrain->at(next.x, next.y) == ETile::Wall) ||
                IsGridCellOccupied(registry, next.x, next.y)) continue;
            const int nextIndex = next.y * kGridWidth + next.x;
            if (result.distance[nextIndex] != -1) continue;
            result.distance[nextIndex] = result.distance[index] + 1;
            result.parent[nextIndex] = index;
            pending.push(next);
        }
    }
    return result;
}
} // namespace

std::vector<FGridPosition> ComputeReachableCells(entt::registry& registry, entt::entity unit, int maxDistance) {
    const auto search = Search(registry, unit, maxDistance);
    std::vector<FGridPosition> reachable;
    for (int index = 0; index < kGridWidth * kGridHeight; ++index) {
        if (search.distance[index] > 0) reachable.push_back({index % kGridWidth, index / kGridWidth});
    }
    return reachable;
}

std::vector<FGridPosition> FindGridPath(
    entt::registry& registry, entt::entity unit, FGridPosition destination, int maxDistance) {
    if (destination.x < 0 || destination.y < 0 || destination.x >= kGridWidth || destination.y >= kGridHeight) return {};
    const auto search = Search(registry, unit, maxDistance);
    int index = destination.y * kGridWidth + destination.x;
    if (search.distance[index] <= 0) return {};
    std::vector<FGridPosition> path;
    while (search.parent[index] != -1) {
        path.push_back({index % kGridWidth, index / kGridWidth});
        index = search.parent[index];
    }
    std::ranges::reverse(path);
    return path;
}

bool HasGridLineOfSight(entt::registry& registry, FGridPosition from, FGridPosition to) {
    const auto* terrain = registry.ctx().find<FGridTerrain>();
    if (!terrain) return true;
    int x = from.x;
    int y = from.y;
    const int dx = to.x - from.x;
    const int dy = to.y - from.y;
    const int nx = std::abs(dx);
    const int ny = std::abs(dy);
    const int sx = dx > 0 ? 1 : -1;
    const int sy = dy > 0 ? 1 : -1;
    int ix = 0;
    int iy = 0;
    // Supercover traversal also checks both cells touched at a diagonal corner.
    while (ix < nx || iy < ny) {
        const int decision = (1 + 2 * ix) * ny - (1 + 2 * iy) * nx;
        if (decision == 0) {
            if (terrain->at(x + sx, y) == ETile::Wall || terrain->at(x, y + sy) == ETile::Wall) return false;
            x += sx; y += sy; ++ix; ++iy;
        } else if (decision < 0) {
            x += sx; ++ix;
        } else {
            y += sy; ++iy;
        }
        if (terrain->at(x, y) == ETile::Wall) return false;
    }
    return true;
}

} // namespace game
