#include "gameplay/tactical_d20/systems/actions/TacticalD20MoveActionResolutionSystem.h"

#include "core/factories/FTacticalD20BoardPlacement.h"
#include "ecs/components/FActionEconomyHasActionOnly.h"
#include "ecs/components/FActionEconomyHasMoveOnly.h"
#include "ecs/components/FActionEconomyTurnComplete.h"
#include "ecs/components/FPosition.h"
#include "ecs/components/FTacticalUnit.h"
#include "ecs/components/FTurnBudget.h"
#include "gameplay/tactical_d20/actions/FTacticalD20ActionResolutionUtils.h"
#include "gameplay/tactical_d20/logging/FTacticalD20LogUtils.h"

#include <algorithm>
#include <fmt/format.h>
#include <tracy/Tracy.hpp>

namespace game {
namespace {

void ResolveMove(entt::registry& registry, entt::entity unit, const FQueuedTacticalD20Command& command) {
    ZoneScopedN("TacticalD20::ActionResolution");
    // Action economy transition table:
    //   HasMoveAndAction + Move resolved -> HasActionOnly
    //   HasMoveOnly + Move resolved -> TurnComplete
    //   any economy state + command.endTurnAfterResolution -> TurnComplete
    auto& tactical = registry.get<FTacticalUnit>(unit);
    tactical.tileX = command.targetTileX;
    tactical.tileY = command.targetTileY;
    if (auto* position = registry.try_get<FPosition>(unit)) {
        position->x = TacticalD20TileToWorldX(tactical.tileX);
        position->y = TacticalD20TileToWorldY(tactical.tileY);
    }

    auto& budget = registry.get<FTurnBudget>(unit);
    budget.movementBudgetTiles = std::max(budget.movementBudgetTiles - std::max(command.movementSpentTiles, 0), 0);

    const bool turnComplete = command.endTurnAfterResolution || registry.all_of<FActionEconomyHasMoveOnly>(unit);
    if (turnComplete) SetTacticalD20ActionEconomy<FActionEconomyTurnComplete>(registry, unit);
    else SetTacticalD20ActionEconomy<FActionEconomyHasActionOnly>(registry, unit);

    AppendTacticalD20CombatLog(registry, fmt::format("{} moved to ({}, {}).", tactical.displayName, tactical.tileX, tactical.tileY));
    PublishTacticalD20ActionResolved(registry, unit, "move", turnComplete);
}

} // namespace

void TacticalD20MoveActionResolutionSystem::update(entt::registry& registry, float /*dt*/) {
    ZoneScopedN("TacticalD20MoveActionResolutionSystem");
    entt::entity unit{entt::null};
    FQueuedTacticalD20Command command;
    if (TryTakeQueuedTacticalD20Command(registry, "move", unit, command)) ResolveMove(registry, unit, command);
}

} // namespace game
