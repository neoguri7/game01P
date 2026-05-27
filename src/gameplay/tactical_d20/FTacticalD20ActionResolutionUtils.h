#pragma once

#include "ecs/components/FActionEconomyHasActionOnly.h"
#include "ecs/components/FActionEconomyHasMoveAndAction.h"
#include "ecs/components/FActionEconomyHasMoveOnly.h"
#include "ecs/components/FActionEconomyTurnComplete.h"
#include "ecs/components/FQueuedTacticalD20Command.h"

#include <entt/entt.hpp>
#include <string>
#include <string_view>

namespace game {

bool IsTacticalD20ResolvingAction(entt::registry& registry);
bool TryTakeQueuedTacticalD20Command(entt::registry& registry,
    std::string_view actionId,
    entt::entity& unit,
    FQueuedTacticalD20Command& command);
void ClearTacticalD20ActionEconomy(entt::registry& registry, entt::entity unit);
void PublishTacticalD20ActionResolved(entt::registry& registry, entt::entity unit, const std::string& action, bool complete);

template<typename Tag>
void SetTacticalD20ActionEconomy(entt::registry& registry, entt::entity unit) {
    ClearTacticalD20ActionEconomy(registry, unit);
    registry.emplace<Tag>(unit);
}

} // namespace game
