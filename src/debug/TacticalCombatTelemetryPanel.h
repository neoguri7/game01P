#pragma once

#include <entt/entt.hpp>

namespace game {

class SystemManager;
class Time;

void RenderTacticalCombatTelemetryPanel(entt::registry& registry, const Time& frameTime, const SystemManager& systemManager);

} // namespace game
