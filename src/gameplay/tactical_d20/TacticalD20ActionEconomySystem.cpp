#include "gameplay/tactical_d20/TacticalD20ActionEconomySystem.h"

#include "core/events/FEventBus.h"
#include "ecs/components/FActionEconomyHasActionOnly.h"
#include "ecs/components/FActionEconomyHasMoveAndAction.h"
#include "ecs/components/FActionEconomyHasMoveOnly.h"
#include "ecs/components/FActionEconomyTurnComplete.h"
#include "ecs/components/FActiveTacticalUnit.h"
#include "ecs/components/FCombatStateAwaitingCommand.h"
#include "ecs/components/FCombatStateEnemyThinking.h"
#include "ecs/components/FCombatStateTurnStart.h"
#include "ecs/components/FTacticalTurnOrder.h"
#include "ecs/components/FTacticalUnit.h"
#include "ecs/components/FTurnBudget.h"
#include "gameplay/tactical_d20/FTacticalD20Config.h"
#include "gameplay/tactical_d20/FTacticalD20Events.h"

#include <algorithm>
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

template<typename NextTag>
void SetActionEconomyState(entt::registry& registry, entt::entity unitEntity) {
    ClearActionEconomyTags(registry, unitEntity);
    registry.emplace<NextTag>(unitEntity);
}

void ClearAllActionEconomy(entt::registry& registry) {
    auto view = registry.view<FTacticalUnit>();
    for (auto entity : view) {
        ClearActionEconomyTags(registry, entity);
        registry.remove<FTurnBudget>(entity);
    }
}

void ResetTurnBudget(entt::registry& registry, entt::entity unitEntity) {
    const auto* config = registry.ctx().find<FTacticalD20Config>();
    const int movementTiles = BaseMovementTiles(registry.get<FTacticalUnit>(unitEntity), config);
    registry.emplace_or_replace<FTurnBudget>(unitEntity, movementTiles, movementTiles);
    SetActionEconomyState<FActionEconomyHasMoveAndAction>(registry, unitEntity);
}

void ResetActiveUnitTurn(entt::registry& registry) {
    const auto active = ActiveUnit(registry);
    if (active != entt::null) {
        ClearAllActionEconomy(registry);
        ResetTurnBudget(registry, active);
    }
}

bool HasStateTag(entt::registry& registry, entt::entity unitEntity) {
    return registry.any_of<FActionEconomyHasMoveAndAction, FActionEconomyHasActionOnly, FActionEconomyHasMoveOnly, FActionEconomyTurnComplete>(unitEntity);
}

FTacticalD20CommandQueuedEvent DefaultCommandForState(entt::registry& registry, entt::entity unitEntity) {
    if (registry.all_of<FActionEconomyHasMoveAndAction>(unitEntity)) return {.unit = unitEntity, .actionId = "wait"};
    if (registry.all_of<FActionEconomyHasActionOnly>(unitEntity)) return {.unit = unitEntity, .actionId = "wait"};
    if (registry.all_of<FActionEconomyHasMoveOnly>(unitEntity)) return {.unit = unitEntity, .actionId = "wait"};
    return {.unit = unitEntity, .actionId = "wait"};
}

void PublishResolution(entt::registry& registry, entt::entity unitEntity, std::string_view actionId, bool turnComplete) {
    const int movement = registry.all_of<FTurnBudget>(unitEntity) ? registry.get<FTurnBudget>(unitEntity).movementBudgetTiles : 0;
    const FTacticalD20ActionResolvedEvent event{unitEntity, std::string(actionId), movement, turnComplete};
    PUBLISH(FTacticalD20ActionResolvedEvent, registry, event);
}

void ResolveMove(entt::registry& registry, entt::entity unitEntity, int spentTiles) {
    // Transition table:
    //   HasMoveAndAction + Move resolved -> HasActionOnly
    //   HasMoveOnly      + Move resolved -> TurnComplete
    auto& budget = registry.get<FTurnBudget>(unitEntity);
    budget.movementBudgetTiles = std::max(budget.movementBudgetTiles - std::max(spentTiles, 0), 0);
    if (registry.all_of<FActionEconomyHasMoveAndAction>(unitEntity)) SetActionEconomyState<FActionEconomyHasActionOnly>(registry, unitEntity);
    else SetActionEconomyState<FActionEconomyTurnComplete>(registry, unitEntity);
    PublishResolution(registry, unitEntity, "move", registry.all_of<FActionEconomyTurnComplete>(unitEntity));
}

void ResolveDash(entt::registry& registry, entt::entity unitEntity) {
    // Transition table:
    //   HasMoveAndAction + Dash resolved -> HasMoveOnly
    auto& budget = registry.get<FTurnBudget>(unitEntity);
    budget.movementBudgetTiles = budget.baseMovementTiles * 2;
    SetActionEconomyState<FActionEconomyHasMoveOnly>(registry, unitEntity);
    PublishResolution(registry, unitEntity, "dash", false);
}

void ResolveTurnEndingAction(entt::registry& registry, entt::entity unitEntity, std::string_view actionId) {
    // Transition table:
    //   HasMoveAndAction + Attack/Dodge/Wait resolved -> TurnComplete
    //   HasActionOnly    + Attack/Dodge/Wait resolved -> TurnComplete
    //   HasMoveOnly      + Wait resolved              -> TurnComplete
    SetActionEconomyState<FActionEconomyTurnComplete>(registry, unitEntity);
    PublishResolution(registry, unitEntity, actionId, true);
}

bool CanResolveMove(entt::registry& registry, entt::entity unitEntity) {
    return registry.any_of<FActionEconomyHasMoveAndAction, FActionEconomyHasMoveOnly>(unitEntity);
}

bool CanResolveTurnEndingAction(entt::registry& registry, entt::entity unitEntity, std::string_view actionId) {
    if (actionId == "wait") return true;
    return registry.any_of<FActionEconomyHasMoveAndAction, FActionEconomyHasActionOnly>(unitEntity);
}

void ResolveCommand(entt::registry& registry, const FTacticalD20CommandQueuedEvent& command) {
    if (command.unit == entt::null || !registry.valid(command.unit) || !registry.all_of<FTacticalUnit, FTurnBudget>(command.unit)) return;

    if (command.actionId == "move" && CanResolveMove(registry, command.unit)) return ResolveMove(registry, command.unit, command.movementSpentTiles);
    if (command.actionId == "dash" && registry.all_of<FActionEconomyHasMoveAndAction>(command.unit)) return ResolveDash(registry, command.unit);
    if ((command.actionId == "attack" || command.actionId == "dodge" || command.actionId == "wait")
        && CanResolveTurnEndingAction(registry, command.unit, command.actionId)) {
        return ResolveTurnEndingAction(registry, command.unit, command.actionId);
    }
}

bool IsEnemyThinking(entt::registry& registry) {
    auto enemyView = registry.view<FCombatStateEnemyThinking, FTacticalTurnOrder>();
    return enemyView.begin() != enemyView.end();
}

bool IsAwaitingCommand(entt::registry& registry) {
    auto playerView = registry.view<FCombatStateAwaitingCommand, FTacticalTurnOrder>();
    return playerView.begin() != playerView.end();
}

bool TryResolveQueuedCommand(entt::registry& registry, entt::entity active) {
    auto* bus = registry.ctx().find<FEventBus>();
    if (!bus) return false;

    for (const auto& command : bus->frameEvents<FTacticalD20CommandQueuedEvent>()) {
        if (command.unit != active) continue;

        ResolveCommand(registry, command);
        return registry.all_of<FActionEconomyTurnComplete>(active);
    }
    return false;
}

void AutoResolveEnemyCommand(entt::registry& registry, entt::entity active) {
    const auto command = DefaultCommandForState(registry, active);
    PUBLISH(FTacticalD20CommandQueuedEvent, registry, command);
    ResolveCommand(registry, command);
}

} // namespace

void TacticalD20ActionEconomySystem::update(entt::registry& registry, float /*dt*/) {
    ZoneScopedN("TacticalD20ActionEconomySystem");
    if (!registry.view<FCombatStateTurnStart>().empty()) return ResetActiveUnitTurn(registry);

    const auto active = ActiveUnit(registry);
    if (active == entt::null || registry.all_of<FActionEconomyTurnComplete>(active)) return;
    if (!HasStateTag(registry, active)) ResetTurnBudget(registry, active);
    if (IsAwaitingCommand(registry) && TryResolveQueuedCommand(registry, active)) return;
    if (IsEnemyThinking(registry)) AutoResolveEnemyCommand(registry, active);
}

} // namespace game
