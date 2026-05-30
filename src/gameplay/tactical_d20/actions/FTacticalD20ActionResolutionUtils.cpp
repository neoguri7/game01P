#include "gameplay/tactical_d20/actions/FTacticalD20ActionResolutionUtils.h"

#include "core/events/FEventPublishing.h"
#include "ecs/components/FCombatStateResolvingAction.h"
#include "ecs/components/FTacticalUnit.h"
#include "ecs/components/FTurnBudget.h"
#include "ecs/components/FUnitStateDefeated.h"
#include "gameplay/tactical_d20/events/FTacticalD20ActionEvents.h"
#include "gameplay/tactical_d20/logging/FTacticalD20LogUtils.h"

#include <fmt/format.h>

namespace game {

bool IsTacticalD20ResolvingAction(entt::registry& registry) {
    return !registry.view<FCombatStateResolvingAction>().empty();
}

bool TryTakeQueuedTacticalD20Command(entt::registry& registry,
    std::string_view actionId,
    entt::entity& unit,
    FQueuedTacticalD20Command& command) {
    if (!IsTacticalD20ResolvingAction(registry)) return false;

    auto view = registry.view<FTacticalUnit, FQueuedTacticalD20Command>(entt::exclude<FUnitStateDefeated>);
    for (auto entity : view) {
        const auto queued = view.get<FQueuedTacticalD20Command>(entity);
        if (queued.actionId != actionId) continue;
        registry.remove<FQueuedTacticalD20Command>(entity);
        if (!queued.validationApproved) return false;
        unit = entity;
        command = queued;
        return true;
    }
    return false;
}

void ClearTacticalD20ActionEconomy(entt::registry& registry, entt::entity unit) {
    registry.remove<FActionEconomyHasMoveAndAction, FActionEconomyHasActionOnly, FActionEconomyHasMoveOnly, FActionEconomyTurnComplete>(unit);
}

void PublishTacticalD20ActionResolved(entt::registry& registry, entt::entity unit, const std::string& action, bool complete) {
    const int movement = registry.all_of<FTurnBudget>(unit) ? registry.get<FTurnBudget>(unit).movementBudgetTiles : 0;
    const FTacticalD20ActionResolvedEvent event{unit, action, movement, complete};
    PublishAndQueueFrameEvent(registry, event);
    AppendTacticalD20EventLog(registry, fmt::format("[Event] ActionResolved action={} complete={}", action, complete));
    AppendTacticalD20StateLog(registry, fmt::format("[Action] {} resolved", action));
}

} // namespace game
