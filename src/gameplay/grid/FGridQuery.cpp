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

FPushResult ComputePushDestination(
    entt::registry& registry, entt::entity target, FGridPosition from, int distance,
    EDisplacementDirection direction) {
    FPushResult result;
    const auto* cell = registry.try_get<FGridPosition>(target);
    if (cell == nullptr) {
        return result;
    }
    result.finalCell = *cell;
    result.path.push_back(*cell);
    if (distance <= 0 || (*cell == from)) {
        return result; // no direction to speak of (or a zero-length push): nothing moves
    }

    // The step: the dominant axis of the from -> target delta (tie = x axis); a pull is the same step
    // reversed. Only one axis ever changes, so a push of an orthogonally adjacent target goes straight back.
    const int dx = cell->x - from.x;
    const int dy = cell->y - from.y;
    int stepX = 0;
    int stepY = 0;
    if (std::abs(dx) >= std::abs(dy) && dx != 0) {
        stepX = dx > 0 ? 1 : -1;
    } else if (dy != 0) {
        stepY = dy > 0 ? 1 : -1;
    } else {
        return result; // unreachable: from == *cell was handled above
    }
    if (direction == EDisplacementDirection::Toward) {
        stepX = -stepX;
        stepY = -stepY;
    }

    const auto* metrics = registry.ctx().find<FGridMetrics>();
    const FGridMetrics fallback{};
    const FGridMetrics& grid = metrics != nullptr ? *metrics : fallback;
    const auto* terrain = registry.ctx().find<FGridTerrain>();

    FGridPosition current = result.finalCell;
    for (int step = 1; step <= distance; ++step) {
        const FGridPosition next{current.x + stepX, current.y + stepY};
        // A pull never carries the target past the caster on the step axis: the last cell before the
        // caster's column/row is the intended stop (no collision). A non-aligned pull cannot be stopped by
        // the caster's own cell alone — without this rule it would walk on past the caster.
        const bool pastCaster =
            direction == EDisplacementDirection::Toward &&
            ((stepX > 0 && next.x > from.x) || (stepX < 0 && next.x < from.x) ||
             (stepY > 0 && next.y > from.y) || (stepY < 0 && next.y < from.y));
        if (pastCaster) {
            break;
        }
        // Same two boundaries the path search speaks: the metrics grid and the compile-time array size.
        const bool insideGrid = IsInsideGrid(grid, next.x, next.y) && next.x < kGridWidth && next.y < kGridHeight;
        const bool wall = terrain != nullptr && terrain->at(next.x, next.y) == ETile::Wall;
        const bool occupied = IsGridCellOccupied(registry, next.x, next.y);
        if (!insideGrid || wall || occupied) {
            // A pull stopping on the caster's own cell is the intended stop, not a collision.
            const bool intendedStop =
                direction == EDisplacementDirection::Toward && next.x == from.x && next.y == from.y;
            if (!intendedStop) {
                result.hitObstacle = true;
            }
            break;
        }
        current = next;
        result.path.push_back(current);
    }
    result.finalCell = current;
    return result;
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
