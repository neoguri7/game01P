#pragma once

#include <entt/entt.hpp>

namespace game {

class SystemManager;

void RegisterTacticalD20TurnFlowSystems(SystemManager& systemManager);
void RegisterTacticalD20VisualSystems(SystemManager& systemManager);
void ValidateTacticalD20SystemOrder(SystemManager& systemManager, entt::registry& registry);

} // namespace game
