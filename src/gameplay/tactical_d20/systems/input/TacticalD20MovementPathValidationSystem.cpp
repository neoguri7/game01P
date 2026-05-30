#include "gameplay/tactical_d20/systems/input/TacticalD20MovementPathValidationSystem.h"

#include "core/events/FEventPublishing.h"
#include "ecs/components/FUnitStateDefeated.h"
#include "ecs/components/FTacticalBoardTile.h"
#include "ecs/components/FTacticalUnit.h"
#include "ecs/components/FTurnBudget.h"
#include "gameplay/tactical_d20/events/FTacticalD20CommandEvents.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fmt/format.h>
#include <string>
#include <vector>
#include <tracy/Tracy.hpp>

namespace game {
namespace {

struct FPathValidation {
    bool valid{false};
    std::string reason;
    int costTiles{0};
    int budgetTiles{0};
};

struct FSearchNode {
    int tileX{0};
    int tileY{0};
    int costTiles{0};
};

entt::entity TileAt(entt::registry& registry, int tileX, int tileY) {
    auto view = registry.view<FTacticalBoardTile>();
    for (auto entity : view) {
        const auto& tile = view.get<FTacticalBoardTile>(entity);
        if (tile.tileX == tileX && tile.tileY == tileY) return entity;
    }
    return entt::null;
}

entt::entity OccupyingUnitAt(entt::registry& registry, int tileX, int tileY, entt::entity ignoredUnit) {
    auto view = registry.view<FTacticalUnit>(entt::exclude<FUnitStateDefeated>);
    for (auto entity : view) {
        if (entity == ignoredUnit) continue;
        const auto& unit = view.get<FTacticalUnit>(entity);
        if (unit.tileX == tileX && unit.tileY == tileY) return entity;
    }
    return entt::null;
}

bool IsWall(entt::registry& registry, int tileX, int tileY) {
    const auto tileEntity = TileAt(registry, tileX, tileY);
    return tileEntity != entt::null && registry.get<FTacticalBoardTile>(tileEntity).isWall;
}

bool IsBlockedPathTile(entt::registry& registry, entt::entity unit, int tileX, int tileY) {
    return TileAt(registry, tileX, tileY) == entt::null
        || IsWall(registry, tileX, tileY)
        || OccupyingUnitAt(registry, tileX, tileY, unit) != entt::null;
}

bool WasVisited(const std::vector<FSearchNode>& nodes, int tileX, int tileY) {
    return std::ranges::any_of(nodes, [tileX, tileY](const FSearchNode& node) {
        return node.tileX == tileX && node.tileY == tileY;
    });
}

int FindShortestNoDiagonalPathCost(entt::registry& registry,
                                   entt::entity unit,
                                   int startX,
                                   int startY,
                                   int targetX,
                                   int targetY,
                                   int budgetTiles) {
    constexpr std::array<FSearchNode, 4> Directions{{
        {.tileX = 1, .tileY = 0},
        {.tileX = -1, .tileY = 0},
        {.tileX = 0, .tileY = 1},
        {.tileX = 0, .tileY = -1},
    }};

    std::vector<FSearchNode> frontier{{.tileX = startX, .tileY = startY, .costTiles = 0}};
    for (std::size_t cursor = 0; cursor < frontier.size(); ++cursor) {
        const auto current = frontier[cursor];
        if (current.tileX == targetX && current.tileY == targetY) return current.costTiles;
        if (current.costTiles >= budgetTiles) continue;

        for (const auto& direction : Directions) {
            const FSearchNode next{
                .tileX = current.tileX + direction.tileX,
                .tileY = current.tileY + direction.tileY,
                .costTiles = current.costTiles + 1,
            };
            if (WasVisited(frontier, next.tileX, next.tileY)) continue;
            if (next.tileX != targetX || next.tileY != targetY) {
                if (IsBlockedPathTile(registry, unit, next.tileX, next.tileY)) continue;
            }
            frontier.push_back(next);
        }
    }

    return -1;
}

FPathValidation ValidateMovement(entt::registry& registry, const FTacticalD20CommandDropRequestedEvent& request) {
    FPathValidation result;

    if (!request.hasTargetTile) {
        result.reason = "move drop outside board";
        return result;
    }
    if (request.unit == entt::null || !registry.valid(request.unit) || !registry.all_of<FTacticalUnit, FTurnBudget>(request.unit)) {
        result.reason = "active unit is unavailable";
        return result;
    }

    const auto& unit = registry.get<FTacticalUnit>(request.unit);
    const auto& budget = registry.get<FTurnBudget>(request.unit);
    result.costTiles = std::abs(request.targetTileX - unit.tileX) + std::abs(request.targetTileY - unit.tileY);
    result.budgetTiles = std::max(budget.movementBudgetTiles, 0);

    const auto tileEntity = TileAt(registry, request.targetTileX, request.targetTileY);
    if (tileEntity == entt::null) {
        result.reason = "move target is outside board";
        return result;
    }
    if (registry.get<FTacticalBoardTile>(tileEntity).isWall) {
        result.reason = "move target is a wall tile";
        return result;
    }
    if ((request.targetTileX == unit.tileX && request.targetTileY == unit.tileY)
        || OccupyingUnitAt(registry, request.targetTileX, request.targetTileY, request.unit) != entt::null) {
        result.reason = "move target is occupied";
        return result;
    }
    if (result.costTiles > result.budgetTiles) {
        result.reason = fmt::format("move requires {} tiles, only {} available", result.costTiles, result.budgetTiles);
        return result;
    }
    const int pathCost = FindShortestNoDiagonalPathCost(
        registry,
        request.unit,
        unit.tileX,
        unit.tileY,
        request.targetTileX,
        request.targetTileY,
        result.budgetTiles);
    if (pathCost < 0) {
        result.reason = "move path crosses a wall or occupied tile";
        return result;
    }

    result.costTiles = pathCost;
    result.valid = true;
    return result;
}

void PublishResult(entt::registry& registry, const FTacticalD20CommandDropRequestedEvent& request, const FPathValidation& validation) {
    FTacticalD20MovementPathValidatedEvent event;
    event.unit = request.unit;
    event.commandId = request.commandId;
    event.hasTargetTile = request.hasTargetTile;
    event.targetTileX = request.targetTileX;
    event.targetTileY = request.targetTileY;
    event.valid = validation.valid;
    event.invalidReason = validation.reason;
    event.movementCostTiles = validation.costTiles;
    event.movementBudgetTiles = validation.budgetTiles;

    PublishAndQueueFrameEvent(registry, event);
}

} // namespace

void TacticalD20MovementPathValidationSystem::update(entt::registry& registry, float /*dt*/) {
    ZoneScopedN("TacticalD20MovementPathValidationSystem");

    auto* bus = registry.ctx().find<FEventBus>();
    if (!bus) return;

    for (const auto& request : bus->frameEvents<FTacticalD20CommandDropRequestedEvent>()) {
        if (request.commandId != "move") continue;
        PublishResult(registry, request, ValidateMovement(registry, request));
    }
}

} // namespace game
