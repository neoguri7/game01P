#pragma once

#include <cstdint>
#include <entt/entt.hpp>
#include <string>

namespace game {
struct FResolvedSkill;
}

namespace game::ecs {

/// True when `target` is a living unit the skill may legally hit from `actor`'s cell (faction, range, LOS).
[[nodiscard]] bool CanTargetWithSkill(entt::registry& registry, entt::entity actor, entt::entity target,
                                      const FResolvedSkill& skill);

struct EmberCombatHudSystem {
    int selectedSkill{0};
    std::uint64_t boundTurn{0};
    void update(entt::registry& registry, float deltaTime);
    void render(entt::registry& registry);
    [[nodiscard]] std::string name() const { return "EmberCombatHudSystem"; }
};

} // namespace game::ecs
