#include "gameplay/tactical_d20/TacticalD20MovementPathValidationSystem.h"

#include "core/events/FEventBus.h"
#include "ecs/components/FUnitStateDefeated.h"
#include "ecs/components/FTacticalBoardTile.h"
#include "ecs/components/FTacticalUnit.h"
#include "ecs/components/FTurnBudget.h"
#include "gameplay/tactical_d20/FTacticalD20Events.h"

#include <algorithm>
#include <cmath>
#include <fmt/format.h>
#include <string>
#include <tracy/Tracy.hpp>

namespace game {
namespace {

struct FPathValidation {
    bool valid{false};
    std::string reason;
    int costTiles{0};
    int budgetTiles{0};
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
    return IsWall(registry, tileX, tileY) || OccupyingUnitAt(registry, tileX, tileY, unit) != entt::null;
}

bool IsEndpoint(int tileX, int tileY, int startX, int startY, int targetX, int targetY) {
    return (tileX == startX && tileY == startY) || (tileX == targetX && tileY == targetY);
}

bool SegmentClear(entt::registry& registry,
                  entt::entity unit,
                  int fromX,
                  int fromY,
                  int toX,
                  int toY,
                  int startX,
                  int startY,
                  int targetX,
                  int targetY) {
    const int stepX = (toX > fromX) - (toX < fromX);
    const int stepY = (toY > fromY) - (toY < fromY);
    int x = fromX;
    int y = fromY;

    while (x != toX || y != toY) {
        x += stepX;
        y += stepY;
        if (IsEndpoint(x, y, startX, startY, targetX, targetY)) continue;
        if (IsBlockedPathTile(registry, unit, x, y)) return false;
    }
    return true;
}

bool ManhattanPathClear(entt::registry& registry, entt::entity unit, int startX, int startY, int targetX, int targetY) {
    const bool horizontalThenVertical =
        SegmentClear(registry, unit, startX, startY, targetX, startY, startX, startY, targetX, targetY)
        && SegmentClear(registry, unit, targetX, startY, targetX, targetY, startX, startY, targetX, targetY);
    if (horizontalThenVertical) return true;

    return SegmentClear(registry, unit, startX, startY, startX, targetY, startX, startY, targetX, targetY)
        && SegmentClear(registry, unit, startX, targetY, targetX, targetY, startX, startY, targetX, targetY);
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
    if (!ManhattanPathClear(registry, request.unit, unit.tileX, unit.tileY, request.targetTileX, request.targetTileY)) {
        result.reason = "move path crosses a wall or occupied tile";
        return result;
    }

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

    PUBLISH(FTacticalD20MovementPathValidatedEvent, registry, event);
    QUEUE_FRAME_EVENT(FTacticalD20MovementPathValidatedEvent, registry, event);
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
