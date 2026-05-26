#pragma once

#include <entt/entt.hpp>

namespace game {

struct FDemoPlayerFactory {
    static entt::entity create(entt::registry& registry);
};

} // namespace game
