#pragma once

#include "ecs/systems/ISystem.h"

namespace game::ecs {

struct GridRenderSystem : ISystem {
    void update(entt::registry&, float) override {}
    void render(entt::registry& registry) override;
    std::string name() const override { return "GridRenderSystem"; }
};

} // namespace game::ecs
