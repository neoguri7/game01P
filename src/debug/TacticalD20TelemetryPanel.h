#pragma once

#include <entt/entt.hpp>

namespace game {

class SystemManager;
class Time;

void RenderTacticalD20TelemetryPanel(entt::registry& registry, const Time& frameTime, const SystemManager& systemManager);

} // namespace game
