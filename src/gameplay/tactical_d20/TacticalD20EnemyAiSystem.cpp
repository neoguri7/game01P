#include "gameplay/tactical_d20/TacticalD20EnemyAiSystem.h"

#include "core/events/FEventBus.h"
#include "ecs/components/FActiveTacticalUnit.h"
#include "ecs/components/FCombatStateEnemyThinking.h"
#include "ecs/components/FQueuedTacticalD20Command.h"
#include "ecs/components/FTacticalBoardTile.h"
#include "ecs/components/FTacticalUnit.h"
#include "ecs/components/FTurnBudget.h"
#include "ecs/components/FUnitStateDefeated.h"
#include "gameplay/tactical_d20/FTacticalD20Config.h"
#include "gameplay/tactical_d20/FTacticalD20Events.h"
#include "gameplay/tactical_d20/FTacticalD20LogUtils.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fmt/format.h>
#include <string_view>
#include <tracy/Tracy.hpp>
#include <vector>

namespace game {
namespace {

struct FNode {
    int x{0};
    int y{0};
    int parent{-1};
};

bool IsEnemyThinking(entt::registry& registry) {
    return !registry.view<FCombatStateEnemyThinking>().empty();
}

entt::entity ActiveEnemy(entt::registry& registry) {
    auto view = registry.view<FActiveTacticalUnit, FTacticalUnit>(entt::exclude<FUnitStateDefeated>);
    for (auto entity : view) {
        if (view.get<FTacticalUnit>(entity).team == "enemy") return entity;
    }
    return entt::null;
}

bool HasAction(const FTacticalUnit& unit, std::string_view action) {
    return std::ranges::any_of(unit.actions, [action](const std::string& candidate) { return candidate == action; });
}

entt::entity NearestPlayer(entt::registry& registry, const FTacticalUnit& enemy) {
    entt::entity best = entt::null;
    int bestDistance = 1000000;
    auto view = registry.view<FTacticalUnit>(entt::exclude<FUnitStateDefeated>);
    for (auto entity : view) {
        const auto& unit = view.get<FTacticalUnit>(entity);
        if (unit.team != "player") continue;
        const int distance = std::abs(unit.tileX - enemy.tileX) + std::abs(unit.tileY - enemy.tileY);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = entity;
        }
    }
    return best;
}

bool TileExistsAndOpen(entt::registry& registry, int x, int y) {
    auto view = registry.view<FTacticalBoardTile>();
    for (auto entity : view) {
        const auto& tile = view.get<FTacticalBoardTile>(entity);
        if (tile.tileX == x && tile.tileY == y) return !tile.isWall;
    }
    return false;
}

bool Occupied(entt::registry& registry, int x, int y, entt::entity ignored) {
    auto view = registry.view<FTacticalUnit>(entt::exclude<FUnitStateDefeated>);
    for (auto entity : view) {
        if (entity == ignored) continue;
        const auto& unit = view.get<FTacticalUnit>(entity);
        if (unit.tileX == x && unit.tileY == y) return true;
    }
    return false;
}

bool Visited(const std::vector<FNode>& nodes, int x, int y) {
    return std::ranges::any_of(nodes, [x, y](const FNode& node) { return node.x == x && node.y == y; });
}

bool AdjacentTo(const FNode& node, const FTacticalUnit& target) {
    return std::abs(node.x - target.tileX) + std::abs(node.y - target.tileY) == 1;
}

std::vector<FNode> PathToAdjacent(entt::registry& registry, entt::entity enemyEntity, const FTacticalUnit& enemy, const FTacticalUnit& target) {
    constexpr std::array<FNode, 4> Directions{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
    std::vector<FNode> nodes{{enemy.tileX, enemy.tileY, -1}};
    int found = -1;
    for (std::size_t cursor = 0; cursor < nodes.size() && found < 0; ++cursor) {
        if (cursor > 0 && AdjacentTo(nodes[cursor], target)) {
            found = static_cast<int>(cursor);
            break;
        }
        for (const auto& direction : Directions) {
            const FNode next{nodes[cursor].x + direction.x, nodes[cursor].y + direction.y, static_cast<int>(cursor)};
            if (Visited(nodes, next.x, next.y)) continue;
            if (!TileExistsAndOpen(registry, next.x, next.y) || Occupied(registry, next.x, next.y, enemyEntity)) continue;
            nodes.push_back(next);
        }
    }
    if (found < 0) return {};

    std::vector<FNode> path;
    for (int index = found; index > 0; index = nodes[static_cast<std::size_t>(index)].parent) {
        path.push_back(nodes[static_cast<std::size_t>(index)]);
    }
    std::ranges::reverse(path);
    return path;
}

int SpeedTiles(entt::registry& registry, const FTacticalUnit& unit) {
    const auto* config = registry.ctx().find<FTacticalD20Config>();
    const int tileFeet = config ? std::max(config->tileFeet, 1) : 5;
    return std::max(unit.speedFeet / tileFeet, 0);
}

int MovementBudget(entt::registry& registry, entt::entity unit, const FTacticalUnit& tactical) {
    if (registry.all_of<FTurnBudget>(unit)) return std::max(registry.get<FTurnBudget>(unit).movementBudgetTiles, 0);
    return SpeedTiles(registry, tactical);
}

void QueueCommand(entt::registry& registry, entt::entity unit, const FTacticalD20CommandQueuedEvent& command) {
    PUBLISH(FTacticalD20CommandQueuedEvent, registry, command);
    QUEUE_FRAME_EVENT(FTacticalD20CommandQueuedEvent, registry, command);
    registry.emplace_or_replace<FQueuedTacticalD20Command>(unit, FQueuedTacticalD20Command{
        .actionId = command.actionId,
        .movementSpentTiles = command.movementSpentTiles,
        .hasTargetTile = command.hasTargetTile,
        .targetTileX = command.targetTileX,
        .targetTileY = command.targetTileY,
        .targetEntity = command.targetEntity,
        .validationApproved = true,
        .endTurnAfterResolution = command.endTurnAfterResolution,
    });
}

void QueueWait(entt::registry& registry, entt::entity enemy, const std::string& reason) {
    AppendTacticalD20CombatLog(registry, reason);
    QueueCommand(registry, enemy, {.unit = enemy, .actionId = "wait", .validationApproved = true});
}

void Think(entt::registry& registry, entt::entity enemyEntity) {
    ZoneScopedN("TacticalD20::EnemyAiDecision");
    if (registry.all_of<FQueuedTacticalD20Command>(enemyEntity)) return;
    const auto& enemy = registry.get<FTacticalUnit>(enemyEntity);
    const auto targetEntity = NearestPlayer(registry, enemy);
    if (targetEntity == entt::null) return QueueWait(registry, enemyEntity, fmt::format("{} waits: no target.", enemy.displayName));
    const auto& target = registry.get<FTacticalUnit>(targetEntity);
    const int distance = std::abs(target.tileX - enemy.tileX) + std::abs(target.tileY - enemy.tileY);
    const auto* config = registry.ctx().find<FTacticalD20Config>();
    const std::string_view aiType = config ? std::string_view(config->enemyAi.type) : std::string_view("nearestTargetMeleeFirst");
    if (aiType != "nearestTargetMeleeFirst") {
        return QueueWait(registry, enemyEntity, fmt::format("{} waits: unsupported enemy AI type '{}'.", enemy.displayName, aiType));
    }
    const bool canAttack = !config || config->enemyAi.attackIfInRange;
    const bool canMove = !config || config->enemyAi.moveTowardTargetIfOutOfRange;
    const bool endTurnAfterMove = config ? config->enemyAi.noAttackAfterMoveInFirstPrototype : true;

    if (distance == 1 && canAttack && HasAction(enemy, "attack")) {
        AppendTacticalD20CombatLog(registry, fmt::format("{} attacks {}.", enemy.displayName, target.displayName));
        return QueueCommand(registry, enemyEntity, {.unit = enemyEntity, .actionId = "attack", .targetEntity = targetEntity, .validationApproved = true});
    }
    if (!canMove || !HasAction(enemy, "move")) return QueueWait(registry, enemyEntity, fmt::format("{} waits.", enemy.displayName));

    auto path = PathToAdjacent(registry, enemyEntity, enemy, target);
    if (path.empty()) return QueueWait(registry, enemyEntity, fmt::format("{} waits: no valid path.", enemy.displayName));
    const int steps = std::min(MovementBudget(registry, enemyEntity, enemy), static_cast<int>(path.size()));
    if (steps <= 0) return QueueWait(registry, enemyEntity, fmt::format("{} waits: no movement budget.", enemy.displayName));
    const auto destination = path[static_cast<std::size_t>(steps - 1)];
    AppendTacticalD20CombatLog(registry, fmt::format("{} moves toward {}.", enemy.displayName, target.displayName));
    QueueCommand(registry, enemyEntity, {
        .unit = enemyEntity,
        .actionId = "move",
        .movementSpentTiles = steps,
        .hasTargetTile = true,
        .targetTileX = destination.x,
        .targetTileY = destination.y,
        .validationApproved = true,
        .endTurnAfterResolution = endTurnAfterMove,
    });
}

} // namespace

void TacticalD20EnemyAiSystem::update(entt::registry& registry, float /*dt*/) {
    ZoneScopedN("TacticalD20EnemyAiSystem");
    if (!IsEnemyThinking(registry)) return;
    const auto enemy = ActiveEnemy(registry);
    if (enemy != entt::null) Think(registry, enemy);
}

} // namespace game
