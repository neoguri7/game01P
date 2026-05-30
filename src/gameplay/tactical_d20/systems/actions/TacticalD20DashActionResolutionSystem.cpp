#include "gameplay/tactical_d20/systems/actions/TacticalD20DashActionResolutionSystem.h"

#include "ecs/components/FActionEconomyHasMoveOnly.h"
#include "ecs/components/FTacticalUnit.h"
#include "ecs/components/FTurnBudget.h"
#include "gameplay/tactical_d20/actions/FTacticalD20ActionResolutionUtils.h"
#include "gameplay/tactical_d20/FTacticalD20Config.h"
#include "gameplay/tactical_d20/logging/FTacticalD20LogUtils.h"

#include <algorithm>
#include <fmt/format.h>
#include <tracy/Tracy.hpp>

namespace game {
namespace {

void ResolveDash(entt::registry& registry, entt::entity unit) {
    ZoneScopedN("TacticalD20::ActionResolution");
    // Action economy transition table:
    //   HasMoveAndAction + Dash resolved -> HasMoveOnly
    auto& budget = registry.get<FTurnBudget>(unit);
    const auto* config = registry.ctx().find<FTacticalD20Config>();
    const int multiplier = config ? std::max(config->actions.dashExtraMovementMultiplier, 0) : 1;
    budget.movementBudgetTiles += budget.baseMovementTiles * multiplier;
    SetTacticalD20ActionEconomy<FActionEconomyHasMoveOnly>(registry, unit);
    AppendTacticalD20CombatLog(registry, fmt::format("{} dashed.", registry.get<FTacticalUnit>(unit).displayName));
    PublishTacticalD20ActionResolved(registry, unit, "dash", false);
}

} // namespace

void TacticalD20DashActionResolutionSystem::update(entt::registry& registry, float /*dt*/) {
    ZoneScopedN("TacticalD20DashActionResolutionSystem");
    entt::entity unit{entt::null};
    FQueuedTacticalD20Command command;
    if (TryTakeQueuedTacticalD20Command(registry, "dash", unit, command)) ResolveDash(registry, unit);
}

} // namespace game
