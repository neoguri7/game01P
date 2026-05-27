#pragma once

#include "gameplay/tactical_d20/FTacticalD20Rules.h"

#include <entt/entt.hpp>

namespace game {

struct FTacticalD20RollPolicy {
    ETacticalD20RollMode mode{ETacticalD20RollMode::Normal};
    bool disadvantageApplied{false};
};

FTacticalD20RollPolicy TacticalD20AttackRollPolicy(entt::registry& registry, entt::entity attacker, entt::entity target);
FTacticalD20RollPolicy TacticalD20AbilityCheckRollPolicy(entt::registry& registry, entt::entity unit);

} // namespace game
