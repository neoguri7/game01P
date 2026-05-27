#include "gameplay/tactical_d20/TacticalD20TelemetrySystem.h"

#include "core/events/FEventBus.h"
#include "ecs/components/FActiveTacticalUnit.h"
#include "ecs/components/FCombatStateAwaitingCommand.h"
#include "ecs/components/FCombatStateDefeat.h"
#include "ecs/components/FCombatStateEnemyThinking.h"
#include "ecs/components/FCombatStateInitiativeRolling.h"
#include "ecs/components/FCombatStateNextTurn.h"
#include "ecs/components/FCombatStateResolvingAction.h"
#include "ecs/components/FCombatStateRoundStart.h"
#include "ecs/components/FCombatStateSetup.h"
#include "ecs/components/FCombatStateTurnEndCheck.h"
#include "ecs/components/FCombatStateTurnStart.h"
#include "ecs/components/FCombatStateVictory.h"
#include "ecs/components/FTacticalTurnOrder.h"
#include "ecs/components/FTacticalUnit.h"
#include "gameplay/tactical_d20/FTacticalD20BoardInteraction.h"
#include "gameplay/tactical_d20/FTacticalD20Events.h"
#include "gameplay/tactical_d20/FTacticalD20Telemetry.h"

#include <fmt/format.h>
#include <tracy/Tracy.hpp>

namespace game {
namespace {

template<typename Event>
void AddCount(const FEventBus& bus, FTacticalD20Telemetry& telemetry, const char* name) {
    const int count = static_cast<int>(bus.frameEvents<Event>().size());
    if (count > 0) telemetry.eventCountsThisFrame.push_back({name, count});
}

template<typename Tag>
bool StateNameIf(entt::registry& registry, std::string& state, const char* name) {
    if (registry.view<Tag>().empty()) return false;
    state = name;
    return true;
}

std::string CombatState(entt::registry& registry) {
    std::string state = "None";
    if (StateNameIf<FCombatStateSetup>(registry, state, "CombatSetup")) return state;
    if (StateNameIf<FCombatStateInitiativeRolling>(registry, state, "InitiativeRolling")) return state;
    if (StateNameIf<FCombatStateRoundStart>(registry, state, "RoundStart")) return state;
    if (StateNameIf<FCombatStateTurnStart>(registry, state, "TurnStart")) return state;
    if (StateNameIf<FCombatStateAwaitingCommand>(registry, state, "AwaitingCommand")) return state;
    if (StateNameIf<FCombatStateEnemyThinking>(registry, state, "EnemyThinking")) return state;
    if (StateNameIf<FCombatStateResolvingAction>(registry, state, "ResolvingAction")) return state;
    if (StateNameIf<FCombatStateTurnEndCheck>(registry, state, "TurnEndCheck")) return state;
    if (StateNameIf<FCombatStateNextTurn>(registry, state, "NextTurn")) return state;
    if (StateNameIf<FCombatStateVictory>(registry, state, "Victory")) return state;
    if (StateNameIf<FCombatStateDefeat>(registry, state, "Defeat")) return state;
    return state;
}

void CaptureEventCounts(const FEventBus& bus, FTacticalD20Telemetry& telemetry) {
    telemetry.eventCountsThisFrame.clear();
    AddCount<FTacticalD20CombatSetupRequestedEvent>(bus, telemetry, "CombatSetupRequested");
    AddCount<FTacticalD20CombatSetupCompletedEvent>(bus, telemetry, "CombatSetupCompleted");
    AddCount<FTacticalD20CombatStateChangedEvent>(bus, telemetry, "CombatStateChanged");
    AddCount<FTacticalD20InitiativeRollResolvedEvent>(bus, telemetry, "InitiativeRollResolved");
    AddCount<FTacticalD20RoundStartedEvent>(bus, telemetry, "RoundStarted");
    AddCount<FTacticalD20TurnStartedEvent>(bus, telemetry, "TurnStarted");
    AddCount<FTacticalD20ActiveUnitChangedEvent>(bus, telemetry, "ActiveUnitChanged");
    AddCount<FTacticalD20TurnEndedEvent>(bus, telemetry, "TurnEnded");
    AddCount<FTacticalD20CommandDragStartedEvent>(bus, telemetry, "CommandDragStarted");
    AddCount<FTacticalD20CommandSelectedEvent>(bus, telemetry, "CommandSelected");
    AddCount<FTacticalD20CommandDropRequestedEvent>(bus, telemetry, "CommandDropRequested");
    AddCount<FTacticalD20MovementPathValidatedEvent>(bus, telemetry, "MovementPathValidated");
    AddCount<FTacticalD20CommandDropValidatedEvent>(bus, telemetry, "CommandDropValidated");
    AddCount<FTacticalD20CommandAcceptedEvent>(bus, telemetry, "CommandAccepted");
    AddCount<FTacticalD20CommandQueuedEvent>(bus, telemetry, "CommandQueued");
    AddCount<FTacticalD20ActionResolvedEvent>(bus, telemetry, "ActionResolved");
    AddCount<FTacticalD20AttackRollResolvedEvent>(bus, telemetry, "AttackRollResolved");
    AddCount<FTacticalD20AttackResolvedEvent>(bus, telemetry, "AttackResolved");
    AddCount<FTacticalD20DamageAppliedEvent>(bus, telemetry, "DamageApplied");
    AddCount<FTacticalD20ConditionChangedEvent>(bus, telemetry, "ConditionChanged");
    AddCount<FTacticalD20ConditionAppliedEvent>(bus, telemetry, "ConditionApplied");
    AddCount<FTacticalD20ConditionTickedEvent>(bus, telemetry, "ConditionTicked");
    AddCount<FTacticalD20ConditionExpiredEvent>(bus, telemetry, "ConditionExpired");
}

void CaptureLastEvents(const FEventBus& bus, FTacticalD20Telemetry& telemetry) {
    for (const auto& event : bus.frameEvents<FTacticalD20CommandDropValidatedEvent>()) {
        telemetry.lastCommandDropResult = fmt::format("{} {}", event.commandId, event.valid ? "valid" : "invalid");
    }
    for (const auto& event : bus.frameEvents<FTacticalD20InitiativeRollResolvedEvent>()) telemetry.lastD20RollBreakdown = event.breakdown;
    for (const auto& event : bus.frameEvents<FTacticalD20AttackRollResolvedEvent>()) telemetry.lastD20RollBreakdown = event.breakdown;
}

void CaptureBoardInteraction(entt::registry& registry, FTacticalD20Telemetry& telemetry) {
    const auto* interaction = registry.ctx().find<FTacticalD20BoardInteraction>();
    if (!interaction) return;
    telemetry.hasHoveredTile = interaction->hasHoveredTile;
    telemetry.hoveredTileX = interaction->hoveredTileX;
    telemetry.hoveredTileY = interaction->hoveredTileY;
    telemetry.hasSelectedTile = interaction->hasSelectedTile;
    telemetry.selectedTileX = interaction->selectedTileX;
    telemetry.selectedTileY = interaction->selectedTileY;
    telemetry.hoveredEntity = interaction->hoveredEntity;
    telemetry.selectedEntity = interaction->selectedEntity;
}

} // namespace

void TacticalD20TelemetrySystem::update(entt::registry& registry, float dt) {
    ZoneScopedN("TacticalD20TelemetrySystem");
    if (!registry.ctx().contains<FTacticalD20Telemetry>()) registry.ctx().emplace<FTacticalD20Telemetry>();
    auto& telemetry = registry.ctx().get<FTacticalD20Telemetry>();
    telemetry.deltaTimeSeconds = dt;
    telemetry.entityCount = static_cast<int>(registry.storage<entt::entity>().size());
    telemetry.combatState = CombatState(registry);
    telemetry.activeUnit = entt::null;
    telemetry.activeUnitId = "None";
    auto activeView = registry.view<FActiveTacticalUnit, FTacticalUnit>();
    for (auto entity : activeView) {
        telemetry.activeUnit = entity;
        telemetry.activeUnitId = activeView.get<FTacticalUnit>(entity).id;
    }
    auto orderView = registry.view<FTacticalTurnOrder>();
    for (auto entity : orderView) telemetry.round = orderView.get<FTacticalTurnOrder>(entity).round;
    if (const auto* bus = registry.ctx().find<FEventBus>()) {
        CaptureEventCounts(*bus, telemetry);
        CaptureLastEvents(*bus, telemetry);
    }
    CaptureBoardInteraction(registry, telemetry);
}

} // namespace game
