#pragma once

#include <cstdint>
#include <entt/entt.hpp>
#include <string>

namespace game::ecs {

struct EmberCombatHudSystem {
    int selectedSkill{0};
    std::uint64_t boundTurn{0};
    void update(entt::registry& registry, float deltaTime);
    void render(entt::registry& registry);
    [[nodiscard]] std::string name() const { return "EmberCombatHudSystem"; }
};

} // namespace game::ecs
