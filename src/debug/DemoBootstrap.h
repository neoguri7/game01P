#pragma once

#include <entt/entt.hpp>

namespace game {

void BootstrapDemoScene(entt::registry& registry);
entt::entity CreateDebugDemoEntity(entt::registry& registry);

} // namespace game
