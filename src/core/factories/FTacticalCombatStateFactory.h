#pragma once

#include <entt/entt.hpp>

namespace game {

struct FTacticalCombatStateFactory {
    static entt::entity createSetupState(entt::registry& registry);
};

} // namespace game
