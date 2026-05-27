#pragma once

#include "ecs/systems/ISystem.h"
#include <string>

namespace game {

struct TacticalD20SetupSystem : public ecs::ISystem {
    void update(entt::registry& registry, float dt) override;
    std::string name() const override { return "TacticalD20SetupSystem"; }
};

} // namespace game