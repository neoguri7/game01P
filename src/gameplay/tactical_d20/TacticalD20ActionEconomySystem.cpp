#include "gameplay/tactical_d20/TacticalD20ActionEconomySystem.h"

#include "core/events/FEventBus.h"
#include "ecs/components/FActionEconomyHasActionOnly.h"
#include "ecs/components/FActionEconomyHasMoveAndAction.h"
#include "ecs/components/FActionEconomyHasMoveOnly.h"
#include "ecs/components/FActionEconomyTurnComplete.h"
#include "ecs/components/FActiveTacticalUnit.h"
#include "ecs/components/FCombatStateAwaitingCommand.h"
#include "ecs/components/FCombatStateTurnStart.h"
#include "ecs/components/FQueuedTacticalD20Command.h"
#include "ecs/components/FTacticalUnit.h"
#include "ecs/components/FTurnBudget.h"
#include "gameplay/tactical_d20/FTacticalD20Config.h"
#include "gameplay/tactical_d20/FTacticalD20Events.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <tracy/Tracy.hpp>

namespace game {
namespace {

int BaseMovementTiles(const FTacticalUnit& unit, const FTacticalD20Config* config) {
    const int tileFeet = config ? std::max(config->tileFeet, 1) : 5;
    return std::max(unit.speedFeet / tileFeet, 0);
}

entt::entity ActiveUnit(entt::registry& registry) {
    auto view = registry.view<FActiveTacticalUnit, FTacticalUnit>();
    for (auto entity : view) return entity;
    return entt::null;
}

void ClearActionEconomyTags(entt::registry& registry, entt::entity unitEntity) {
    registry.remove<FActionEconomyHasMoveAndAction, FActionEconomyHasActionOnly, FActionEconomyHasMoveOnly, FActionEconomyTurnComplete>(unitEntity);
}

void ResetTurnBudget(entt::registry& registry, entt::entity unitEntity) {
    const auto* config = registry.ctx().find<FTacticalD20Config>();
    const int movementTiles = BaseMovementTiles(registry.get<FTacticalUnit>(unitEntity), config);
    ClearActionEconomyTags(registry, unitEntity);
    registry.remove<FQueuedTacticalD20Command>(unitEntity);
    registry.emplace_or_replace<FTurnBudget>(unitEntity, movementTiles, movementTiles);
    registry.emplace<FActionEconomyHasMoveAndAction>(unitEntity);
}

void ResetActiveUnitTurn(entt::registry& registry) {
    const auto active = ActiveUnit(registry);
    if (active != entt::null) ResetTurnBudget(registry, active);
}

bool HasStateTag(entt::registry& registry, entt::entity unitEntity) {
    return registry.any_of<FActionEconomyHasMoveAndAction, FActionEconomyHasActionOnly, FActionEconomyHasMoveOnly, FActionEconomyTurnComplete>(unitEntity);
}

bool UnitHasAction(const FTacticalUnit& unit, std::string_view actionId) {
    return std::ranges::any_of(unit.actions, [actionId](const std::string& action) {
        return action == actionId;
    });
}

bool CanResolveMove(entt::registry& registry, entt::entity unitEntity) {
    return registry.any_of<FActionEconomyHasMoveAndAction, FActionEconomyHasMoveOnly>(unitEntity);
}

bool CanResolveTurnEndingAction(entt::registry& registry, entt::entity unitEntity, std::string_view actionId) {
    if (actionId == "wait") return true;
    return registry.any_of<FActionEconomyHasMoveAndAction, FActionEconomyHasActionOnly>(unitEntity);
}

bool CanAcceptCommand(entt::registry& registry, const FTacticalD20CommandQueuedEvent& command) {
    if (command.unit == entt::null || !registry.valid(command.unit) || !registry.all_of<FTacticalUnit, FTurnBudget>(command.unit)) return false;

    const auto& unit = registry.get<FTacticalUnit>(command.unit);
    if (!UnitHasAction(unit, command.actionId)) return false;

    if (command.actionId == "move") return CanResolveMove(registry, command.unit);
    if (command.actionId == "dash") return registry.all_of<FActionEconomyHasMoveAndAction>(command.unit);
    if (command.actionId == "attack" || command.actionId == "dodge" || command.actionId == "wait") {
        return CanResolveTurnEndingAction(registry, command.unit, command.actionId);
    }
    return false;
}

bool IsAwaitingCommand(entt::registry& registry) {
    auto playerView = registry.view<FCombatStateAwaitingCommand>();
    return playerView.begin() != playerView.end();
}

bool TryStoreQueuedCommand(entt::registry& registry, entt::entity active) {
    if (registry.all_of<FQueuedTacticalD20Command>(active)) return true;

    auto* bus = registry.ctx().find<FEventBus>();
    if (!bus) return false;

    for (const auto& accepted : bus->frameEvents<FTacticalD20CommandAcceptedEvent>()) {
        if (accepted.unit != active) continue;

        const FTacticalD20CommandQueuedEvent command{
            .unit = accepted.unit,
            .actionId = accepted.commandId,
            .movementSpentTiles = accepted.movementSpentTiles,
            .hasTargetTile = accepted.hasTargetTile,
            .targetTileX = accepted.targetTileX,
            .targetTileY = accepted.targetTileY,
            .targetEntity = accepted.targetEntity,
            .validationApproved = true,
        };
        if (!CanAcceptCommand(registry, command)) continue;
        PUBLISH(FTacticalD20CommandQueuedEvent, registry, command);
        QUEUE_FRAME_EVENT(FTacticalD20CommandQueuedEvent, registry, command);
        registry.emplace_or_replace<FQueuedTacticalD20Command>(
            active,
            command.actionId,
            command.movementSpentTiles,
            command.hasTargetTile,
            command.targetTileX,
            command.targetTileY,
            command.targetEntity,
            command.validationApproved);
        return true;
    }
    return false;
}

} // namespace

void TacticalD20ActionEconomySystem::update(entt::registry& registry, float /*dt*/) {
    ZoneScopedN("TacticalD20ActionEconomySystem");
    if (!registry.view<FCombatStateTurnStart>().empty()) return ResetActiveUnitTurn(registry);

    const auto active = ActiveUnit(registry);
    if (active == entt::null || registry.all_of<FActionEconomyTurnComplete>(active)) return;
    if (!HasStateTag(registry, active)) ResetTurnBudget(registry, active);
    if (IsAwaitingCommand(registry)) {
        TryStoreQueuedCommand(registry, active);
        return;
    }
}

} // namespace game
