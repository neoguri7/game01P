#pragma once

#include <entt/entt.hpp>

namespace game {

struct FTacticalD20CombatStateFactory {
    static entt::entity createSetupState(entt::registry& registry);
};

} // namespace game