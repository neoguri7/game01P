#pragma once

#include "ecs/systems/ISystem.h"

namespace game::ecs {

struct EmberGridRenderSystem : ISystem {
    void update(entt::registry&, float) override {}
    void render(entt::registry& registry) override;
    std::string name() const override { return "EmberGridRenderSystem"; }
};

} // namespace game::ecs
