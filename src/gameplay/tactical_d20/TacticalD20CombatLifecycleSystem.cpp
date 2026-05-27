#include "gameplay/tactical_d20/TacticalD20CombatLifecycleSystem.h"

#include "core/events/FEventBus.h"
#include "ecs/components/FActiveTacticalUnit.h"
#include "ecs/components/FActionEconomyTurnComplete.h"
#include "ecs/components/FActionEconomyHasActionOnly.h"
#include "ecs/components/FActionEconomyHasMoveAndAction.h"
#include "ecs/components/FActionEconomyHasMoveOnly.h"
#include "ecs/components/FCombatStateAwaitingCommand.h"
#include "ecs/components/FCombatStateDefeat.h"
#include "ecs/components/FCombatStateEnemyThinking.h"
#include "ecs/components/FCombatStateNextTurn.h"
#include "ecs/components/FCombatStateResolvingAction.h"
#include "ecs/components/FCombatStateRoundStart.h"
#include "ecs/components/FCombatStateTurnEndCheck.h"
#include "ecs/components/FCombatStateTurnStart.h"
#include "ecs/components/FCombatStateVictory.h"
#include "ecs/components/FQueuedTacticalD20Command.h"
#include "ecs/components/FTacticalTurnOrder.h"
#include "ecs/components/FTacticalUnit.h"
#include "ecs/components/FTurnBudget.h"
#include "ecs/components/FUnitStateDefeated.h"
#include "gameplay/tactical_d20/FTacticalD20Config.h"
#include "gameplay/tactical_d20/FTacticalD20Events.h"
#include "gameplay/tactical_d20/FTacticalD20StateLog.h"

#include <algorithm>
#include <fmt/format.h>
#include <type_traits>
#include <tracy/Tracy.hpp>

namespace game {
namespace {

void EnsureLifecycleServices(entt::registry& registry) {
    if (!registry.ctx().contains<FTacticalD20StateLog>()) registry.ctx().emplace<FTacticalD20StateLog>();
}

void AppendStateLog(entt::registry& registry, const std::string& line) {
    if (auto* log = registry.ctx().find<FTacticalD20StateLog>()) {
        log->lines.push_back(line);
        if (auto* config = registry.ctx().find<FTacticalD20Config>()) {
            while (static_cast<int>(log->lines.size()) > config->logging.stateLogMaxLines) log->lines.erase(log->lines.begin());
        }
    }
}

void PublishStateChange(entt::registry& registry, const char* previousState, const char* nextState) {
    AppendStateLog(registry, fmt::format("[CombatState] {} -> {}", previousState, nextState));
    const FTacticalD20CombatStateChangedEvent event{previousState, nextState};
    PUBLISH(FTacticalD20CombatStateChangedEvent, registry, event);
}

template<typename PreviousTag, typename NextTag>
void Transition(entt::registry& registry, entt::entity stateEntity, const char* previousState, const char* nextState) {
    registry.remove<PreviousTag>(stateEntity);
    registry.emplace<NextTag>(stateEntity);
    PublishStateChange(registry, previousState, nextState);
}

bool HasLivingTeam(entt::registry& registry, const char* team) {
    auto view = registry.view<FTacticalUnit>(entt::exclude<FUnitStateDefeated>);
    for (auto entity : view) {
        if (registry.get<FTacticalUnit>(entity).team == team) return true;
    }
    return false;
}

template<typename StateTag>
bool TryTerminalTransition(entt::registry& registry, entt::entity stateEntity, const char* currentState) {
    // Transition table:
    //   Any active combat state + all enemies defeated -> FCombatStateVictory
    //   Any active combat state + no living player unit -> FCombatStateDefeat
    if constexpr (std::is_same_v<StateTag, FCombatStateVictory> || std::is_same_v<StateTag, FCombatStateDefeat>) {
        return true;
    }
    if (!HasLivingTeam(registry, "enemy")) {
        Transition<StateTag, FCombatStateVictory>(registry, stateEntity, currentState, "Victory");
        return true;
    }
    if (!HasLivingTeam(registry, "player")) {
        Transition<StateTag, FCombatStateDefeat>(registry, stateEntity, currentState, "Defeat");
        return true;
    }
    return false;
}

void ClearActiveUnit(entt::registry& registry) {
    auto view = registry.view<FActiveTacticalUnit>();
    for (auto entity : view) registry.remove<FActiveTacticalUnit>(entity);
}

void AssignActiveUnit(entt::registry& registry, entt::entity unitEntity) {
    ClearActiveUnit(registry);
    registry.emplace_or_replace<FActiveTacticalUnit>(unitEntity);
}

bool SelectLivingUnitAtIndex(entt::registry& registry, FTacticalTurnOrder& order, int startIndex) {
    for (int offset = 0; offset < static_cast<int>(order.units.size()); ++offset) {
        const int candidateIndex = (startIndex + offset) % static_cast<int>(order.units.size());
        const auto candidate = order.units[static_cast<std::size_t>(candidateIndex)];
        if (registry.valid(candidate) && registry.all_of<FTacticalUnit>(candidate) && !registry.all_of<FUnitStateDefeated>(candidate)) {
            order.currentIndex = candidateIndex;
            AssignActiveUnit(registry, candidate);
            return true;
        }
    }
    return false;
}

bool SelectFirstTurn(entt::registry& registry, entt::entity stateEntity) {
    auto& order = registry.get<FTacticalTurnOrder>(stateEntity);
    order.round += 1;
    return SelectLivingUnitAtIndex(registry, order, 0);
}

bool SelectNextTurn(entt::registry& registry, entt::entity stateEntity) {
    auto& order = registry.get<FTacticalTurnOrder>(stateEntity);
    for (int index = order.currentIndex + 1; index < static_cast<int>(order.units.size()); ++index) {
        const auto candidate = order.units[static_cast<std::size_t>(index)];
        if (registry.valid(candidate) && registry.all_of<FTacticalUnit>(candidate) && !registry.all_of<FUnitStateDefeated>(candidate)) {
            order.currentIndex = index;
            AssignActiveUnit(registry, candidate);
            return true;
        }
    }
    return false;
}

bool IsRoundComplete(const FTacticalTurnOrder& order) {
    return order.currentIndex >= static_cast<int>(order.units.size()) - 1;
}

entt::entity ActiveUnit(entt::registry& registry) {
    auto view = registry.view<FActiveTacticalUnit, FTacticalUnit>(entt::exclude<FUnitStateDefeated>);
    for (auto entity : view) return entity;
    return entt::null;
}

void ClearActiveTurnState(entt::registry& registry, entt::entity unitEntity) {
    registry.remove<FActionEconomyHasMoveAndAction, FActionEconomyHasActionOnly, FActionEconomyHasMoveOnly, FActionEconomyTurnComplete>(unitEntity);
    registry.remove<FTurnBudget, FQueuedTacticalD20Command>(unitEntity);
}

void HandleRoundStart(entt::registry& registry, entt::entity stateEntity) {
    // Transition table:
    //   FCombatStateRoundStart + first living unit selected -> FCombatStateTurnStart
    if (SelectFirstTurn(registry, stateEntity)) Transition<FCombatStateRoundStart, FCombatStateTurnStart>(registry, stateEntity, "RoundStart", "TurnStart");
}

void HandleTurnStart(entt::registry& registry, entt::entity stateEntity) {
    // Transition table:
    //   FCombatStateTurnStart + player active unit -> FCombatStateAwaitingCommand
    //   FCombatStateTurnStart + enemy active unit  -> FCombatStateEnemyThinking
    const auto active = ActiveUnit(registry);
    if (active == entt::null) return;

    const auto& unit = registry.get<FTacticalUnit>(active);
    if (unit.team == "player") Transition<FCombatStateTurnStart, FCombatStateAwaitingCommand>(registry, stateEntity, "TurnStart", "AwaitingCommand");
    else Transition<FCombatStateTurnStart, FCombatStateEnemyThinking>(registry, stateEntity, "TurnStart", "EnemyThinking");
}

void HandleActionResolved(entt::registry& registry, entt::entity stateEntity) {
    // Transition table:
    //   FCombatStateResolvingAction + action result applied -> FCombatStateTurnEndCheck
    Transition<FCombatStateResolvingAction, FCombatStateTurnEndCheck>(registry, stateEntity, "ResolvingAction", "TurnEndCheck");
}

void HandleTurnEndCheck(entt::registry& registry, entt::entity stateEntity) {
    // Transition table:
    //   FCombatStateTurnEndCheck + active turn complete -> FCombatStateNextTurn
    //   FCombatStateTurnEndCheck + player active unit  -> FCombatStateAwaitingCommand
    //   FCombatStateTurnEndCheck + enemy active unit   -> FCombatStateEnemyThinking
    const auto active = ActiveUnit(registry);
    if (active == entt::null || registry.all_of<FActionEconomyTurnComplete>(active)) {
        if (active != entt::null) ClearActiveTurnState(registry, active);
        return Transition<FCombatStateTurnEndCheck, FCombatStateNextTurn>(registry, stateEntity, "TurnEndCheck", "NextTurn");
    }

    const auto& unit = registry.get<FTacticalUnit>(active);
    if (unit.team == "player") return Transition<FCombatStateTurnEndCheck, FCombatStateAwaitingCommand>(registry, stateEntity, "TurnEndCheck", "AwaitingCommand");
    Transition<FCombatStateTurnEndCheck, FCombatStateEnemyThinking>(registry, stateEntity, "TurnEndCheck", "EnemyThinking");
}

void HandleNextTurn(entt::registry& registry, entt::entity stateEntity) {
    // Transition table:
    //   FCombatStateNextTurn + next living unit found -> FCombatStateTurnStart
    //   FCombatStateNextTurn + all living units acted -> FCombatStateRoundStart
    if (IsRoundComplete(registry.get<FTacticalTurnOrder>(stateEntity)) || !SelectNextTurn(registry, stateEntity)) {
        Transition<FCombatStateNextTurn, FCombatStateRoundStart>(registry, stateEntity, "NextTurn", "RoundStart");
        return;
    }
    Transition<FCombatStateNextTurn, FCombatStateTurnStart>(registry, stateEntity, "NextTurn", "TurnStart");
}

bool HasQueuedCommandForActiveUnit(entt::registry& registry) {
    const auto active = ActiveUnit(registry);
    return active != entt::null && registry.all_of<FQueuedTacticalD20Command>(active);
}

bool HasResolvedActionForActiveUnit(entt::registry& registry) {
    const auto active = ActiveUnit(registry);
    if (active == entt::null) return false;

    auto* bus = registry.ctx().find<FEventBus>();
    if (!bus) return false;

    for (const auto& event : bus->frameEvents<FTacticalD20ActionResolvedEvent>()) {
        if (event.unit == active) return true;
    }
    return false;
}

} // namespace

void TacticalD20CombatLifecycleSystem::update(entt::registry& registry, float /*dt*/) {
    ZoneScopedN("TacticalD20CombatLifecycleSystem");
    EnsureLifecycleServices(registry);

    auto view = registry.view<FTacticalTurnOrder>();
    for (auto stateEntity : view) {
        if (registry.all_of<FCombatStateRoundStart>(stateEntity) && !TryTerminalTransition<FCombatStateRoundStart>(registry, stateEntity, "RoundStart")) return HandleRoundStart(registry, stateEntity);
        if (registry.all_of<FCombatStateTurnStart>(stateEntity) && !TryTerminalTransition<FCombatStateTurnStart>(registry, stateEntity, "TurnStart")) return HandleTurnStart(registry, stateEntity);
        if (registry.all_of<FCombatStateAwaitingCommand>(stateEntity) && !TryTerminalTransition<FCombatStateAwaitingCommand>(registry, stateEntity, "AwaitingCommand") && HasQueuedCommandForActiveUnit(registry)) return Transition<FCombatStateAwaitingCommand, FCombatStateResolvingAction>(registry, stateEntity, "AwaitingCommand", "ResolvingAction");
        if (registry.all_of<FCombatStateEnemyThinking>(stateEntity) && !TryTerminalTransition<FCombatStateEnemyThinking>(registry, stateEntity, "EnemyThinking") && HasQueuedCommandForActiveUnit(registry)) return Transition<FCombatStateEnemyThinking, FCombatStateResolvingAction>(registry, stateEntity, "EnemyThinking", "ResolvingAction");
        if (registry.all_of<FCombatStateResolvingAction>(stateEntity) && !TryTerminalTransition<FCombatStateResolvingAction>(registry, stateEntity, "ResolvingAction") && HasResolvedActionForActiveUnit(registry)) return HandleActionResolved(registry, stateEntity);
        if (registry.all_of<FCombatStateTurnEndCheck>(stateEntity) && !TryTerminalTransition<FCombatStateTurnEndCheck>(registry, stateEntity, "TurnEndCheck")) return HandleTurnEndCheck(registry, stateEntity);
        if (registry.all_of<FCombatStateNextTurn>(stateEntity) && !TryTerminalTransition<FCombatStateNextTurn>(registry, stateEntity, "NextTurn")) return HandleNextTurn(registry, stateEntity);
    }
}

} // namespace game
