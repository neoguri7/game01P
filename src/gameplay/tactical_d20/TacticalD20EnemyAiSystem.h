#pragma once

#include "ecs/systems/ISystem.h"
#include <string>

namespace game {

struct TacticalD20EnemyAiSystem : public ecs::ISystem {
    void update(entt::registry& registry, float dt) override;
    std::string name() const override { return "TacticalD20EnemyAiSystem"; }
};

} // namespace game
