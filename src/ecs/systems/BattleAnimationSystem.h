#pragma once

#include <string>

#include "ecs/systems/ISystem.h"
#include "presentation/FBattleAnim.h"

namespace game::ecs {

/// Plays what the combat rules already decided: unit motions, skill effects and the walk between cells.
/// It only reads the queued FBattleEvents; nothing in gameplay waits for it.
struct BattleAnimationSystem : ISystem {
    void update(entt::registry& registry, float deltaTime) override { UpdateBattleAnimations(registry, deltaTime); }
    std::string name() const override { return "BattleAnimationSystem"; }
};

} // namespace game::ecs
