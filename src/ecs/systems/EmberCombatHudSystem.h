#pragma once

#include <cstdint>
#include <entt/entt.hpp>
#include <string>

#include "ecs/systems/EmberCombatInfo.h"
#include "gameplay/combat/FCombatTypes.h"

namespace game {
struct FResolvedSkill;
}

namespace game::ecs {

/// True when `target` is a living unit the skill may legally hit from `actor`'s cell (faction, range, LOS).
[[nodiscard]] bool CanTargetWithSkill(entt::registry& registry, entt::entity actor, entt::entity target,
                                      const FResolvedSkill& skill);

/// WP-12 (관례 a): every active HUD submit goes through WP-06's one command API with source Player. The
/// systems and the ImGui view share this helper, so the five converted sites and the test path are the
/// same call. The boolean is the API's acceptance result — a refused submit is already recorded and
/// logged by SubmitBattleCommand; callers must not act on it as if it had happened.
[[nodiscard]] bool SubmitHudAction(entt::registry& registry, FBattleAction action);

struct EmberCombatHudSystem {
    int selectedSkill{0};
    std::uint64_t boundTurn{0};
    void update(entt::registry& registry, float deltaTime);
    void render(entt::registry& registry);
    [[nodiscard]] std::string name() const { return "EmberCombatHudSystem"; }
};

} // namespace game::ecs
