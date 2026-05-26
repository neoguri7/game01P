#pragma once

#include <entt/entt.hpp>

namespace game {

class SystemManager;

void RegisterDefaultSystems(SystemManager& systemManager, entt::registry& registry);

} // namespace game
