#pragma once

#include <entt/entt.hpp>
#include <glm/vec2.hpp>

namespace game {

bool IsFallbackTacticalD20TurnPanelHit(entt::registry& registry, const glm::vec2& point);

} // namespace game
