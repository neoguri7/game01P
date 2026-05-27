#include "gameplay/tactical_d20/TacticalD20ActionEconomySystem.h"

#include "core/events/FEventBus.h"
#include "ecs/components/FActionEconomyHasActionOnly.h"
#include "ecs/components/FActionEconomyHasMoveAndAction.h"
#include "ecs/components/FActionEconomyHasMoveOnly.h"
#include "ecs/components/FActionEconomyTurnComplete.h"
#include "ecs/components/FActiveTacticalUnit.h"
#include "ecs/components/FCombatStateAwaitingCommand.h"
#include "ecs/components/FCombatStateEnemyThinking.h"
#include "ecs/components/FCombatStateResolvingAction.h"
#include "ecs/components/FCombatStateTurnStart.h"
#include "ecs/components/FQueuedTacticalD20Command.h"
#include "ecs/components/FTacticalTurnOrder.h"
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

template<typename NextTag>
void SetActionEconomyState(entt::registry& registry, entt::entity unitEntity) {
    ClearActionEconomyTags(registry, unitEntity);
    registry.emplace<NextTag>(unitEntity);
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

FTacticalD20CommandQueuedEvent DefaultCommandForState(entt::registry& registry, entt::entity unitEntity) {
    constexpr std::string_view turnEndingActions[] = {"wait", "attack", "dodge"};

    for (const auto actionId : turnEndingActions) {
        FTacticalD20CommandQueuedEvent command{.unit = unitEntity, .actionId = std::string(actionId)};
        if (CanAcceptCommand(registry, command)) return command;
    }
    FTacticalD20CommandQueuedEvent dashCommand{.unit = unitEntity, .actionId = "dash"};
    if (CanAcceptCommand(registry, dashCommand)) return dashCommand;
    FTacticalD20CommandQueuedEvent moveCommand{.unit = unitEntity, .actionId = "move"};
    if (CanAcceptCommand(registry, moveCommand)) return moveCommand;
    return {.unit = entt::null, .actionId = ""};
}

void PublishResolution(entt::registry& registry, entt::entity unitEntity, std::string_view actionId, bool turnComplete) {
    const int movement = registry.all_of<FTurnBudget>(unitEntity) ? registry.get<FTurnBudget>(unitEntity).movementBudgetTiles : 0;
    const FTacticalD20ActionResolvedEvent event{unitEntity, std::string(actionId), movement, turnComplete};
    PUBLISH(FTacticalD20ActionResolvedEvent, registry, event);
    QUEUE_FRAME_EVENT(FTacticalD20ActionResolvedEvent, registry, event);
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
    const auto* config = registry.ctx().find<FTacticalD20Config>();
    const int extraMovementMultiplier = config ? std::max(config->actions.dashExtraMovementMultiplier, 0) : 1;
    budget.movementBudgetTiles += budget.baseMovementTiles * extraMovementMultiplier;
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

void ResolveCommand(entt::registry& registry, const FTacticalD20CommandQueuedEvent& command) {
    if (!CanAcceptCommand(registry, command)) return;

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

bool IsResolvingAction(entt::registry& registry) {
    auto resolvingView = registry.view<FCombatStateResolvingAction, FTacticalTurnOrder>();
    return resolvingView.begin() != resolvingView.end();
}

bool TryResolveQueuedCommand(entt::registry& registry, entt::entity active) {
    if (!registry.all_of<FQueuedTacticalD20Command>(active)) return false;

    const auto queuedCommand = registry.get<FQueuedTacticalD20Command>(active);
    registry.remove<FQueuedTacticalD20Command>(active);
    ResolveCommand(registry, FTacticalD20CommandQueuedEvent{active, queuedCommand.actionId, queuedCommand.movementSpentTiles});
    return true;
}

bool TryStoreQueuedCommand(entt::registry& registry, entt::entity active) {
    if (registry.all_of<FQueuedTacticalD20Command>(active)) return true;

    auto* bus = registry.ctx().find<FEventBus>();
    if (!bus) return false;

    for (const auto& command : bus->frameEvents<FTacticalD20CommandQueuedEvent>()) {
        if (command.unit != active) continue;
        if (!CanAcceptCommand(registry, command)) continue;

        registry.emplace_or_replace<FQueuedTacticalD20Command>(active, command.actionId, command.movementSpentTiles);
        return true;
    }
    return false;
}

void QueueEnemyCommand(entt::registry& registry, entt::entity active) {
    if (registry.all_of<FQueuedTacticalD20Command>(active)) return;

    const auto command = DefaultCommandForState(registry, active);
    if (command.unit == entt::null) return;

    PUBLISH(FTacticalD20CommandQueuedEvent, registry, command);
    QUEUE_FRAME_EVENT(FTacticalD20CommandQueuedEvent, registry, command);
    registry.emplace_or_replace<FQueuedTacticalD20Command>(active, command.actionId, command.movementSpentTiles);
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
    if (IsEnemyThinking(registry)) {
        QueueEnemyCommand(registry, active);
        return;
    }
    if (!IsResolvingAction(registry)) return;
    TryResolveQueuedCommand(registry, active);
}

} // namespace game
